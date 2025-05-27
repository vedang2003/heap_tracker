// heap_tracker_observer_socket.cc
#include "heap_tracker_observer_socket.h"  // Includes the revised header
#include <cassert>
#include <iostream>
#include <string>

#ifdef ENABLE_SOCKET_OUTPUT
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>           // For getpid(), close()
#include <cerrno>             // For errno
#include <cstring>            // For strerror
#include <nlohmann/json.hpp>  // Make sure nlohmann::json is available
#endif  // ENABLE_SOCKET_OUTPUT

// Constructor: Attempts to connect and register PID on creation.
HeapObserverSocket::HeapObserverSocket(HeapTrackOptions heap_track_options,
                                       const char * server_ip, int server_port)
    : heap_track_options_(heap_track_options)
#ifdef ENABLE_SOCKET_OUTPUT
      ,  // Use comma here when ENABLE_SOCKET_OUTPUT is defined
      server_ip_(server_ip),
      server_port_(server_port),
      sock_fd_(-1)  // Initialized via member initializer list in header
#endif
{
#ifdef ENABLE_SOCKET_OUTPUT
  // Attempt to connect and register when the observer is constructed
  if (!connectAndRegister()) {  // Call the combined helper function
    // Error message already printed by connectAndRegister
    std::cerr << "HeapObserverSocket: Initial connection/registration failed. "
                 "Socket output will be disabled."
              << std::endl;
    // sock_fd_ remains -1
  }
#else
  // Silence unused parameter warnings when socket output is disabled
  (void)server_ip;
  (void)server_port;
#endif  // ENABLE_SOCKET_OUTPUT
}

// Destructor implementation
HeapObserverSocket::~HeapObserverSocket() {
#ifdef ENABLE_SOCKET_OUTPUT
  if (sock_fd_ != -1) {
    std::cout << "HeapObserverSocket: Closing socket in destructor."
              << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
  }
#endif
}

// GetHeapTrackOptions implementation
HeapTrackOptions
HeapObserverSocket::GetHeapTrackOptions() const {
  return heap_track_options_;
}

#ifdef ENABLE_SOCKET_OUTPUT
// --- Implementation of Private Helper Member Functions ---

// connectAndRegister Member Function: Connects TCP and sends PID registration.
bool
HeapObserverSocket::connectAndRegister() {
  // Ensure any previous socket is closed before creating a new one
  if (sock_fd_ != -1) {
    close(sock_fd_);
    sock_fd_ = -1;
  }

  // 1. Create Socket
  sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd_ == -1) {
    std::cerr << "HeapObserverSocket: Could not create socket: "
              << strerror(errno) << std::endl;
    return false;
  }

  // 2. Prepare Server Address
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port =
    htons(static_cast<uint16_t>(server_port_));  // Use member variable

  if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <=
      0) {  // Use member variable
    std::cerr << "HeapObserverSocket: Invalid address/ Address not supported: "
              << strerror(errno) << std::endl;
    close(sock_fd_);  // Close the created socket
    sock_fd_ = -1;
    return false;
  }

  // 3. Connect TCP Socket
  if (connect(sock_fd_,
              reinterpret_cast<struct sockaddr *>(
                &server_addr),  // Use member variable
              sizeof(server_addr)) < 0) {
    std::cerr << "HeapObserverSocket: Connection Failed: " << strerror(errno)
              << std::endl;
    close(sock_fd_);  // Close the created socket
    sock_fd_ = -1;
    return false;
  }

  std::cout << "HeapObserverSocket: TCP Connection Established to "
            << server_ip_ << ":" << server_port_ << std::endl;

  // 4. Send Registration Message (if TCP connect succeeded)
  json registration_msg;
  registration_msg["type"] = "register";  // Use "type" key
  registration_msg["pid"]  = getpid();

  std::cout << "HeapObserverSocket: Sending registration for PID " << getpid()
            << std::endl;

  // Use SendDataToSocket member function to send registration
  // It will handle errors and close/reset sock_fd_ if sending fails.
  SendDataToSocket(registration_msg);

  // Check if SendDataToSocket failed (it resets sock_fd_ on failure)
  if (sock_fd_ == -1) {
    std::cerr << "HeapObserverSocket: Failed to send registration message "
                 "after connecting."
              << std::endl;
    return false;  // Indicate overall failure
  }

  std::cout << "HeapObserverSocket: Registration successful for PID "
            << getpid() << std::endl;
  return true;  // Both connection and registration succeeded
}


