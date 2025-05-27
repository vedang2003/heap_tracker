from flask import Flask, Response
import json
import socket
import threading
import time
import os
import logging 

app = Flask(__name__)

# Configure basic logging
logging.basicConfig(level=logging.INFO,
                    format='%(levelname)s - %(message)s') # CHANGED FORMAT

BUFFER_SIZE = 4096

# --- Global Structures ---
metrics = {} 
connection_pids = {} 
metrics_lock = threading.Lock() 
# ------------------------

def init_pid_metrics(pid_str):
    """ Initialize metrics structure for a new PID string """
    return {
        "allocations_total": 0,
        "deallocations_total": 0,
        "allocated_bytes_total": 0,
        "freed_bytes_total": 0,
        "outstanding_allocations": 0,
        "outstanding_bytes": 0,
        "active_allocations": {} 
    }

def process_event(event, pid):
    """ Process incoming JSON event for a known PID and update global metrics """
    pid_str = str(pid) # Ensure consistent key type
    size = event.get("size")
    address = event.get("address")
    event_type = event.get("event_type")

    with metrics_lock:
        if pid_str not in metrics:
            logging.info(f"First event for PID {pid_str}, initializing metrics.")
            metrics[pid_str] = init_pid_metrics(pid_str)

        pid_metrics = metrics[pid_str]

        if event_type == "A":
            if size is None or address is None:
                logging.warning(f"Incomplete allocation event for PID {pid_str}. Event: {event}")
                return

            pid_metrics["allocations_total"] += 1
            pid_metrics["allocated_bytes_total"] += size
            pid_metrics["outstanding_allocations"] += 1
            pid_metrics["outstanding_bytes"] += size
            pid_metrics["active_allocations"][address] = size # Store allocation size

        elif event_type == "D":
            if address is None:
                 logging.warning(f"Deallocation event for PID {pid_str} missing 'address'. Event: {event}")
                 return

            pid_metrics["deallocations_total"] += 1
            pid_metrics["outstanding_allocations"] -= 1

            # Look up original size to update outstanding bytes accurately
            original_size = pid_metrics["active_allocations"].pop(address, None)
            if original_size is not None:
                pid_metrics["outstanding_bytes"] -= original_size
                pid_metrics["freed_bytes_total"] += original_size
            else:
                logging.warning(f"Free event for PID {pid_str} for untracked address {address}. Cannot update outstanding_bytes accurately.")

        if pid_metrics["outstanding_allocations"] < 0:
           logging.warning(f"PID {pid_str} outstanding allocations went negative ({pid_metrics['outstanding_allocations']}). Resetting to 0.")
           pid_metrics["outstanding_allocations"] = 0
        if pid_metrics["outstanding_bytes"] < 0:
           logging.warning(f"PID {pid_str} outstanding bytes went negative ({pid_metrics['outstanding_bytes']}). Resetting to 0.")
           pid_metrics["outstanding_bytes"] = 0

def format_prometheus_metrics(pid, data):
    """ Format a single PID's metrics into Prometheus format """
    lines = []
    # Gauges
    lines.append(f"# TYPE heap_outstanding_allocations gauge")
    lines.append(f'heap_outstanding_allocations{{pid="{pid}"}} {data["outstanding_allocations"]}')
    lines.append(f"# TYPE heap_outstanding_bytes gauge")
    lines.append(f'heap_outstanding_bytes{{pid="{pid}"}} {data["outstanding_bytes"]}')

    # Counters
    lines.append(f"# TYPE heap_allocations_total counter")
    lines.append(f'heap_allocations_total{{pid="{pid}"}} {data["allocations_total"]}')
    lines.append(f"# TYPE heap_deallocations_total counter")
    lines.append(f'heap_deallocations_total{{pid="{pid}"}} {data["deallocations_total"]}')
    lines.append(f"# TYPE heap_allocated_bytes_total counter")
    lines.append(f'heap_allocated_bytes_total{{pid="{pid}"}} {data["allocated_bytes_total"]}')
    lines.append(f"# TYPE heap_freed_bytes_total counter")
    lines.append(f'heap_freed_bytes_total{{pid="{pid}"}} {data["freed_bytes_total"]}')

    return "\n".join(lines)

@app.route('/metrics')
def metrics_endpoint():
    """ Expose aggregated metrics in Prometheus format """
    output_lines = []
    with metrics_lock:
        metrics_items = list(metrics.items())

    for pid, data in metrics_items:
        try:
            output_lines.append(format_prometheus_metrics(pid, data))
        except Exception as e:
            logging.error(f"Error formatting metrics for PID {pid}: {e}")
            output_lines.append(f"# ERROR formatting metrics for PID {pid}")

    return Response("\n\n".join(output_lines) + "\n", mimetype="text/plain")

