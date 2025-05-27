from flask import Flask, Response
import os
import glob
import re
import time
import threading

app = Flask(__name__)

HISTOGRAM_BUCKETS = [1024, 10*1024, 100*1024, 1024*1024]

def parse_heap_file(file_path):
    metrics = {
        "allocations_total": 0,
        "deallocations_total": 0,
        "allocated_bytes_total": 0,
        "freed_bytes_total": 0,
        "histogram_count": 0,
        "histogram_sum": 0,
        "histogram_buckets": {str(bucket): 0 for bucket in HISTOGRAM_BUCKETS},
        "+Inf": 0
    }
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                tokens = line.split()
                if len(tokens) < 4:
                    continue  
                event_type = tokens[0]
                try:
                    size = int(tokens[2])
                except ValueError:
                    continue

                if event_type == 'A':
                    metrics["allocations_total"] += 1
                    metrics["allocated_bytes_total"] += size
                    # Update histogram
                    metrics["histogram_count"] += 1
                    metrics["histogram_sum"] += size
                    for bucket in HISTOGRAM_BUCKETS:
                        if size <= bucket:
                            metrics["histogram_buckets"][str(bucket)] += 1
                    metrics["+Inf"] += 1
                elif event_type == 'D':
                    metrics["deallocations_total"] += 1
                    metrics["freed_bytes_total"] += size
    except Exception as e:
        print(f"Error processing file {file_path}: {e}")
    
    metrics["outstanding_allocations"] = metrics["allocations_total"] - metrics["deallocations_total"]
    metrics["outstanding_bytes"] = metrics["allocated_bytes_total"] - metrics["freed_bytes_total"]
    return metrics

def aggregate_all_heap_data(directory):
    results = {}
    file_pattern = os.path.join(directory, "*.txt")
    for file_path in glob.glob(file_pattern):
        base = os.path.basename(file_path)
        pid, ext = os.path.splitext(base)
        results[pid] = parse_heap_file(file_path)
    return results

def format_prometheus_metrics(pid, metrics):

    lines = []
    # Core metrics (Gauge)
    core_metrics = [
        ("heap_outstanding_allocations", "Current count of unfreed allocations", "gauge", metrics["outstanding_allocations"]),
        ("heap_outstanding_bytes", "Current total of allocated but unfreed bytes", "gauge", metrics["outstanding_bytes"])
    ]
    for name, help_text, mtype, value in core_metrics:
        lines.append(f"# HELP {name} {help_text}")
        lines.append(f"# TYPE {name} {mtype}")
        lines.append(f'{name}{{pid="{pid}"}} {value}')
    
    # Additional metrics (Counters)
    additional_counters = [
        ("heap_allocation_rate", "Total allocation events (counter)", "counter", metrics["allocations_total"]),
        ("heap_deallocation_rate", "Total deallocation events (counter)", "counter", metrics["deallocations_total"])
    ]
    for name, help_text, mtype, value in additional_counters:
        lines.append(f"# HELP {name} {help_text}")
        lines.append(f"# TYPE {name} {mtype}")
        lines.append(f'{name}{{pid="{pid}"}} {value}')
    
    # Histogram: allocation size distribution.
    hist_metric = "heap_allocation_size_histogram"
    lines.append(f"# HELP {hist_metric} Distribution of allocation sizes")
    lines.append(f"# TYPE {hist_metric} histogram")
    cumulative = 0
    for bucket in HISTOGRAM_BUCKETS:
        cumulative += metrics["histogram_buckets"][str(bucket)]
        lines.append(f'{hist_metric}_bucket{{pid="{pid}",le="{bucket}"}} {cumulative}')
    cumulative += metrics["+Inf"]
    lines.append(f'{hist_metric}_bucket{{pid="{pid}",le="+Inf"}} {cumulative}')
    lines.append(f'{hist_metric}_sum{{pid="{pid}"}} {metrics["histogram_sum"]}')
    lines.append(f'{hist_metric}_count{{pid="{pid}"}} {metrics["histogram_count"]}')
    
    return "\n".join(lines)

aggregated_metrics = {}
heap_dir = "/home/vedang/Documents/heap_tracker/build/timeseries_output"

def update_metrics():
    global aggregated_metrics
    while True:
        if os.path.isdir(heap_dir):
            aggregated_metrics = aggregate_all_heap_data(heap_dir)
        else:
            print("Heap tracker dump directory not found.")
            aggregated_metrics = {}
        time.sleep(5)  # Update every 5 seconds.

@app.route('/metrics')
def metrics_endpoint():
    """
    HTTP endpoint to expose Prometheus metrics.
    """
    output_lines = []
    for pid, metrics in aggregated_metrics.items():
        output_lines.append(format_prometheus_metrics(pid, metrics))
    output = "\n\n".join(output_lines)
    return Response(output, mimetype="text/plain")

@app.route('/healthz')
def healthz():
    return Response("OK", mimetype="text/plain")

if __name__ == '__main__':
    metrics_thread = threading.Thread(target=update_metrics, daemon=True)
    metrics_thread.start()
    app.run(host='0.0.0.0', port=9101)