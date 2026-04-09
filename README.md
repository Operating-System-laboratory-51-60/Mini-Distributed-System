# 🌐 P2P Distributed Computing Mesh

A **Peer-to-Peer (P2P) distributed computing system** that removes the single point of failure by using a full mesh topology. Every machine acts as both worker and coordinator, discovering peers automatically, balancing load dynamically, and running tasks concurrently using `fork()`.

**Key innovation**: No dedicated server — every peer is equal.

---

## 🎯 What This System Solves

### Old problems from legacy versions
Older versions used a central server and separate worker roles. That design had major drawbacks:
- **Central server SPOF**: if the server crashed, the entire system failed.
- **Separate server and worker binaries**: required more deployment effort.
- **Static topology**: peers had to be configured before startup.
- **Single-task workers**: limited concurrency and throughput.
- **Weak result routing**: task results could be lost or misdirected.

### What the new mesh fixes
- **No central server**: the mesh stays alive when any peer fails.
- **Equal peers**: one binary runs on every machine.
- **Dynamic join**: peers can join at runtime.
- **Full mesh**: every peer knows every other peer.
- **Concurrent execution**: multiple tasks run in child processes.
- **Reliable routing**: tasks and results return correctly to the origin.
- **Robust networking**: TCP fragmentation and large payloads are handled safely.

---

## 🚀 What’s New and Improved

| Feature | Benefit |
|---|---|
| **🔗 Full P2P Mesh** | No single point of failure; all peers share responsibility |
| **🧭 UDP discovery** | Automatically finds peers on the local network |
| **⚖️ Distributed load balancing** | Uses live peer load to choose the best executor |
| **🌀 Local task queue** | Prevents task loss when the mesh is busy |
| **👨‍👩‍👧‍👦 Single peer binary** | Same executable on every machine |
| **🧪 Fork-based concurrency** | Run many tasks in parallel without blocking |
| **🛠️ Reliable protocol** | Safe transfer of control messages and binary payloads |
| **📊 Live monitoring** | Rich status display with peer health and queue information |

---

## 🏗️ System Architecture

### Mesh topology
```
Peer A ─── Peer B ─── Peer C
  │     ╲       │        │
  │      ╲      │        │
  │       ╲     │        │
  └────── Peer D ─────── Peer E
```

### Main components
- **`mesh_main.c`** — main event loop, CLI, peer connection manager
- **`peer_manager.c`** — discovery, join, and mesh formation
- **`process_manager.c`** — task execution and child process handling
- **`task_queue.c`** — local queue and retry logic
- **`mesh_monitor.c`** — load broadcasts, health checks, and task routing
- **`logger.c`** — event and orphaned result logging
- **`network.c`** — reliable TCP send/receive helpers

### Key shared types
- **`WorkerState`** — peer state, queue state, and runtime metrics
- **`PeerInfo`** — connected peer status and load
- **`Task`** — task metadata and routing information
- **`Message`** — routed P2P protocol packet

---

## 📦 Project Structure

```
.
├── mesh_bin                    # Compiled P2P mesh executable
├── peers.conf                  # Optional peer startup peer list
├── events.log                  # Mesh event history
├── orphaned_results.log        # Crashed-peer results log
├── Makefile                    # Build system
├── README.md                   # Project documentation
├── code_documentation.md       # Detailed code explanations
├── ppt_outline.md              # Presentation outline
├── viva_questions.md           # Viva preparation notes
├── common/                     # Shared utilities
│   ├── logger.c                # Event logging
│   ├── network.c               # Reliable TCP helpers
│   └── peer_manager.c          # Peer discovery + mesh membership
├── include/                    # Shared headers and protocol definitions
│   └── common.h                # Enums, structs, constants
└── mesh/                       # P2P mesh implementation
    ├── mesh_main.c            # Main event loop and CLI
    ├── mesh_monitor.c         # Monitoring and routing
    ├── process_manager.c      # Fork-based execution
    └── task_queue.c           # Local queue management
```

