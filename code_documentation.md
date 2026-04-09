# P2P Distributed Computing Mesh — Complete Code Documentation

This document provides comprehensive explanations of every component in our **Peer-to-Peer (P2P) distributed computing mesh**. Unlike traditional client-server systems, every computer in this mesh acts as both worker and coordinator, creating a resilient distributed system with no single point of failure.

## 🏗️ P2P Mesh Architecture Overview

### The Innovation: No Server, No SPOF
Traditional distributed systems use a central server that becomes a single point of failure. Our P2P mesh **eliminates the server entirely** — every peer is equal and can submit tasks, execute work, and coordinate with others.

```
Peer A ─── Peer B ─── Peer C
  │  ╲       │         │
  │   ╲      │         │
  │    ╲     │         │
  └───── Peer D ───── Peer E
```
- **Full Mesh Topology**: Every peer connects to every other peer
- **Distributed Intelligence**: Load balancing happens everywhere
- **Fault Tolerance**: System survives any single peer failure

### Project Structure (P2P Mesh)
```text
mesh/
├── Makefile                    # Build system for mesh_bin
├── mesh_bin                    # Compiled P2P mesh executable
├── peers.conf                  # Optional peer configuration file
├── events.log                  # Comprehensive event logging
├── orphaned_results.log        # Results from crashed peers
├── common/                     # Shared utilities
│   ├── network.c              # Reliable TCP send/recv functions
│   └── logger.c               # Event logging system
├── include/                    # Header files
│   └── common.h               # Protocol definitions, structs, enums
└── mesh/                       # P2P mesh components
    ├── mesh_main.c            # Main event loop and command interface
    ├── mesh_monitor.c         # Load monitoring and peer health
    ├── process_manager.c      # Multi-process task execution
    └── task_queue.c           # Local task queuing
```

---

## 📡 1. `include/common.h` — P2P Protocol Definitions

This header file defines the shared "vocabulary" that all peers use to communicate. It's the foundation of our P2P mesh protocol.

### Core Data Structures

#### WorkerState — Global State for Each Peer
```c
typedef struct {
    char my_ip[16];                            // This peer's IP address
    int my_port;                               // This peer's port
    PeerInfo peers[MAX_PEERS];                 // Array of PeerInfo structs (IP, port, socket, load, health)
    int peer_count;                            // Number of known peers
    int my_load_percent;                       // Current CPU load (0-100%)
    Task task_queue[MAX_QUEUE];                // Local tasks queued when mesh is busy
    int queue_head, queue_tail;                // Circular queue indices (no separate struct)
    pid_t child_pids[MAX_CONCURRENT_TASKS];    // Active child process PIDs
    int child_count;                           // Count of running child processes
    ResultQueue result_queue;                  // Completed task results storage
} WorkerState;
```
**Purpose**: Single global struct shared across all modules. Task queue state (`queue_head`/`queue_tail`) is stored directly here — no separate `TaskQueue` struct.

#### PeerInfo — Per-Peer Connection Health
```c
typedef struct {
    char ip[16];
    int port;
    int socket_fd;        // TCP connection socket (-1 = disconnected)
    int load_percent;     // Last reported load
    int is_alive;         // 1=connected, 0=dead
    int queue_depth;      // Peer's local queue depth
    time_t last_heartbeat;
    int retry_count;      // Failed reconnection attempts
} PeerInfo;
```

#### Enhanced Message Struct for P2P Routing
```c
typedef struct {
    MessageType type;        // MSG_LOAD_UPDATE, MSG_TASK_ASSIGN, etc.
    int worker_id;
    int load_percent;
    TaskType task_type;
    int task_id;
    int task_arg;
    int task_result;
    char command[256];       // Task command or data
    char output[1024];       // Task results
    char source_ip[16];      // ORIGINATING peer IP (for result routing)
    int source_port;         // ORIGINATING peer port
    int hop_count;           // Reserved for future multi-hop routing
    int queue_depth;         // Sender's local queue depth
} Message;
```
**Key P2P Features**:
- **`source_ip/source_port`**: Critical for routing results back to originating peer
- **`hop_count`**: Currently set to 0; reserved for future multi-hop routing expansion

#### Task Structure
```c
typedef struct {
    int task_id;
    char source_ip[16];          // Original requester IP
    int source_port;             // Original requester port
    int hop_count;               // Routing hop counter
    int peer_visited[MAX_PEERS]; // Bitset of tried peers (prevents retry loops)
    TaskType type;               // TASK_SLEEP, TASK_EXEC, TASK_BINARY
    char command[256];           // Command string or sleep duration
    char filename[256];          // Source file path (for TASK_BINARY)
    long binary_size;
    char binary_data[MAX_BINARY_SIZE];
} Task;
```

#### BinaryTask — File Transfer Payload
```c
typedef struct {
    int  task_id;
    long binary_size;
    char binary_data[MAX_BINARY_SIZE];  // Raw .c source file content (up to 64KB)
} BinaryTask;
```
**Purpose**: Used to transfer `.c` source files over TCP to the executing peer. The receiver writes it to `/tmp/mesh_incoming_<id>.c`, then compiles and runs it natively — solving the ARM/x86 architecture mismatch that would occur if pre-compiled binaries were sent.

#### ResultQueue — Completed Task Storage
```c
typedef struct {
    TaskResult results[MAX_RESULTS];
    int head, tail;
} ResultQueue;
// Stored as worker_state.result_queue — not a global variable
```