// SendDataToSocket Member Function: Sends JSON data. Handles errors.
void
HeapObserverSocket::SendDataToSocket(const json & data) {
  // This function assumes sock_fd_ is valid when called externally,
  // but checks internally just in case and for calls from connectAndRegister.
  if (sock_fd_ == -1) {
    // std::cerr << "HeapObserverSocket: Attempted send on invalid socket." <<
    // std::endl;
    return;  // Cannot send if socket is not valid
  }

  bool success = false;
  try {
    std::string json_str = data.dump() + "\n";  // Append newline
    // Use MSG_NOSIGNAL to prevent SIGPIPE
    ssize_t bytes_sent =
      send(sock_fd_, json_str.c_str(), json_str.length(), MSG_NOSIGNAL);

    if (bytes_sent == -1) {
      // EPIPE means the other end closed the connection gracefully (or
      // forcibly)
      if (errno == EPIPE) {
        std::cerr << "HeapObserverSocket: Send failed: Connection closed by "
                     "peer (EPIPE)."
                  << std::endl;
      }
      else {
        std::cerr << "HeapObserverSocket: Error sending data: "
                  << strerror(errno) << std::endl;
      }
      // Failure case handled below by checking 'success'
    }
    else if (static_cast<size_t>(bytes_sent) != json_str.length()) {
      std::cerr << "HeapObserverSocket: Warning: Incomplete send. Sent "
                << bytes_sent << " of " << json_str.length() << std::endl;
      // Treat incomplete send as failure
    }
    else {
      // std::cout << "DEBUG: Sent: " << json_str; // Verbose debug
      success = true;  // Send successful
    }
  } catch (const nlohmann::json::exception & e) {
    std::cerr << "HeapObserverSocket: JSON dump error during send: " << e.what()
              << std::endl;
  } catch (const std::exception & e) {
    std::cerr << "HeapObserverSocket: Standard exception during send: "
              << e.what() << std::endl;
  } catch (...) {
    std::cerr << "HeapObserverSocket: Unknown exception during send."
              << std::endl;
  }

  // If send was not successful, close the socket and mark as invalid
  if (!success && sock_fd_ != -1) {
    close(sock_fd_);
    sock_fd_ = -1;  // CRITICAL: Mark socket as invalid on failure
  }
}

#endif  // ENABLE_SOCKET_OUTPUT


// --- Implementation of Public Interface Methods ---

// OnAlloc implementation
void
HeapObserverSocket::OnAlloc(AllocCallbackInfo const & alloc_cb_info) {
#ifdef ENABLE_SOCKET_OUTPUT
  // If socket is not valid (initial connection failed or later disconnected),
  // do nothing.
  if (sock_fd_ == -1) {
    // std::cerr << "DEBUG: OnAlloc called but socket not connected." <<
    // std::endl; // Optional debug
    return;
  }

  json j;
  j["event_type"] = "A";  // Use "event_type" key
  // *** PID is NO LONGER sent here ***
  j["size"]      = alloc_cb_info.alloc_cb_size;
  j["address"]   = reinterpret_cast<uintptr_t>(alloc_cb_info.alloc_cb_ptr);
  j["timepoint"] = TimePointToUsecsCount(alloc_cb_info.alloc_cb_timepoint);
  if (heap_track_options_.track_callstack) {
    j["callstack"] = alloc_cb_info.alloc_cb_callstack.ToString();
  }
  else {
    j["callstack"] = "";  // Send empty if not tracked
  }

  // Send the allocation event data using the member function
  SendDataToSocket(j);
  // If SendDataToSocket failed, it already closed the socket and set sock_fd_
  // to -1. Subsequent calls in this state will simply return early.

#else
  // Silence unused parameter warning
  (void)alloc_cb_info;
#endif  // ENABLE_SOCKET_OUTPUT
}

// OnFree implementation
void
HeapObserverSocket::OnFree(FreeCallbackInfo const & free_cb_info) {
#ifdef ENABLE_SOCKET_OUTPUT
  // If socket is not valid, do nothing.
  if (sock_fd_ == -1) {
    // std::cerr << "DEBUG: OnFree called but socket not connected." <<
    // std::endl; // Optional debug
    return;
  }

  json j;
  j["event_type"] = "D";  // Use "event_type" key
  // *** PID is NO LONGER sent here ***
  j["address"]   = reinterpret_cast<uintptr_t>(free_cb_info.free_cb_ptr);
  j["timepoint"] = TimePointToUsecsCount(free_cb_info.free_cb_timepoint);
  // Size on free is usually not sent unless specifically needed/available
  // j["size"] = free_cb_info.free_cb_size;


  // Send the deallocation event data using the member function
  SendDataToSocket(j);
  // If SendDataToSocket failed, it already closed the socket and set sock_fd_
  // to -1.

#else
  // Silence unused parameter warning
  (void)free_cb_info;
#endif  // ENABLE_SOCKET_OUTPUT
}

// OnComplete implementation
void
HeapObserverSocket::OnComplete() {
#ifdef ENABLE_SOCKET_OUTPUT
  // Ensure socket is closed cleanly if still open
  if (sock_fd_ != -1) {
    std::cout << "HeapObserverSocket: OnComplete called, closing socket."
              << std::endl;
    close(sock_fd_);
    sock_fd_ = -1;
  }
#endif  // ENABLE_SOCKET_OUTPUT
}

// Dump implementation
void
HeapObserverSocket::Dump() const {
  std::cout << "HeapObserverSocket: Dump() called (no action)." << std::endl;
}

// Reset implementation
void
HeapObserverSocket::Reset() {
#ifdef ENABLE_SOCKET_OUTPUT
  std::cout << "HeapObserverSocket: Reset() called. Attempting reconnect and "
               "re-register."
            << std::endl;
  // Close existing connection if any (handled within connectAndRegister)
  // Attempt to reconnect and register again using the combined helper
  if (!connectAndRegister()) {  // Call the refined helper
    std::cerr << "HeapObserverSocket: Reset failed to connect/register."
              << std::endl;
    // sock_fd_ remains -1
  }
#else
  std::cout << "HeapObserverSocket: Reset() called (socket output disabled)."
            << std::endl;
#endif  // ENABLE_SOCKET_OUTPUT
}