@app.route('/healthz')
def healthz():
    """ Basic health check endpoint """
    return Response("OK", mimetype="text/plain")

def handle_connection(client_socket, addr):
    """ Handle a single client connection: register PID, process events """
    registered_pid = None
    buffer = ""
    logging.info(f"Connection accepted from {addr}")

    try:
        # --- Registration Phase ---
        try:
            client_socket.settimeout(5.0) # 5 second registration timeout
            reg_data = client_socket.recv(BUFFER_SIZE)
            client_socket.settimeout(None) # Disable timeout after registration

            if not reg_data:
                logging.warning(f"Connection closed by {addr} before registration.")
                return

            try:
                reg_line = reg_data.decode('utf-8').strip()
                if not reg_line: raise ValueError("Empty registration line")
                reg_event = json.loads(reg_line)

                if (isinstance(reg_event, dict) and
                        reg_event.get("type") == "register" and
                        "pid" in reg_event):
                    registered_pid = str(reg_event["pid"]) # Store PID as string
                    with metrics_lock:
                        connection_pids[client_socket] = registered_pid
                    logging.info(f"Registered PID {registered_pid} for connection {addr}")
                else:
                    raise ValueError(f"Invalid registration message format: {reg_line}")

            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as e:
                logging.error(f"Registration failed for {addr}: {e} - Data: {reg_data!r}")
                return # Close connection

        except socket.timeout:
             logging.error(f"Registration timed out for {addr}.")
             return
        except Exception as e:
             logging.error(f"Error during registration phase for {addr}: {e}")
             return

        # --- Event Processing Phase ---
        while True:
            try:
                data = client_socket.recv(BUFFER_SIZE)
                if not data:
                    logging.info(f"Connection closed by {addr} (PID: {registered_pid})")
                    break
                try:
                    buffer += data.decode('utf-8')
                except UnicodeDecodeError:
                    logging.error(f"Unicode decode error from PID {registered_pid} ({addr}). Clearing buffer.")
                    buffer = "" # Clear potentially corrupt buffer
                    continue

                while "\n" in buffer: # Process newline-delimited JSON
                    line, buffer = buffer.split("\n", 1)
                    if line.strip():
                        try:
                            event = json.loads(line)
                            if isinstance(event, dict) and "event_type" in event:
                                process_event(event, registered_pid) # Use registered PID
                            else:
                                logging.error(f"Invalid event structure from PID {registered_pid} ({addr}): {line}")
                        except json.JSONDecodeError:
                            logging.error(f"Invalid JSON received from PID {registered_pid} ({addr}): {line}")
                        except Exception as e:
                            logging.exception(f"Error processing event from PID {registered_pid} ({addr}): {line} - {e}")

            except ConnectionResetError:
                logging.warning(f"Connection reset by {addr} (PID: {registered_pid})")
                break
            except Exception as e:
                logging.error(f"Error receiving data from {addr} (PID: {registered_pid}): {e}")
                break

    finally:
        logging.info(f"Cleaning up connection from {addr} (PID: {registered_pid})")
        with metrics_lock:
            connection_pids.pop(client_socket, None) # Remove connection mapping
        client_socket.close()

def socket_listener():
    """ Socket server thread to accept connections and spawn handlers """
    HOST = '127.0.0.1' # Listen only on localhost by default
    PORT = 9102        # Port for C++ client connections
    
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server_socket.bind((HOST, PORT))
        server_socket.listen(10)
        logging.info(f"Socket listener started on {HOST}:{PORT}")
    except Exception as e:
        logging.error(f"Failed to start socket listener on {HOST}:{PORT}: {e}")
        return

    while True:
        try:
            client_socket, addr = server_socket.accept()
            handler_thread = threading.Thread(target=handle_connection,
                                              args=(client_socket, addr),
                                              daemon=True)
            handler_thread.name = f"Handler-{addr}"
            handler_thread.start()
        except Exception as e:
            logging.error(f"Error accepting connection: {e}")
            time.sleep(1)

if __name__ == '__main__':
    listener_thread = threading.Thread(target=socket_listener, daemon=True)
    listener_thread.name = "SocketListenerThread"
    listener_thread.start()

    # Start Flask HTTP server for /metrics endpoint
    logging.info("Starting Flask HTTP server on 0.0.0.0:9101") # Port for Prometheus scraping
    app.run(host='0.0.0.0', port=9101, threaded=True)