### Message Types for P2P Communication
```c
typedef enum {
    MSG_REGISTER,
    MSG_LOAD_UPDATE,       // Periodic load broadcast (every 3 seconds)
    MSG_TASK_ASSIGN,       // Task assignment to peer
    MSG_TASK_RESULT,       // Task completion result
    MSG_HEARTBEAT,
    MSG_BINARY_RESULT,
    MSG_PEER_JOIN,         // New peer joining the mesh
    MSG_PEER_LEAVE,        // Peer leaving the mesh
    MSG_QUEUE_STATUS,      // Queue depth info
    MSG_LOAD_QUERY         // Request load from specific peer
} MessageType;
```

### Task Types
```c
typedef enum {
    TASK_SLEEP,     // Sleep for N seconds (load simulation)
    TASK_MATH,      // Math computation
    TASK_EXEC,      // Execute shell command via popen()
    TASK_BINARY     // Receive .c source, compile natively, execute
} TaskType;
```

---

## 🎯 2. `mesh/mesh_main.c` — Main Event Loop & Command Interface

This is the heart of each peer — a single-threaded event loop that handles network connections, user commands, and periodic maintenance.

### Core Architecture: select() Event Multiplexing
```c
while (1) {
    // 1. Setup file descriptors for all connections
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);        // User keyboard input
    FD_SET(listen_socket, &read_fds);       // New peer connections
    FD_SET(discovery_socket, &read_fds);    // UDP discovery broadcasts

    // Add all TCP peer sockets
    for each connected peer:
        FD_SET(peer_socket, &read_fds);

    // 2. Wait for events (1 second timeout)
    select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

    // 3. Handle events
    if (FD_ISSET(STDIN_FILENO)) handle_user_command();
    if (FD_ISSET(listen_socket)) handle_new_connection();
    if (FD_ISSET(discovery_socket)) handle_discovery();
    for each peer socket: handle_peer_message();

    // 4. Periodic maintenance (every loop)
    mesh_monitor_update();  // Load broadcasts, health checks
}
```

### Interactive Command Interface
The system provides a rich ANSI color-coded command-line interface:

```c
void mesh_main_process_user_command(char *command) {
    if (strcmp(command, "status") == 0) {
        mesh_main_show_status();  // ANSI color ASCII dashboard
    }
    else if (strncmp(command, "task", 4) == 0) {
        // Parse: "task 5" → submit 5-second sleep task
        int seconds = atoi(command + 5);
        mesh_main_submit_task(TASK_SLEEP, seconds);
    }
    else if (strcmp(command, "discover") == 0) {
        // UDP broadcast to find peers on LAN
        peer_manager_broadcast_discovery();
    }
    else if (strncmp(command, "exec", 4) == 0) {
        // Run a shell command on the best available peer
        mesh_main_submit_exec_task(command + 5);
    }
    else if (strcmp(command, "peers") == 0) {
        mesh_main_show_peers();
    }
    // Peers are joined via -P startup flag or auto-discovered via Gossip/UDP
}
```

### Status Dashboard — Actual Implemented Design
```
╔══════════════════════════════════════════════════════════╗
║       P2P MESH DISTRIBUTED WORKER                        ║
╚══════════════════════════════════════════════════════════╝

  IP  : 192.168.1.10   Port: 8080

mesh> status

╔══════════════════════════════════════════╗
║         MESH STATUS DASHBOARD            ║
╚══════════════════════════════════════════╝

  Node   : 192.168.1.10:8080
  Load   : [====------] 45%
  Tasks  : 2 active / 10 max
  Peers  : 3 connected / 4 total
  Queue  : 0 tasks   Results: 1

┌─ PEER STATUS ──────────────────────────────────────────┐
│  > 192.168.1.11:8080   [ALIVE]  Load:  15%  Queue: 0
│  > 192.168.1.12:8080   [ALIVE]  Load:  30%  Queue: 1
└────────────────────────────────────────────────────────┘
```

---

## 🔗 3. `mesh/peer_manager.c` — P2P Discovery & Mesh Formation

This module handles the complex task of forming and maintaining the P2P mesh topology.

### Peer Discovery Mechanisms

#### 1. Decentralized Gossip Protocol
```c
// Inside mesh_main_handle_peer_message (MSG_PEER_JOIN)
int is_new = (peer_manager_find_peer_index(msg->source_ip, msg->source_port) < 0);
mesh_monitor_handle_peer_join(msg->source_ip, msg->source_port);

// Peer Gossiping: if this is a newly discovered peer, broadcast their existence to everyone else
if (is_new) {
    mesh_main_broadcast_message(msg);
}
```
**Purpose**: Solves the configuration nightmare. You only need to connect to a single "seed node". That node automatically broadcasts your `MSG_PEER_JOIN` to all its other connections, who then connect to you. This organically wires the entire mesh together instantly.

#### 2. UDP Broadcast Discovery
```c
void peer_manager_broadcast_discovery() {
    // Send MSG_DISCOVERY_REQUEST to 255.255.255.255
    // Automatically finds peers on the local native network
}
```

#### Manual Peer Addition — Two-Step Process
`peer_manager_add_peer()` and `peer_manager_connect_to_peer()` are two **separate functions** with distinct responsibilities:

```c
// STEP 1: peer_manager_add_peer() — ONLY validates and registers in the array
int peer_manager_add_peer(const char *ip, int port) {
    // Check if already known
    if (peer_manager_find_peer_index(ip, port) >= 0) return 0;
    // Check capacity
    if (worker_state.peer_count >= MAX_PEERS) return -1;
    // Don't add ourselves
    if (strcmp(ip, worker_state.my_ip) == 0 && port == worker_state.my_port) return 0;

    // Register in array (socket_fd = -1, is_alive = 0 — just metadata)
    int idx = worker_state.peer_count;
    strcpy(worker_state.peers[idx].ip, ip);
    worker_state.peers[idx].port = port;
    worker_state.peers[idx].socket_fd = -1;  // No socket yet!
    worker_state.peers[idx].is_alive = 0;
    worker_state.peer_count++;
    return 0;
}

// STEP 2: peer_manager_connect_to_peer() — creates socket, connects, sends MSG_PEER_JOIN
void peer_manager_connect_to_peer(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);  // non-blocking connect with timeout
    connect(sock, &peer_addr, sizeof(peer_addr));
    // ... select() to wait for connection ...
    // On success: update peers[idx].socket_fd = sock, is_alive = 1
    // Then send MSG_PEER_JOIN to trigger Gossip propagation
}
```
**Key insight:** `add_peer()` returns `0` for both new AND already-existing peers. Do NOT use its return value to decide whether to connect — always check `socket_fd != -1` directly.

### Mesh Maintenance
- **Connection Monitoring**: Detects disconnected peers via `recv()` return values
- **State Synchronization**: All peers maintain identical knowledge of mesh topology
- **Dynamic Membership**: Peers can join/leave at runtime without restarting mesh

---

## ⚖️ 4. `mesh/mesh_monitor.c` — Load Balancing & Health Monitoring

This module implements distributed load balancing and fault tolerance.

### Distributed Load Balancing Algorithm
```c
// Actual function: peer_manager_get_best_peer() in peer_manager.c
int peer_manager_get_best_peer(int *visited_peers) {
    int best_peer = -1;
    int best_load = 101;

    for (int i = 0; i < worker_state.peer_count; i++) {
        if (visited_peers && visited_peers[i]) continue;    // Skip already-tried peers
        if (!worker_state.peers[i].is_alive) continue;      // Skip dead peers

        if (worker_state.peers[i].load_percent < best_load) {
            best_load = worker_state.peers[i].load_percent;
            best_peer = i;
        }
    }
    return best_peer;  // -1 if no peers available
}
```
**Strategy**: Returns the index of the alive peer with the lowest load that hasn't already been tried for this task.

### Periodic Load Broadcasting
```c
void mesh_monitor_broadcast_load_update() {
    Message load_msg;
    load_msg.type = MSG_LOAD_UPDATE;
    load_msg.load_percent = worker_state.my_load_percent;

    // Send to all connected peers
    for each peer:
        send(peer_socket, &load_msg, sizeof(Message), 0);
}
```
**Purpose**: Every peer broadcasts its load every **3 seconds** (verified: `difftime(now, last_load_broadcast) >= 3.0` in `mesh_monitor.c`), serving as both a load update and a heartbeat.

### Fault Detection & Recovery
```c
void mesh_monitor_check_peer_health() {
    for each peer:
        if recv(peer_socket, &msg, sizeof(Message), MSG_DONTWAIT) <= 0:
            // Peer disconnected or crashed
            log_event("Peer %s:%d disconnected", peer_ip, peer_port);
            peer_manager_remove_peer(i);
            // Redistribute any tasks that were assigned to this peer
            mesh_monitor_redistribute_tasks(i);
}
```
**Mechanism**: TCP connection monitoring + load broadcast heartbeats detect failures within 10 seconds.

### Event Loop Protection (Rate-Limited Reconnections)
Because connecting to a non-existent port blocks `select()` for up to 1 second, repeatedly polling down nodes causes event loop freezing in the main thread. We solved this by:
- Reconnecting to a **maximum of 1 dead peer per cycle** (every 30 seconds).
- Implementing a `retry_count`. After 3 failed reconnects, the peer is permanently purged to protect the core mesh responsiveness.

---

## 🔄 5. `mesh/process_manager.c` — Multi-Process Task Execution

This module handles concurrent task execution using `fork()`, providing isolation and parallelism.

### Fork-Based Task Execution
```c
int process_manager_execute_task(Task *task) {
    pid_t pid = fork();

    if (pid == 0) {
        // Child process - execute the task
        process_manager_child_execute(task);

        // Send result back to originating peer
        Message result;
        result.type = MSG_TASK_RESULT;
        result.task_id = task->task_id;
        strcpy(result.source_ip, task->source_ip);
        result.source_port = task->source_port;
        // ... fill in result data ...

        // Route result back through mesh
        process_manager_route_result(&result);
        exit(0);
    }
    else if (pid > 0) {
        // Parent process - track child
        worker_state.child_pids[worker_state.active_tasks] = pid;
        worker_state.active_tasks++;
        return 0;
    }
    else {
        // Fork failed
        return -1;
    }
}
```

### Child Process Task Execution
```c
void process_manager_child_execute(Task *task) {
    switch (task->task_type) {
        case TASK_SLEEP:
            int seconds = atoi(task->command);
            sleep(seconds);
            break;

        case TASK_EXEC:
            FILE *fp = popen(task->command, "r");
            fread(output, 1, sizeof(output)-1, fp);
            pclose(fp);
            break;

        case TASK_BINARY:
            // Execute binary and capture output/timing
            process_manager_execute_binary(task);
            break;
    }
}
```