> Legacy `server/` and `worker/` folders remain in the repository for archive, but they are not used by the current `make` build.

---

## 🛠️ Build and Run Guide

### Prerequisites
- GCC compiler
- Linux or WSL2 environment
- POSIX socket support

### Build the project
```bash
make
```

### Clean build artifacts
```bash
make clean
```

### Starting the First Peer
The first peer is the seed node. Start it with your machine's IP and chosen port:

```bash
./mesh_bin -i 192.168.1.10 -p 8080
```
- `-i` — this peer's LAN IP address
- `-p` — port to listen on (use the same port on all machines)

### Joining the Mesh — Other Machines
Use the `-P` flag to connect to the seed node at startup. This is the **recommended and most reliable** method:

```bash
./mesh_bin -i <YOUR_IP> -p 8080 -P 192.168.1.10:8080
```

- `-P SEED_IP:PORT` — connects to an already-running peer
- Once you connect to **one** peer, the **Gossip Protocol** automatically propagates your address to the entire mesh
- **No manual file editing required** — the mesh self-configures

> ⚠️ **Why `-P` and not UDP discover?** `discover` uses UDP broadcast which is blocked by most Wi-Fi hotspots (AP isolation) and WSL2 NAT boundaries. Always use `-P` for cross-machine setups.

### Tested Configuration
Successfully tested across **4 physical Linux machines** on a shared Wi-Fi hotspot using `-P` bootstrap. Task delegation, load balancing, fault injection (peer crash + rejoin), and result routing all verified.

### Optional `peers.conf`
Alternatively, list known peers in `peers.conf` (one entry per line) — they are auto-connected at startup:
```
192.168.1.10:8080
192.168.1.11:8080
192.168.1.12:8080
```

### Networking Ports
The mesh uses two distinct ports simultaneously:
- **TCP Port 8080** for guaranteed peer-to-peer data transfer (tasks, results, loads).
- **UDP Port 8081** exclusively for LAN discovery broadcasts.
*Separating these transports ensures heavy task transfers never block new peers from discovering the mesh.*


---

## 🎛️ Command Reference

| Command | Description |
|---|---|
| `status` | Show the live mesh status dashboard |
| `task <n>` | Submit a sleep task of `n` seconds to the mesh |
| `exec <cmd>` | Run a shell command on the best available peer |
| `discover` | UDP broadcast to find peers on the LAN |
| `peers` | List all connected peers with status and load |
| `load` | Show this node's current load percentage |
| `queue` | Show tasks queued locally waiting for a free peer |
| `help` | Show the help menu |
| `quit` / `q` | Exit cleanly and disconnect from the mesh |

---

## 📊 Visual Status Dashboard

A clean and fully aligned ASCII dashboard example:

```
╔══════════════════════════════════════════╗
║         MESH STATUS DASHBOARD            ║
╚══════════════════════════════════════════╝

  Node   : 192.168.1.10:8080
  Load   : [====------] 45%
  Tasks  : 2 active / 10 max
  Peers  : 3 connected / 4 total
  Queue  : 0 tasks   Results: 1

┌─ PEER STATUS ──────────────────────────────┐
│  > 192.168.1.11:8080   [ALIVE]  Load: 15%  Queue: 0
│  > 192.168.1.12:8080   [ALIVE]  Load: 30%  Queue: 0
│  > 192.168.1.13:8080   [ALIVE]  Load: 25%  Queue: 0
└──────────────────────────────────────────────┘
```

---

## 🧠 What Changed from the Old Architecture

### Problems solved by the new code
- **Central server failure**: eliminated by removing the dedicated server role.
- **Separate server/worker binaries**: replaced with one peer binary.
- **Static topology**: peers can now join dynamically.
- **Limited task concurrency**: fixed by fork-based execution.
- **Incomplete result routing**: solved with source-aware P2P messaging.
- **Fragile large transfers**: fixed with reliable TCP send/receive.

