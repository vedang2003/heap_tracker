// Copyright 2018, Arun Saha <arunksaha@gmail.com>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "heap_tracker_dispatcher.h"
#include "heap_tracker_interceptor.h"
#include "heap_tracker_observer_timeseries_file.h"

static std::string
get_output_filename() {
  // Define the output directory.
  std::string output_dir = "timeseries_output";

  // Check if the directory exists; if not, create it.
  struct stat st;
  if (stat(output_dir.c_str(), &st) != 0) {
    if (mkdir(output_dir.c_str(), 0700) != 0) {
      std::cerr << "Error creating directory " << output_dir << ": "
                << strerror(errno) << std::endl;
      exit(1);
    }
  }
  else if (!S_ISDIR(st.st_mode)) {
    std::cerr << output_dir << " exists but is not a directory." << std::endl;
    exit(1);
  }

  // Get the current process ID.
  pid_t pid = getpid();

  // Construct the output filename: output/<pid>.txt
  std::ostringstream oss;
  oss << output_dir << "/" << pid << ".txt";
  return oss.str();
}

// Store the filename in this const output_filename
static std::string output_filename_str    = get_output_filename();
static char const * const output_filename = output_filename_str.c_str();

// The following portion of the file is like a "driver",
// it helps an application to use HeapObserverTimeseriesFile
// in a off-the-shelf ready-to-use way. Essentially, it
// creates few a static variables and thereby initialize
// few data structures.

// Step 1: Create observer.
static HeapObserverTimeseriesFile local_heap_observer{
  GetHeapObserverTimeseriesFileAutomaticTrackOptions(), output_filename};

// Step 2: Create dispatcher using the above observer.
static Dispatcher local_dispatcher{&local_heap_observer};

HeapObserverTimeseriesFileDriver::HeapObserverTimeseriesFileDriver() {
  SetInterceptorDispatcher(&local_dispatcher);
}

#ifdef HEAP_TRACKER_INTERCEPT_INTERPOSITION
// Step 3: Create the driver to setup interceptor dispatcher.
// For tcmalloc, this step is done by the test code.
static const HeapObserverTimeseriesFileDriver local_driver;
#endif
