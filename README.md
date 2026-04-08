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

### Start the first peer
The first peer in the mesh is the initial entry point. Start it without `-P`.

```bash
./mesh_bin -i 192.168.1.10 -p 8080
```
- `-i` is this peer's local IP.
- `-p` is this peer's listening port.

### Join the mesh
After the first peer is running, any other peer can join the mesh in one of three ways.

1. Join while starting the peer:
```bash
./mesh_bin -i 192.168.1.11 -p 8080 -P 192.168.1.10:8080
```
- `-P` is the address of an already-running peer in the mesh.

2. Start the peer first, then join later from its command shell:
```bash
./mesh_bin -i 192.168.1.11 -p 8080
```
Then enter:
```bash
join 192.168.1.10:8080
```

3. Discover peers automatically on the LAN:
```bash
discover
```
- Use this when peers are on the same local network.

### Optional `peers.conf`
Create a file with one `IP:PORT` entry per line:
```
192.168.1.10:8080
192.168.1.11:8080
192.168.1.12:8080
```

---

## 🎛️ Command Guide

| Command | Description |
|---|---|
| `status` | Show the live mesh dashboard |
| `task <n>` | Submit a sleep task for `n` seconds |
| `exec <cmd>` | Run a shell command on the mesh |
| `join <ip:port>` | Connect to a peer already in the mesh |
| `discover` | Broadcast discovery on the LAN to find peers |
| `peers` | List connected peers |
| `load` | Show current load values |
| `queue` | Show local queued tasks |
| `help` | Show available commands |
| `quit` | Exit cleanly |

---

## 📊 Visual Status Dashboard

A clean and fully aligned ASCII dashboard example:

```
╔════════════════════════════════════════════════════════════════════════╗
║                          MESH STATUS DASHBOARD                         ║
╠════════════════════════════════════════════════════════════════════════╣
║ Peer: 192.168.1.10                        Load: ███████░ 15%           ║
║ Connected peers: 3 / 4                    Queue: 0 tasks               ║
║ Active tasks: 2 / 10                      Average load: 23%            ║
╠════════════════════════════════════════════════════════════════════════╣
║                             PEER STATUS DETAILS                        ║
║  ┌───────────────────────────────┐  ┌───────────────────────────────┐  ║
║  │ Peer B | Load: 15% | ALIVE    │  │ Peer C | Load: 30% | ALIVE    │  ║
║  └───────────────────────────────┘  └───────────────────────────────┘  ║
║  ┌───────────────────────────────┐                                     ║
║  │ Peer D | Load: 25% | ALIVE    │                                     ║
║  └───────────────────────────────┘                                     ║
╠════════════════════════════════════════════════════════════════════════╣
║ Recent activity:                                                       ║
║  • Task #112 assigned to Peer C                                        ║
║  • Peer D joined the mesh                                              ║
╚════════════════════════════════════════════════════════════════════════╝
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

### Fault Tolerance
- Detects failed peers through TCP and heartbeat monitoring.
- Removes dead peers automatically from the mesh.
- Reassigns tasks when a peer disappears.

### Multi-Process Execution
- Uses `fork()` so task execution does not block the mesh.
- Keeps the main peer responsive while child processes work.
- Logs orphaned task results if a peer crashes mid-execution.

---

## 📌 Notes

- The current build uses only `mesh/` and `common/` sources.
- Legacy `server/` and `worker/` directories remain only for archive reference.
- Use Linux or WSL for correct POSIX socket and process behavior.
''