### Process Lifecycle Management & FD Scrubbing
- **Tracking**: Maintains array of child PIDs for monitoring
- **Cleanup**: Reaps zombie processes, updates active task count
- **Isolation & Protection**: Each task runs in a separate process, but more importantly, **File Descriptor (FD) Scrubbing** is performed aggressively right after `fork()`. Linux limits an environment to typically 1024 open sockets/files. The codebase actively `close()`s FDs 3 through 1024 inside the child to prevent *Phantom Socket Leaks* (unintended inheritance of parent routing sockets) and ensures child connections to the parent run cleanly.

---

## 📋 6. `mesh/task_queue.c` — Local Task Queuing

When all peers are busy, tasks queue locally until capacity becomes available.

### Queue Management
The task queue does **not** use a separate `TaskQueue` struct. Queue state is stored directly in `WorkerState`:

```c
// Queue state lives directly in worker_state:
// worker_state.task_queue[MAX_QUEUE]  — circular buffer of Task structs
// worker_state.queue_head             — index of next item to dequeue
// worker_state.queue_tail             — index where next item will be enqueued

void task_queue_enqueue(Task *task) {
    // Check capacity: tail wraps around to head if full
    int next_tail = (worker_state.queue_tail + 1) % MAX_QUEUE;
    if (next_tail != worker_state.queue_head) {
        worker_state.task_queue[worker_state.queue_tail] = *task;
        worker_state.queue_tail = next_tail;
    }
}
```

### Automatic Processing
```c
void task_queue_check_and_process() {
    // Check if we can now execute queued tasks
    while (queue.size > 0 && worker_state.active_tasks < MAX_CONCURRENT_TASKS) {
        Task *task = task_queue_dequeue();
        if (task) {
            process_manager_execute_task(task);
        }
    }
}
```
**Trigger**: Called after task completion or peer load updates.

---

## 📝 7. `common/logger.c` — Comprehensive Event Logging

Tracks all mesh activities for debugging and monitoring.

### Event Logging Functions
```c
void log_event(const char *format, ...) {
    time_t now = time(NULL);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    FILE *log_file = fopen("events.log", "a");
    fprintf(log_file, "[%s] ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fclose(log_file);
}
```

### Orphaned Result Handling
```c
// Actual signature — takes four separate arguments, not a struct pointer
void log_orphaned_result(int task_id, const char *result, const char *source_ip, int source_port) {
    FILE *orphan_file = fopen("orphaned_results.log", "a");
    fprintf(orphan_file, "[ORPHANED] Task #%d from %s:%d\n", task_id, source_ip, source_port);
    fprintf(orphan_file, "Output: %s\n---\n", result);
    fclose(orphan_file);
}
```
**Purpose**: Captures results from tasks that completed but whose requesting peer had already disconnected. Called from `mesh_main.c` when `MSG_TASK_RESULT` arrives but `source_ip` is unreachable.

---

## 🌐 8. `common/network.c` — Reliable TCP Communication

Provides guaranteed data transfer across the mesh.

### Reliable Send Function
```c
int send_all(int sock, const void *buf, int total_size) {
    int sent = 0;
    const char *ptr = (const char *)buf;

    while (sent < total_size) {
        int s = send(sock, ptr + sent, total_size - sent, 0);
        if (s <= 0) return s;
        sent += s;
    }
    return sent;
}
```
**Purpose**: Ensures complete message transmission despite TCP packet fragmentation.

### Reliable Receive Function
```c
int recv_all(int sock, void *buf, int total_size) {
    int received = 0;
    char *ptr = (char *)buf;

    while (received < total_size) {
        int r = recv(sock, ptr + received, total_size - received, 0);
        if (r <= 0) return r;
        received += r;
    }
    return received;
}
```
**Critical for**: Binary file transfers and large result data across real networks.

---

## 🛠️ 9. `Makefile` — Build System

```makefile
CC = gcc
CFLAGS = -Wall -Iinclude -pthread
LDFLAGS = -lm

MESH_OBJS = mesh/mesh_main.o mesh/peer_manager.o mesh/process_manager.o \
            mesh/task_queue.o mesh/mesh_monitor.o common/network.o common/logger.o

mesh_bin: $(MESH_OBJS)
	$(CC) $(MESH_OBJS) -o mesh_bin $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mesh_bin mesh/*.o common/*.o events.log orphaned_results.log
```

**Features**:
- **Automatic Dependencies**: Recompiles only changed files
- **Include Path**: `-Iinclude` finds header files
- **Libraries**: Links math library for calculations
- **Clean Target**: Removes all generated files

---

## 🚀 10. Deployment & Usage

### Single Machine Testing (Two Terminals)
```bash
# Terminal 1 — Start first peer (seed node)
./mesh_bin -i 127.0.0.1 -p 8080

# Terminal 2 — Start second peer and connect to seed via -P flag
./mesh_bin -i 127.0.0.1 -p 8081 -P 127.0.0.1:8080

# From either terminal, submit tasks:
task 5
status
```

