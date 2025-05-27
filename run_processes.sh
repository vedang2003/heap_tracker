#!/bin/bash

# Check if the number of times to run the processes is provided
if [ -z "$1" ]; then
  echo "Usage: $0 <number_of_times>"
  exit 1
fi

n=$1

executable="/home/vedang/Documents/Sample/a.out"
shared_lib="src/libheap_tracker_observer_timeseries_file_interposition.so"

pids=()

function cleanup {
  echo "Terminating all processes..."
  for pid in "${pids[@]}"; do
    echo "Terminating process $pid..."
    kill -SIGINT "$pid"
  done
  wait
  echo "All processes have been terminated."
  exit 0
}

trap cleanup SIGINT

# Run the processes for n number of times
for ((i=1; i<=n; i++)); do
  echo "Running process $i..."
  LD_PRELOAD=$shared_lib $executable &
  pids+=($!)
done

# Wait for all background processes to finish
wait

echo "All processes have completed."