### Why this design is better
- Easier deployment: one binary on every machine.
- Much stronger resilience: one peer crash does not kill the mesh.
- Better scalability: peers can be added or removed anytime.
- Clearer monitoring: live view of peer health, queue, and load.

---

## 🔧 Technical Highlights

### P2P Mesh Formation
- Uses **manual join** and **UDP discovery**.
- Builds a **full mesh** so each peer connects directly to every other peer.
- Supports **dynamic membership** with runtime peer addition.

### Distributed Load Balancing
- Each peer broadcasts its load regularly.
- The mesh chooses the best available peer for each task.
- Tasks queue locally when no immediate peer capacity exists.

### Fault Tolerance & Stability
- Detects failed peers through TCP and heartbeat monitoring.
- Removes dead peers automatically from the mesh.
- Reassigns tasks when a peer disappears.
- **Discovery Storm Prevention**: Explicitly ignores broadcasts from already-connected peers to prevent TCP connection tearing.
- **Payload Reliability**: Uses `send_all` and `recv_all` helpers to guarantee atomic delivery of large binary payloads and task results without fragmentation.

### Multi-Process Execution
- Uses `fork()` so task execution does not block the mesh.
- Keeps the main peer responsive while child processes work.
- Aggressive file descriptor (FD) scrubbing prevents "Phantom Socket Leaks".
- Logs orphaned task results if a peer crashes mid-execution.
---

## 🖥️ OS Concepts & System-Level Programming

This project is a **comprehensive showcase of core Operating Systems concepts** applied in real, running production code. The table below maps every concept to the actual function calls used in this codebase.

### 1. Process Management

| Concept | Syscall / Function | Where Used |
|---|---|---|
| Process creation | `fork()` | `process_manager.c` — spawns a child process per task |
| Child reaping | `waitpid(WNOHANG)` | `process_manager.c` — non-blocking SIGCHLD handler prevents zombies |
| Program execution | `popen()` / `pclose()` | Child process runs shell commands or sleep tasks |
| Process ID lookup | `getpid()` | Tracking active child PIDs in `child_pids[]` array |
| Clean exit | `exit(0)` | Child exits after task completion |

**Key insight:** `fork()` creates an exact copy of the parent process. The child inherits all file descriptors — including peer sockets — so we run a **FD scrubbing loop** immediately after `fork()` (closing FDs 3–1024) to prevent the child from accidentally keeping peer TCP connections open.

---

### 2. I/O Multiplexing — `select()`

The entire mesh event loop runs on a single thread using `select()`:

```c
fd_set read_fds;
FD_ZERO(&read_fds);
FD_SET(listen_sock, &read_fds);       // new TCP connections
FD_SET(STDIN_FILENO, &read_fds);      // user keyboard input
FD_SET(udp_discovery_sock, &read_fds); // UDP peer broadcasts
for (int i = 0; i < peer_count; i++)
    FD_SET(peers[i].socket_fd, &read_fds); // messages from peers

struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
```

- One thread handles **all concurrent I/O** without blocking
- `tv` timeout triggers periodic maintenance (health checks, queue drain, load broadcast)
- `FD_ISSET()` tells us exactly which socket has data ready

---

### 3. Socket Programming

| Socket Operation | Syscall | Protocol | Purpose |
|---|---|---|---|
| Create socket | `socket(AF_INET, SOCK_STREAM, 0)` | TCP | Peer connections |
| Create socket | `socket(AF_INET, SOCK_DGRAM, 0)` | UDP | Discovery broadcast |
| Bind to port | `bind()` | Both | Reserve local port |
| Listen for connections | `listen()` | TCP | Mark as server socket |
| Accept connection | `accept()` | TCP | New peer joins mesh |
| Initiate connection | `connect()` | TCP | Join a peer |
| Send data (reliable) | `send_all()` → `send()` loop | TCP | Task/result routing |
| Receive data (reliable) | `recv_all()` → `recv()` loop | TCP | Read full Message struct |
| UDP broadcast | `sendto()` | UDP | Discovery pulse |
| UDP receive | `recvfrom()` | UDP | Receive peer announcements |