### Multi-Machine Lab Demo (4 Machines)
```bash
# Machine 1 — seed node (start first, no -P needed)
./mesh_bin -i 192.168.1.10 -p 8080

# Machine 2 — join via seed
./mesh_bin -i 192.168.1.11 -p 8080 -P 192.168.1.10:8080

# Machine 3 — join via seed
./mesh_bin -i 192.168.1.12 -p 8080 -P 192.168.1.10:8080

# Machine 4 — join via seed
./mesh_bin -i 192.168.1.13 -p 8080 -P 192.168.1.10:8080

# The Gossip Protocol automatically builds the full mesh from this point.
# Machines 2, 3, 4 do NOT need to know each other upfront.
```

### Interactive Commands
- `status` — Real-time mesh dashboard
- `task <n>` — Execute sleep task of n seconds
- `exec <cmd>` — Run shell command on best available peer
- `discover` — UDP broadcast peer discovery (LAN only)
- `peers` — List connected peers with load and status
- `load` — Show local load percentage
- `queue` — Show locally queued tasks
- `help` / `h` — Show help menu
- `quit` / `q` — Exit cleanly

---

## 🔧 Key Technical Concepts

### 1. P2P Mesh Topology
- **Full Connectivity**: Every peer connects to every other peer
- **Distributed State**: All peers maintain identical mesh knowledge
- **Dynamic Membership**: Peers can join/leave without restarting

### 2. Event-Driven Architecture
- **select() Multiplexing**: Single thread handles all connections
- **Non-blocking I/O**: Efficient handling of multiple peers
- **Timeout Management**: Periodic maintenance without busy waiting

### 3. Fault Tolerance Mechanisms
- **Connection Monitoring**: TCP disconnection detection
- **Heartbeat System**: Load broadcasts serve as keepalives
- **Task Redistribution**: Automatic reassignment on peer failure

### 4. Multi-Process Execution
- **Process Isolation**: Tasks can't interfere with each other
- **Resource Control**: Dedicated process per task
- **Concurrent Execution**: Multiple tasks run simultaneously

### 5. Custom Binary Protocol
- **Fixed-Size Structs**: No serialization overhead
- **Type Safety**: Enums prevent message type errors
- **Routing Information**: Source tracking for result delivery

---

## 🎯 Educational Value

This P2P mesh system demonstrates advanced systems programming concepts:

### Networking & Concurrency
- **Raw Socket Programming**: TCP/UDP socket creation and management
- **Event Multiplexing**: `select()`-based non-blocking I/O
- **Multi-process Architecture**: `fork()` for concurrent task execution
- **Inter-process Communication**: Result routing between processes

### Distributed Systems
- **P2P Architecture**: Mesh topology vs traditional client-server
- **Fault Tolerance**: Decentralized failure detection and recovery
- **Load Balancing**: Distributed task distribution algorithms
- **Consensus-Free Design**: No leader election or coordination protocols

### Systems Programming
- **Custom Protocols**: Binary message formats for efficiency
- **Resource Management**: Process lifecycle and socket management
- **Real-time Systems**: Periodic monitoring and state synchronization
- **Error Handling**: Comprehensive error detection and recovery

This codebase serves as a complete reference implementation of a production-ready P2P distributed computing system, suitable for academic study and real-world deployment scenarios.

---

## 🎨 11. Terminal User Interface (TUI) — `mesh/mesh_main.c`

The mesh binary features a full color-coded terminal UI implemented entirely using **ANSI escape codes** embedded inside `printf` calls. No external library (ncurses, etc.) is used — all formatting is done with raw ANSI sequences.

### 11.1 ANSI Color Macros

Defined at the top of `mesh_main.c` so they are visible to all functions:

```c
#define C_RST  "\033[0m"      // Reset — clear all attributes
#define C_RED  "\033[1;31m"   // Bold Red   → errors, DEAD status
#define C_GRN  "\033[1;32m"   // Bold Green → success, ALIVE status
#define C_YEL  "\033[1;33m"   // Bold Yellow → values, numbers
#define C_MAG  "\033[1;35m"   // Bold Magenta → labels, section heads
#define C_CYD  "\033[1;36m"   // Bold Cyan → borders, box-draw chars
```

**Critical Rule:** ANSI codes have **zero display width** but **non-zero byte length**. If you place a color macro *inside* the string variable passed to `%-Ns`, `printf` will count the escape bytes as visible characters and skip padding — causing column misalignment.

**Correct pattern** (colors in the format string literal, plain text in the argument):
```c
// CORRECT: color is part of the format literal, not the variable
printf(C_CYD "│  " C_RST "> " C_CYD "%-22s" C_RST " [%s%-5s" C_RST "] Load: %3d%%\n",
       peer_addr,              // pure text → %-22s counts correctly
       peer->is_alive ? C_GRN : C_RED,  // color passed as %s (0 display width)
       peer->is_alive ? "ALIVE" : "DEAD ", // plain text → %-5s counts correctly
       peer->load_percent);

// WRONG: concatenating color into the string variable
const char* alive_str = peer->is_alive ? C_GRN "ALIVE" C_RST : C_RED "DEAD" C_RST;
printf("%-15s\n", alive_str);  // BROKEN: printf counts ANSI bytes as display chars
```

---

### 11.2 Startup Banner — `main()`

The startup banner uses the **same box style** as `mesh_main_show_help()` to ensure visual consistency: a `╔══...══╗` box on the title (fixed pure-ASCII literal string), then open label rows below for variable data.

