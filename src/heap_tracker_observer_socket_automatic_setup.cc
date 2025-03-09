// heap_observer_socket_automatic_setup.cc
#include "heap_tracker_observer_socket.h"
#include "heap_tracker_dispatcher.h"
#include "heap_tracker_interceptor.h"

#ifdef ENABLE_SOCKET_OUTPUT  // Only include this whole file if enabled

static Dispatcher local_dispatcher;  // Dispatcher is same

static HeapObserverSocket local_heap_observer{
  GetHeapObserverTimeseriesFileAutomaticTrackOptions(), "127.0.0.1",
  9102};  // Or configure IP/port

void
SetInterceptorDispatcher() {
  SetInterceptorDispatcher(&local_dispatcher);  // Set in intercepter
}

struct HeapObserverSocketDriver {  // Driver Class same as file observer
  HeapObserverSocketDriver() {
    local_dispatcher.SetObserver(&local_heap_observer);
    local_heap_observer.SetInterceptorDispatcher(&local_dispatcher);
    SetInterceptorDispatcher();
  }
};

#ifdef HEAP_TRACKER_INTERCEPT_INTERPOSITION
static const HeapObserverSocketDriver
  local_driver;  // creating the driver object
#endif

#endif  // ENABLE_SOCKET_OUTPUT