---

### 4. File Descriptors & Non-Blocking I/O

```c
// Set socket to non-blocking for timed connect()
int flags = fcntl(sockfd, F_GETFL, 0);
fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

// After connect(), use select() to wait for writability (= connection complete)
fd_set wset;
FD_SET(sockfd, &wset);
select(sockfd + 1, NULL, &wset, NULL, &timeout);

// Check if connect succeeded
getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

// Restore blocking mode
fcntl(sockfd, F_SETFL, flags);
```

This allows `connect()` to timeout gracefully instead of blocking the event loop for 30+ seconds on a dead peer.

---

### 5. Signal Handling

| Signal | Handler | Purpose |
|---|---|---|
| `SIGCHLD` | Custom handler | Reap zombie child processes immediately using `waitpid(WNOHANG)` |
| `SIGPIPE` | `SIG_IGN` | Ignore broken pipe on dead peer sockets — prevents crash |

---

### 6. Socket Options (`setsockopt`)

```c
// Allow port reuse after crash restart
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// Enable UDP broadcast
setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

// Set receive timeout (200ms) to prevent recv_all freezing the event loop
struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

---

### 7. File I/O & Logging

- `fopen()` / `fprintf()` / `fflush()` / `fclose()` — writing to `events.log` and `orphaned_results.log`
- `snprintf()` + `fopen()` — generating dynamic paths (e.g. `/tmp/mesh_incoming_%d.c`) for temporary task payloads
- `fread()` / `fwrite()` — reading and writing binary files
- `remove()` — deleting temp files after task execution

---

### 8. Inter-Process Communication (IPC)

Tasks results travel from child → parent via a **loopback TCP socket using `worker_state.my_ip`** (strictly matching the address the server is bound to):
- Child connects to parent's listening socket on the mesh port
- Sends `MSG_TASK_RESULT` message with output
- Parent's `select()` loop picks it up just like any other peer message
- **No pipes, no shared memory** — same protocol used for local and remote tasks

---

### 9. Timing & Measurement

```c
struct timeval start, end;
gettimeofday(&start, NULL);
// ... execute task ...
gettimeofday(&end, NULL);
long ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
msg.execution_ms = ms;
```

Used in `process_manager.c` to measure and report task execution time in milliseconds.

---

### Summary Table

| OS Topic | Concept Applied |
|---|---|
| **Process Management** | `fork()`, `waitpid()`, `exit()`, `popen()` |
| **I/O Multiplexing** | `select()`, `fd_set`, `FD_SET/ISSET/ZERO` |
| **Socket Programming** | TCP server/client, UDP broadcast, `bind/listen/accept/connect` |
| **Signal Handling** | `SIGCHLD` for zombie prevention, `SIGPIPE` suppression |
| **File Descriptors** | Non-blocking I/O via `fcntl(O_NONBLOCK)`, FD scrubbing post-`fork()` |
| **Socket Options** | `SO_REUSEADDR`, `SO_BROADCAST`, `SO_RCVTIMEO` |
| **File I/O** | Log files, binary temp files, `snprintf`, `remove`, `fread/fwrite` |
| **IPC** | TCP loopback to `worker_state.my_ip` for child→parent result routing |
| **Timing** | `gettimeofday()` for execution time measurement |
| **Concurrency** | Multi-process with `fork()`, single-thread event loop with `select()` |

---

## 📌 Notes

- The current build uses only `mesh/` and `common/` sources.
- Legacy `server/` and `worker/` directories remain only for archive reference.
- Use Linux or WSL for correct POSIX socket and process behavior.