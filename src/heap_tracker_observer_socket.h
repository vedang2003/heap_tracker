// heap_observer_socket.h
#ifndef HEAP_OBSERVER_SOCKET_H
#define HEAP_OBSERVER_SOCKET_H

#include "heap_tracker_observer_interface.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class HeapObserverSocket : public AbstractObserver {
 public:
  HeapObserverSocket(HeapTrackOptions heap_track_options,
                     const char * server_ip = "127.0.0.1",
                     int server_port        = 9102);
  ~HeapObserverSocket() override;

  HeapTrackOptions GetHeapTrackOptions() const override;
  void OnAlloc(AllocCallbackInfo const & alloc_cb_info) override;
  void OnFree(FreeCallbackInfo const & free_cb_info) override;
  void OnComplete() override;
  void Dump() const override;
  void Reset() override;

 private:
#ifdef ENABLE_SOCKET_OUTPUT
  void SendDataToSocket(const json & data);
  bool ConnectToSocket();
#endif

 private:
  const HeapTrackOptions heap_track_options_;
#ifdef ENABLE_SOCKET_OUTPUT
  std::string server_ip_;
  int server_port_;
  int sock_fd_{-1};
#endif
};

#endif  // HEAP_OBSERVER_SOCKET_H