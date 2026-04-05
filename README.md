# 🖥️ Mini Distributed Load Balancer

A fully functional **Distributed Load Balancer** built in C using raw POSIX socket programming. A central server manages multiple worker nodes across machines, dynamically distributes tasks based on real-time load, handles worker crashes with automatic task reassignment, and supports **remote binary execution** — sending compiled programs to worker machines and running them there.

Built as an OS Lab project demonstrating: sockets, `select()`, custom binary protocols, fault tolerance, and real-time load tracking.

---

## 📦 Features

| Feature  | Description |
|---|---|
| **Multi-worker support** | Up to 10 workers connected simultaneously |
| **`select()`-based concurrency** | Single-process server handles all workers + keyboard input |
| **Custom binary protocol** | Structured `Message` struct for all communications |
| **Real load tracking** | Worker measures actual CPU-busy time over last 2 minutes |
| **Smart load balancing** | Assigns to workers under 50% load; falls back to least loaded |
| **Busy worker detection** | `has_task[]` prevents re-assigning to an occupied worker |
| **Fault tolerance** | Detects worker crashes, reassigns task to another worker |
| **`TASK_SLEEP`** | Server sends sleep-N-seconds task to simulate I/O work |
| **`TASK_EXEC`** | Server sends a shell command; worker runs it and returns stdout |
| **`TASK_BINARY`** | Server sends a compiled ELF binary; worker executes it and returns output + timing |
| **Cross-machine support** | Workers connect via LAN/Wi-Fi using command-line IP argument |
| **Reliable TCP transfer** | `recv_all()` handles large binary files across real networks |
| **Execution timing** | Measures and reports binary execution time in milliseconds |

---

## 🗂️ Project Structure

```
.
├── Makefile                    # Build system for compiling binaries
├── demo.c                      # Sample program for binary task testing
├── server_bin                  # Compiled server executable
├── worker_bin                  # Compiled worker executable
├── common/                     # Shared utilities
│   └── network.c               # Reliable TCP data transfer (recv_all)
├── include/                    # Header files with shared definitions
│   ├── binary_handler.h
│   ├── common.h                # Protocol definitions, structs, enums
│   ├── exec_handler.h
│   ├── load_balancer.h
│   ├── load_monitor.h
│   ├── network.h
│   ├── result_handler.h
│   ├── task_dispatcher.h
│   └── worker_manager.h
├── server/                     # Server-side components
│   ├── load_balancer.c         # Load balancing algorithm
│   ├── result_handler.c        # Process worker responses
│   ├── server_main.c           # Main server loop with select()
│   ├── task_dispatcher.c       # Parse and dispatch user commands
│   └── worker_manager.c        # Manage connected workers
└── worker/                     # Worker-side components
    ├── binary_handler.c        # Handle binary task execution
    ├── exec_handler.c          # Handle shell command execution
    ├── load_monitor.c          # Monitor and calculate load
    └── worker_main.c           # Main worker loop
```

---

## 🛠️ Building and Running

### Prerequisites
- GCC compiler
- Linux or WSL environment (POSIX socket support required)
- Standard C library

### Build
```bash
make          # Builds server_bin, worker_bin, and demo_bin
make clean    # Cleans all object files and binaries
```

### Run
1. **Start the server** (on machine 1):
   ```bash
   ./server_bin
   ```

2. **Start workers** (on machine 1 or other machines):
   ```bash
   ./worker_bin <server_ip>
   ```
   Example: `./worker_bin 192.168.1.100`

3. **Send tasks** from server console:
   - `task <N>` - Send sleep task for N seconds
   - `exec <command>` - Execute shell command on worker
   - `bin <path>` - Send compiled binary to worker
   - `run <file.c>` - Compile and send C file to worker

---

## 📋 Recent Updates

### Code Quality Improvements
- Fixed buffer handling issues with proper null-termination
- Added error checking for all network operations
- Corrected typos and improved error messages
- Added proper random number seeding
- Enhanced command validation and error handling
- Resolved unsafe string operations

### Architecture Refactoring
- Modularized codebase into separate server/ and worker/ directories
- Organized header files in include/ directory
- Improved maintainability and team collaboration

---

## 🏗️ Key Design Decisions

### Architecture Choices
1. **Fixed-size arrays instead of dynamic allocation** - No `malloc()` used; simplifies memory management and avoids leaks in a lab project
2. **Select-based multiplexing** - Single-threaded, non-blocking I/O handles multiple workers and stdin concurrently
3. **Asynchronous task reassignment** - When a worker crashes, the server immediately reassigns the task to another available worker
4. **Two-tier load balancing** - Prioritizes workers under 50% load, falls back to least-loaded worker for efficiency
5. **Sliding window load calculation** - Workers track actual CPU busy time over the last 2 minutes for realistic load measurement
6. **Timeout on recv in worker** - 3-second timeout enables periodic load updates even during task execution

### Protocol Design
- **Fixed-size Message struct** - Eliminates need for serialization libraries; both sides cast the same bytes
- **Separate BinaryTask struct** - Handles large binary transfers (up to 64KB) reliably across TCP packet fragmentation
- **Custom enums for message types** - Clear, type-safe communication between server and workers

