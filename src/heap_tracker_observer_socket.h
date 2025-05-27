// heap_tracker_observer_socket.h
#ifndef HEAP_OBSERVER_SOCKET_H
#define HEAP_OBSERVER_SOCKET_H

#include "heap_tracker_observer_interface.h"  // Includes heap_tracker_types.h

#ifdef ENABLE_SOCKET_OUTPUT  
#include <string>            
#include <nlohmann/json.hpp>
using json = nlohmann::json;  
#endif

class HeapObserverSocket : public AbstractObserver {
 public:
  // Constructor now takes server details
  HeapObserverSocket(HeapTrackOptions heap_track_options,
                     const char * server_ip = "127.0.0.1",
                     int server_port        = 9102);

  ~HeapObserverSocket() override;

  // Disable copy/move semantics (Good practice)
  HeapObserverSocket(const HeapObserverSocket &)             = delete;
  HeapObserverSocket & operator=(const HeapObserverSocket &) = delete;
  HeapObserverSocket(HeapObserverSocket &&)                  = delete;
  HeapObserverSocket & operator=(HeapObserverSocket &&)      = delete;

  // --- Public Interface Methods ---
  HeapTrackOptions GetHeapTrackOptions() const override;
  void OnAlloc(AllocCallbackInfo const & alloc_cb_info) override;
  void OnFree(FreeCallbackInfo const & free_cb_info) override;
  void OnComplete() override;  
  void Dump() const override;       
  void Reset() override;  

 private:
  // --- Private Member Variables ---
  HeapTrackOptions const heap_track_options_;  // Store options

#ifdef ENABLE_SOCKET_OUTPUT
  std::string const server_ip_;  // Store server IP
  int const server_port_;        // Store server port
  int sock_fd_{-1};              // Socket file descriptor (-1 if not connected)

  // --- Private Helper Methods (Socket Specific) ---

  // Connects TCP socket AND sends initial PID registration message.
  // Returns true if connection and registration succeeded, false otherwise.
  bool connectAndRegister();

  // Sends the given JSON data object over the socket.
  // Handles errors, closes socket and sets sock_fd_ = -1 on failure.
  void SendDataToSocket(const json & data);

#endif  // ENABLE_SOCKET_OUTPUT
};

#endif  // HEAP_OBSERVER_SOCKET_H