```c
printf("\n" C_MAG
       "╔══════════════════════════════════════════╗\n"
       "║       P2P MESH DISTRIBUTED WORKER        ║\n"
       "╚══════════════════════════════════════════╝\n\n" C_RST);
printf("  " C_MAG "IP  " C_RST ": " C_YEL "%s\n" C_RST, my_ip);
printf("  " C_MAG "Port" C_RST ": " C_YEL "%d\n\n" C_RST, my_port);
printf("  Use " C_GRN "'h'" C_RST " for help | " C_RED "'q'" C_RST " to quit\n\n");
```

**Why no emoji in the box?** The `🌐` emoji was originally used but is a **2-column-wide (double-width) Unicode character**. Box-drawing relies on exact column counts; a double-width char inside a box line makes the right border `║` appear 2 columns to the left of the top/bottom `╗`. Solution: use pure ASCII text inside all box lines.

---

### 11.3 Status Dashboard — `mesh_main_show_status()`

The dashboard uses a **hybrid design**: a fixed-literal title box, then free-form label rows, then a peer section with `┌─┐` borders.

**Load bar** — uses a 10-char pure ASCII `=` and `-` bar instead of block chars (`█`):
```c
char load_bar[11];
int filled = worker_state.my_load_percent / 10;
for (int b = 0; b < 10; b++) load_bar[b] = (b < filled) ? '=' : '-';
load_bar[10] = '\0';
// Output: [====------] for 40%
```
Block chars like `█` (U+2588) are **ambiguous-width** in many terminal configurations — they render as 2 columns, offsetting everything to the right. Plain `=`/`-` are always 1 column.

**Data rows** — no right-side `║` border. Only the title box has a border:
```c
// WRONG (right ║ will misalign with variable-length colored content):
printf(C_CYD "║" C_RST " Node: " C_YEL "%s" C_RST " ... " C_CYD "║\n", my_ip);

// CORRECT (open row, no right border — never misaligns):
printf("  " C_MAG "Node   " C_RST ": " C_YEL "%s" C_RST ":%d\n", my_ip, my_port);
```

**Peer rows** — color for status indicator passed as a separate `%s` argument:
```c
printf(C_CYD "  │  " C_RST "> " C_CYD "%-22s" C_RST
       " [%s%-5s" C_RST "]  Load: " C_YEL "%3d%%" C_RST "  Queue: %d\n",
       peer_addr,
       peer->is_alive ? C_GRN : C_RED,  // %s → zero display width, just sets color
       peer->is_alive ? "ALIVE" : "DEAD ",  // %-5s → pure text, width counted correctly
       peer->load_percent, peer->queue_depth);
```

---

### 11.4 Help Menu — `mesh_main_show_help()`

The help menu uses the **identical box style** as the startup banner — pure ASCII title, then indented label rows. This consistency is intentional so all major output sections have the same visual hierarchy.

```c
printf("\n" C_MAG "╔══════════════════════════════════════════╗\n");
printf("║         P2P MESH - HELP COMMANDS         ║\n");
printf("╚══════════════════════════════════════════╝\n\n" C_RST);
printf(C_MAG "Mesh Info:\n" C_RST);
printf("  " C_GRN "%-17s" C_RST " %s\n", "status", "Show mesh dashboard");
// ...
```

---

## 🐛 12. Bug Fix: Discovery Storm — `common/peer_manager.c`

### Problem
`peer_manager_add_peer()` returns `0` for both **new peers** AND **already-known peers**. The old handler treated `0` as "new peer — connect now!":

```c
// OLD BROKEN CODE:
if (peer_manager_add_peer(peer_ip, peer_port) == 0) {
    peer_manager_connect_to_peer(peer_ip, peer_port); // fires every 10s for ALL known peers!
}
```

Every 10 seconds, every node broadcasts a UDP discovery packet. Every other node receives it, calls `add_peer()` which returns `0`, and immediately tears down and rebuilds the TCP socket to that peer. This caused constant TCP reconnect storms across the whole mesh.

### Fix — `peer_manager_handle_discovery()`

```c
if (sscanf(message + 14, "%15[^:]:%d", peer_ip, &peer_port) == 2) {
    // 1. Don't add ourselves
    if (strcmp(peer_ip, worker_state.my_ip) == 0 && peer_port == worker_state.my_port)
        return;

    // 2. Already have an active TCP connection? Do NOT reconnect!
    int peer_idx = peer_manager_find_peer_index(peer_ip, peer_port);
    if (peer_idx >= 0 && worker_state.peers[peer_idx].socket_fd != -1)
        return;  // ← The critical guard: stable link, ignore broadcast

    // 3. Genuinely new peer — add and connect
    peer_manager_add_peer(peer_ip, peer_port);
    log_event("PEER_MANAGER", "Discovered peer %s:%d via broadcast", peer_ip, peer_port);
    peer_manager_connect_to_peer(peer_ip, peer_port);
}
```

**Key check:** `socket_fd != -1` means an active TCP socket exists. The discovery broadcast is discarded immediately with `return` — no reconnect, no disruption.

---

## 🔧 13. Bug Fix: Payload Fragmentation — `mesh/mesh_main.c`

### Problem

`send()` is **not guaranteed to deliver the full buffer** in a single call. For large structs like `Message` (~1400+ bytes), the kernel may flush a partial segment first. When routing a `MSG_TASK_RESULT` back to the originating peer:

```c
// OLD BROKEN CODE (in MSG_TASK_RESULT handler):
send(source_sock, msg, sizeof(Message), 0);
// Returns early if only e.g. 512 bytes were sent — remainder lost
```

The receiving peer's event loop reads the partial `Message` struct, interprets garbage bytes as the output field, and renders corrupted text to the terminal.

### Fix

```c
// NEW: send_all() loops until every byte is delivered
send_all(source_sock, msg, sizeof(Message));
```

The `send_all()` helper in `common/network.c`:

```c
int send_all(int sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *ptr = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(sock, ptr + sent, len - sent, 0);
        if (n <= 0) return -1;  // connection dropped
        sent += n;
    }
    return 0;  // all bytes delivered
}
```

This mirrors the existing `recv_all()` pattern already used everywhere else in the codebase. The fix ensures the full `sizeof(Message)` binary blob is atomically delivered to the receiving peer before control returns, preventing all partial-read UI corruption.

---

## 🖥️ 14. OS Concepts & System-Level Programming Reference

This section maps every OS concept covered in the course to the exact functions, files, and line-level explanations in this codebase. Use this as your **viva preparation reference**.

---

### A. Process Management — `fork()`, `waitpid()`, `popen()`

**File:** `mesh/process_manager.c`

```c
pid_t pid = fork();
if (pid == 0) {
    // ── CHILD PROCESS ──
    // Close all inherited peer sockets (FD scrubbing — see below)
    for (int fd = 3; fd < 1024; fd++) close(fd);

    // Execute the task
    if (task->task_type == TASK_EXEC) {
        // Execute shell command and capture output
        FILE *fp = popen(task->command, "r");
        // ... read output ...
        pclose(fp);
    } else {
        sleep(task->sleep_seconds);
    }
    exit(0);    // Child terminates cleanly

} else if (pid > 0) {
    // ── PARENT PROCESS ──
    child_pids[active_children++] = pid;  // Track child PID
}
```

**Why `fork()`?** Each task runs in an isolated child process. If a task crashes (segfault, timeout), only that child dies — the parent mesh process keeps running and serving other peers.

**`waitpid(pid, &status, WNOHANG)`** — called in the SIGCHLD handler to reap zombie children *without blocking* the parent event loop.

---

### B. I/O Multiplexing — `select()`

**File:** `mesh/mesh_main.c` → `mesh_main_event_loop()`

```c
fd_set read_fds;
int max_fd = 0;

FD_ZERO(&read_fds);

// Always watch: server socket, stdin, UDP discovery
FD_SET(server_sock, &read_fds);   max_fd = MAX(max_fd, server_sock);
FD_SET(STDIN_FILENO, &read_fds);  max_fd = MAX(max_fd, STDIN_FILENO);
FD_SET(udp_sock, &read_fds);      max_fd = MAX(max_fd, udp_sock);

// Also watch all connected peer sockets
for (int i = 0; i < peer_count; i++) {
    FD_SET(peers[i].socket_fd, &read_fds);
    max_fd = MAX(max_fd, peers[i].socket_fd);
}

// 2-second timeout triggers periodic maintenance
struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

if (activity > 0) {
    if (FD_ISSET(server_sock, &read_fds))  handle_new_connection();
    if (FD_ISSET(STDIN_FILENO, &read_fds)) handle_user_input();
    if (FD_ISSET(udp_sock, &read_fds))     handle_discovery();
    for (int i = 0; i < peer_count; i++)
        if (FD_ISSET(peers[i].socket_fd, &read_fds))
            handle_peer_message(i);
} else if (activity == 0) {
    // Timeout: run periodic tasks
    mesh_monitor_broadcast_load();
    task_queue_try_drain();
}
```

**OS concept:** `select()` is the classical POSIX multiplexing system call. It blocks until *any* of the registered file descriptors becomes ready for I/O, or until the timeout fires. This is how servers handle thousands of clients on a single thread.

---

### C. Socket Programming

**Files:** `common/peer_manager.c`, `mesh/mesh_main.c`

#### TCP Server Socket Setup
```c
int server_sock = socket(AF_INET, SOCK_STREAM, 0); // create TCP socket

// Allow immediate port reuse after restart
int opt = 1;
setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port   = htons(my_port),
    .sin_addr.s_addr = inet_addr(my_ip)
};
bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
listen(server_sock, 10);  // max 10 queued connections
```

#### TCP Client Connect (with non-blocking timeout)
```c
int sock = socket(AF_INET, SOCK_STREAM, 0);
fcntl(sock, F_SETFL, O_NONBLOCK);        // non-blocking
connect(sock, ...);                       // returns immediately with EINPROGRESS
// Wait for writability (= connect done) using select() with 3s timeout
// Then check SO_ERROR via getsockopt()
fcntl(sock, F_SETFL, flags);              // restore blocking
```

#### UDP Broadcast Socket
```c
int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
int broadcast = 1;
setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
// Send to 255.255.255.255 — all hosts on the LAN receive it
sendto(udp_sock, msg, strlen(msg), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
```

---

### D. Signal Handling

**File:** `mesh/mesh_main.c`

```c
// SIGCHLD: child process finished — reap it to prevent zombies
signal(SIGCHLD, sigchld_handler);

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    // WNOHANG: don't block if no child has exited yet
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_child_pid(pid);  // update active_children count
    }
}

// SIGPIPE: suppress crash when writing to a disconnected peer socket
signal(SIGPIPE, SIG_IGN);
```