### Fault Tolerance
- **Connection monitoring** - Server detects worker crashes via `recv()` return values
- **State tracking arrays** - `has_task[]`, `active_tasks[]`, `worker_loads[]` maintain system state
- **Automatic recovery** - Failed tasks are reassigned without user intervention

---

### Architecture
```
┌─────────────────────────────────────┐
│             SERVER                  │
│  - Listens on PORT 8080             │
│  - Tracks worker loads              │
│  - Dispatches tasks via keyboard    │
│  - Handles crashes & reassigns      │
└────────────┬────────────────────────┘
             │ TCP Socket (LAN/Wi-Fi)
    ┌────────┴────────┬─────────────┐
    ▼                 ▼             ▼
┌────────┐      ┌────────┐    ┌────────┐
│Worker 0│      │Worker 1│    │Worker N│
│Machine1│      │Machine2│    │MachineN│
└────────┘      └────────┘    └────────┘
```

### Communication Protocol — `Message` Struct
Every communication uses a fixed-size binary struct sent over TCP:
```c
typedef struct {
    MessageType type;       // MSG_REGISTER / MSG_LOAD_UPDATE / MSG_TASK_ASSIGN / ...
    int worker_id;
    int load_percent;       // Worker's current load (0-100%)
    TaskType task_type;     // TASK_SLEEP / TASK_EXEC / TASK_BINARY
    int task_id;
    int task_arg;           // e.g., sleep duration
    int task_result;
    char command[256];      // For TASK_EXEC: shell command string
    char output[1024];      // For TASK_EXEC: stdout from worker
} Message;
```

### Load Balancing Algorithm
```
find_available_worker():
  1. Skip workers that are already executing a task (has_task[i] == 1)
  2. Return first worker with load < 50%     ← ideal case
  3. If none under 50%, return least-loaded  ← fallback
  4. If no workers connected, return -1
```

### Binary Transfer Protocol (`TASK_BINARY`)
```
Server                          Worker
  │                               │
  │── MSG_TASK_ASSIGN ──────────► │  (control message: "binary task coming")
  │── BinaryTask struct ────────► │  (up to 64KB raw ELF bytes)
  │                               │  writes to /tmp/worker_bin
  │                               │  chmod +x
  │                               │  clock_gettime(start)
  │                               │  popen("/tmp/worker_bin")
  │                               │  clock_gettime(end)
  │◄─ MSG_BINARY_RESULT ──────── │  (control message: "result coming")
  │◄─ BinaryResult struct ─────  │  (output string + execution_ms)
```

---

## 🔧 Prerequisites

- **OS:** Linux or WSL2 (Ubuntu recommended)
- **Compiler:** GCC
- **Network:** All machines on the **same LAN, Wi-Fi network, or Ethernet switch**

```bash
# Check GCC is installed
gcc --version

# Install if missing (Ubuntu/Debian)
sudo apt update && sudo apt install gcc
```

---

## 🚀 Getting Started

### Step 1: Clone the Repository

```bash
git clone https://github.com/Operating-System-laboratory-51-60/Mini-Distributed-System.git
cd Mini-Distributed-System
```

### Step 2: Compile

```bash
# Compile the server
gcc server.c -o server

# Compile the worker
gcc worker.c -o worker

# Compile the demo program (optional, for TASK_BINARY testing)
gcc demo.c -o demo
```

### Step 3: Find Your Server's IP Address

On the **server machine**, run:
```bash
hostname -I
```
This prints all IP addresses. Look for the one that starts with `192.168.` or `10.` — that is your **LAN IP**.

Example output:
```
172.20.176.1 192.168.1.10
```
Use `192.168.1.10` (the LAN IP, not the 172.x WSL internal one).

> **Tip:** You can also run `ip addr show` for detailed interface info. Look for `eth0` or `wlan0`.

### Step 4: Network Requirements

> ⚠️ **All machines MUST be connected to the same network for this to work.**

| Setup | Works? |
|---|---|
| All on same Wi-Fi router | ✅ Yes |
| All connected to same switch | ✅ Yes |
| Server on Wi-Fi, Worker on mobile hotspot | ❌ No |
| Server on university LAN, Worker on home Wi-Fi | ❌ No |
| Both on WSL2 on same laptop | ✅ Yes (use `127.0.0.1`) |

---

## 🖥️ Running the System

### Terminal 1 — Start the Server (on server machine)
```bash
./server
```
Expected output:
```
Waiting for workers to connect...
```

### Terminal 2, 3, 4... — Start Workers (on each worker machine)

**Same machine (testing locally):**
```bash
./worker 127.0.0.1
```

**Different machine (real distributed setup):**
```bash
# Replace 192.168.1.10 with your actual server LAN IP
./worker 192.168.1.10
```

Expected output on worker:
```
Connected to server on port 8080
Registered with server. Waiting for tasks...
Sent load update: 0%.
Sent load update: 0%.
```

Expected output on server:
```
New worker connected. Assigned to slot 0.
Received: Worker 0 registered.
Worker[0] load updated to 0%.
```

---

