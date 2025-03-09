// heap_observer_socket.cc
#include <cassert>
#include <iostream>
#include "heap_observer_socket.h"

#ifdef ENABLE_SOCKET_OUTPUT
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>  // For strerror

namespace {  // Anonymous namespace for helper functions

bool
connectToSocket(int & sock_fd, const std::string & server_ip, int server_port) {
  if (sock_fd != -1) {
    close(sock_fd);
    sock_fd = -1;
  }
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd == -1) {
    std::cerr << "Could not create socket: " << strerror(errno) << std::endl;
    return false;
  }

  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port   = htons(server_port);

  if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address/ Address not supported: " << strerror(errno)
              << std::endl;
    close(sock_fd);
    sock_fd = -1;
    return false;
  }

  if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Connection Failed: " << strerror(errno) << std::endl;
    close(sock_fd);
    sock_fd = -1;
    return false;
  }
  return true;
}

bool
sendDataToSocket(int sock_fd, const json & data) {
  if (sock_fd == -1) {
    return false;  // Or throw an exception, depending on your error handling
  }

  std::string json_str = data.dump();
  ssize_t bytes_sent   = send(sock_fd, json_str.c_str(), json_str.length(), 0);
  if (bytes_sent == -1) {
    std::cerr << "Error sending data to socket: " << strerror(errno)
              << std::endl;
    return false;  // Or throw an exception
  }
  return true;
}

}  // anonymous namespace

#endif  // ENABLE_SOCKET_OUTPUT


HeapObserverSocket::HeapObserverSocket(HeapTrackOptions heap_track_options,
                                       const char * server_ip, int server_port)
    : heap_track_options_(heap_track_options)
#ifdef ENABLE_SOCKET_OUTPUT
      ,
      server_ip_(server_ip),
      server_port_(server_port)
#endif
{
#ifdef ENABLE_SOCKET_OUTPUT
  if (!connectToSocket(sock_fd_, server_ip_, server_port_)) {
    std::cerr << "Failed to connect to socket.  Socket output disabled."
              << std::endl;
  }
#endif
}

HeapObserverSocket::~HeapObserverSocket() {
#ifdef ENABLE_SOCKET_OUTPUT
  if (sock_fd_ != -1) {
    close(sock_fd_);
  }
#endif
}

HeapTrackOptions
HeapObserverSocket::GetHeapTrackOptions() const {
  return heap_track_options_;
}

void
HeapObserverSocket::OnAlloc(AllocCallbackInfo const & alloc_cb_info) {
#ifdef ENABLE_SOCKET_OUTPUT
  json j;
  j["event_type"] = "A";
  j["size"]       = alloc_cb_info.size_;
  j["address"]    = alloc_cb_info.address_;
  j["timepoint"]  = alloc_cb_info.timepoint_;
  if (heap_track_options_.track_callstack) {
    j["callstack"] = alloc_cb_info.callstack_;
  }
  if (!sendDataToSocket(sock_fd_, j)) {
    // Handle send failure (e.g., reconnect, log, etc.)
    close(sock_fd_);
    sock_fd_ = -1;
    connectToSocket(sock_fd_, server_ip_, server_port_);  // try to reconnect
  }
#endif
}

void
HeapObserverSocket::OnFree(FreeCallbackInfo const & free_cb_info) {
#ifdef ENABLE_SOCKET_OUTPUT
  json j;
  j["event_type"] = "D";
  j["address"]    = free_cb_info.address_;
  j["timepoint"]  = free_cb_info.timepoint_;
  if (!sendDataToSocket(sock_fd_, j)) {
    // Handle send failure
    close(sock_fd_);
    sock_fd_ = -1;
    connectToSocket(sock_fd_, server_ip_, server_port_);
  }
#endif
}

void
HeapObserverSocket::OnComplete() {
#ifdef ENABLE_SOCKET_OUTPUT
  if (sock_fd_ != -1) {
    close(sock_fd_);
    sock_fd_ = -1;
  }
#endif
}

void
HeapObserverSocket::Dump() const {
  // Not relevant for a socket-only observer
}

void
HeapObserverSocket::Reset() {
  //  Potentially close and reconnect here for long-lived processes
}