**Why `WNOHANG`?** Without it, `waitpid()` would block the SIGCHLD handler until a child exits. `WNOHANG` makes it return immediately if no child is ready, so the main event loop resumes instantly.

---

### E. File Descriptor Management

**File:** `mesh/process_manager.c`

```c
// After fork(), child inherits ALL parent FDs (peer sockets, listen socket, log files)
// If child keeps them open, peers get confusing duplicate connections
// FD SCRUBBING: close everything above stderr (fd 2)
pid_t pid = fork();
if (pid == 0) {
    for (int fd = 3; fd < 1024; fd++) {
        close(fd);  // close ALL inherited FDs
    }
    // Now child only has stdin/stdout/stderr
    exec_task(...);
}
```

**Non-blocking FD operations (`fcntl`):**
```c
int flags = fcntl(sockfd, F_GETFL, 0);       // GET current flags
fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);  // SET non-blocking
// ... do timed connect ...
fcntl(sockfd, F_SETFL, flags);               // RESTORE blocking
```

---

### F. Socket Options — `setsockopt` / `getsockopt`

| Option | Level | Effect |
|---|---|---|
| `SO_REUSEADDR` | `SOL_SOCKET` | Reuse port immediately after program crash/restart |
| `SO_BROADCAST` | `SOL_SOCKET` | Allow `sendto()` to `255.255.255.255` |
| `SO_RCVTIMEO` | `SOL_SOCKET` | Set 200ms timeout on `recv()` — prevents event loop freeze |
| `SO_ERROR` | `SOL_SOCKET` | Read connect error after non-blocking `connect()` |

---

### G. Reliable Data Transfer — Custom `send_all` / `recv_all`

**File:** `common/network.c`

```c
// recv_all: keep reading until we have 'expected_bytes'
ssize_t recv_all(int sock, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = (char *)buf;
    while (received < len) {
        ssize_t n = recv(sock, ptr + received, len - received, 0);
        if (n <= 0) return n;  // disconnected or error
        received += n;
    }
    return received;
}

// send_all: keep sending until all bytes are flushed
int send_all(int sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *ptr = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(sock, ptr + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}
```

**OS concept:** TCP is a *stream* protocol — it delivers bytes in order but not necessarily in the same chunk sizes they were sent. Every real application must loop `recv()` until the expected message size is received.

---

### H. File I/O — Logging & Temp Files

**File:** `common/logger.c`, `mesh/process_manager.c`

```c
// Append to event log
FILE *log = fopen("events.log", "a");
fprintf(log, "[%s] %s: %s\n", timestamp, event_type, message);
fflush(log);   // Force kernel to write — important for crash safety
fclose(log);

// Temp file for delegated .c source compilation
char tmp_path[256];
snprintf(tmp_path, sizeof(tmp_path), "/tmp/mesh_incoming_%d.c", msg->task_id);

FILE *fp = fopen(tmp_path, "w");
fwrite(msg->binary_data, 1, msg->binary_size, fp);
fclose(fp);

// ... compile and execute natively ...

// Clean up after execution
remove(tmp_path);
```

---

### H. Timing — `gettimeofday()`

**File:** `mesh/process_manager.c`

```c
struct timeval t_start, t_end;
gettimeofday(&t_start, NULL);

// ... run the task ...

gettimeofday(&t_end, NULL);
long elapsed_ms = (t_end.tv_sec  - t_start.tv_sec)  * 1000
                + (t_end.tv_usec - t_start.tv_usec) / 1000;
result_msg.execution_ms = elapsed_ms;
```

`gettimeofday()` gives microsecond resolution and is the standard POSIX way to measure wall-clock elapsed time in C programs.

---

### Complete OS Concept → Code Mapping

| OS Concept | POSIX API Used | File |
|---|---|---|
| Process creation | `fork()` | `process_manager.c` |
| Child reaping / zombie prevention | `waitpid(-1, WNOHANG)` | `mesh_main.c` (SIGCHLD) |
| Program execution | `popen()`, `pclose()` | `process_manager.c` |
| I/O multiplexing | `select()`, `FD_SET/ISSET/ZERO` | `mesh_main.c` |
| TCP server | `socket()`, `bind()`, `listen()`, `accept()` | `mesh_main.c` |
| TCP client | `socket()`, `connect()` | `peer_manager.c` |
| UDP broadcast | `socket(SOCK_DGRAM)`, `sendto()`, `recvfrom()` | `peer_manager.c` |
| Non-blocking I/O | `fcntl(O_NONBLOCK)`, `select()` + write_fd | `peer_manager.c` |
| Socket options | `setsockopt()`, `getsockopt()` | `peer_manager.c`, `mesh_main.c` |
| Signal handling | `signal()`, `SIGCHLD`, `SIGPIPE` | `mesh_main.c` |
| FD management / scrubbing | `close()`, `fcntl()`, FD loop | `process_manager.c` |
| Reliable stream I/O | Custom `send_all()` / `recv_all()` loops | `network.c` |
| File I/O & logging | `fopen()`, `fprintf()`, `fwrite()`, `remove()` | `logger.c`, `process_manager.c` |
| Timing / measurement | `gettimeofday()` | `process_manager.c` |
| IPC (loopback socket) | TCP loopback to `worker_state.my_ip` | `process_manager.c` |
