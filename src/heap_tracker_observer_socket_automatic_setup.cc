// heap_observer_socket_automatic_setup.cc
#include "heap_tracker_dispatcher.h"
#include "heap_tracker_interceptor.h"
#include "heap_tracker_observer_socket.h"
#include "heap_tracker_observer_timeseries_file.h"  

#ifdef ENABLE_SOCKET_OUTPUT 

// Step 1: Create observer first.
static HeapObserverSocket local_heap_observer{
  GetHeapObserverTimeseriesFileAutomaticTrackOptions(),  // Now declared via
                                                         // include
  "127.0.0.1", 9102};                                    // Or configure IP/port

// Step 2: Create dispatcher using the observer created above.
static Dispatcher local_dispatcher{
  &local_heap_observer};  

struct HeapObserverSocketDriver {
  HeapObserverSocketDriver() {

    // Register the dispatcher with the interceptor mechanism.
    // This single call should be sufficient, mirroring the file observer setup.
    ::SetInterceptorDispatcher(
      &local_dispatcher);  
  }
};

#ifdef HEAP_TRACKER_INTERCEPT_INTERPOSITION
// Step 3: Create the driver to setup interceptor dispatcher.
static const HeapObserverSocketDriver
  local_driver;  
#endif

#endif  // ENABLE_SOCKET_OUTPUT