## 💬 Server Commands

Once workers are connected, type commands in the server terminal:

### `task <seconds>`
Sends a sleep task to the best available worker.
```
task 5
```
Worker sleeps for 5 seconds (simulates I/O-bound work), then sends result back.

```
Dispatched task (sleep 5s) to Worker[0] (load: 0%)

Worker[0] finished task #512. Result = 10.    ← after 5 seconds
```

### `exec <shell command>`
Runs any shell command on the best available worker and returns its stdout.
```
exec whoami
exec uname -a
exec ls -la /tmp
exec cat /proc/cpuinfo | head -10
```
Example output:
```
Dispatched command "whoami" to Worker [0]
=== TASK COMPLETE ===
Worker [0] finished task #712. 
--- Output ---
john
--------------
```

### `bin <path/to/binary>`
Sends a compiled ELF binary to the best worker. The worker executes it and returns the output and timing.
```
bin ./demo
bin /home/user/myprogram
```
The binary is read from the server's disk, transferred byte-by-byte over TCP, saved to `/tmp/worker_bin` on the worker, made executable, and run.

Example output:
```
Sent binary (8472 bytes) to Worker[0]
=== BINARY TASK COMPLETE ===
Worker[0] Task #237 | Time: 3ms
---OUTPUT---
10 + 20 = 30.
--------------------
```

---

## 🧪 Testing Binary Execution (demo.c)

A sample program is included:
```c
// demo.c
#include <stdio.h>
int main() {
    int a = 10, b = 20;
    printf("%d + %d = %d.\n", a, b, a+b);
    return 0;
}
```

Compile and send:
```bash
gcc demo.c -o demo
# In server terminal:
bin ./demo
```

You can also write and send your own programs — any valid Linux ELF binary under 64KB works.

---

## 🔍 Troubleshooting

### Worker can't connect to server
- Verify both machines are on the **same Wi-Fi/LAN**
- Check server IP with `hostname -I` on the server machine
- Check firewall: `sudo ufw allow 8080` or `sudo ufw disable`
- Test connectivity: `ping <server-ip>` from worker machine

### `exec` command returns empty output
- Ensure the command is valid on the worker's machine (it runs there, not on the server)
- Try `exec ls` first to confirm the pipeline works

### `bin` command works locally but not cross-machine
- This was the TCP partial receive bug — fixed by `recv_all()`. Ensure you are using the latest code.
- Ensure binary is under 64KB: `ls -lh ./yourbinary`

### Worker crashes and task is lost
- The server automatically detects disconnection (`recv()` returns 0)
- If another worker is available, the task is **automatically reassigned**
- If no workers are available, server prints an error and exits

---

## 📡 How to Find IP Addresses

### On Linux / WSL2:
```bash
hostname -I               # Quick: lists all IPs
ip addr show              # Detailed: shows all network interfaces
ip addr show eth0         # Specific interface (Ethernet)
ip addr show wlan0        # Specific interface (Wi-Fi)
```

### Understanding the output:
```
2: eth0: <BROADCAST,MULTICAST,UP>
    inet 192.168.1.10/24    ← This is your LAN IP (use this!)
```

### On Windows (if running WSL2):
```powershell
ipconfig
# Look for "IPv4 Address" under Wi-Fi or Ethernet adapter
```

---

## 🏗️ Building Your Own Extension

The codebase is modular — adding a new task type takes 3 steps:

1. **Add to enum in `common.h`:**
```c
typedef enum { TASK_SLEEP, TASK_EXEC, TASK_BINARY, TASK_YOUR_NEW } TaskType;
```

2. **Add server dispatch in `server.c`** (in the keyboard handler):
```c
else if(sscanf(command, "your_cmd %d", &arg) == 1) {
    task.task_type = TASK_YOUR_NEW;
    task.task_arg = arg;
    send(client_sockets[idx], &task, sizeof(Message), 0);
}
```

3. **Add worker handler in `worker.c`** (inside `MSG_TASK_ASSIGN` block):
```c
else if(msg.task_type == TASK_YOUR_NEW) {
    // Do your work here
    // Send MSG_TASK_RESULT back
}
```

---

## 📚 Concepts Demonstrated

| Concept | Where Used |
|---|---|
| TCP Sockets | `socket()`, `bind()`, `listen()`, `accept()`, `connect()` |
| Multiplexed I/O | `select()` + `fd_set` in both server and worker |
| Custom binary protocol | Fixed-size `Message` struct |
| Fault tolerance | `recv() == 0` detection + task reassignment |
| Remote code execution | `popen()` + `fread()` for stdout capture |
| Binary file transfer | `fread(rb)` → TCP → `fwrite(wb)` → `chmod(0755)` |
| TCP stream behaviour | `recv_all()` loop for large data over real networks |
| High-resolution timing | `clock_gettime(CLOCK_MONOTONIC)` |
| Real load calculation | 2-minute sliding window of task history |
| Command-line arguments | `argc`/`argv` for dynamic server IP |

---

## 👨‍💻 Authors

OS Laboratory — Group 51-60

---

## 📄 License

This project is for educational purposes as part of an Operating Systems Laboratory course.
