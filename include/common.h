/* common.h — Shared protocol definitions used by ALL modules
 * Do NOT modify without informing the whole group — every file includes this.
 */
#ifndef COMMON_H
#define COMMON_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/select.h>
#include<time.h>
#include<sys/stat.h>

#define PORT            8080
#define BUFFER_SIZE     1024
#define MAX_WORKERS     10
#define MAX_PEERS       10
#define MAX_BINARY_SIZE 65536   // 64KB
#define MAX_QUEUE       100     // Max tasks in local queue
#define MAX_CONCURRENT_TASKS 10 // Max child processes per worker
#define MAX_HOPS        5       // Max routing hops to prevent loops

// Extended message types for P2P mesh
typedef enum {
    MSG_REGISTER,
    MSG_LOAD_UPDATE,
    MSG_TASK_ASSIGN,
    MSG_TASK_RESULT,
    MSG_HEARTBEAT,
    MSG_BINARY_RESULT,
    MSG_PEER_JOIN,        // NEW: Peer joining mesh
    MSG_PEER_LEAVE,       // NEW: Peer leaving mesh
    MSG_QUEUE_STATUS,     // NEW: Queue depth info
    MSG_LOAD_QUERY        // NEW: Request load from specific peer
} MessageType;

typedef enum {
    TASK_SLEEP,
    TASK_MATH,
    TASK_EXEC,
    TASK_BINARY
} TaskType;

// Extended Message struct for P2P mesh
typedef struct {
    MessageType type;
    int worker_id;
    int load_percent;
    TaskType task_type;
    int task_id;
    int task_arg;
    int task_result;
    char command[256];
    char output[1024];
    // NEW P2P fields
    char source_ip[16];      // Origin of task for result routing
    int source_port;         // Port for result routing
    int hop_count;           // Routing hops (anti-loop)
    int queue_depth;         // Local queue status
} Message;

// Extended Task struct for P2P mesh
typedef struct {
    int task_id;
    char source_ip[16];          // NEW: Original requester IP
    int source_port;             // NEW: Original requester port
    int hop_count;               // NEW: Max 5 hops (anti-loop)
    int peer_visited[MAX_PEERS]; // NEW: Bitset of tried peers
    TaskType type;
    char command[256];
    char filename[256];          // For binary tasks
    long binary_size;            // Size of compiled binary
    char binary_data[MAX_BINARY_SIZE]; // Raw binary data
} Task;

// Peer information for mesh management
typedef struct {
    char ip[16];
    int port;
    int socket_fd;           // Connection socket (-1 if disconnected)
    int load_percent;        // Last reported load
    long last_load_update;   // Timestamp of last update
    int is_alive;            // 1=connected, 0=dead
    int queue_depth;         // How many tasks queued locally
    time_t last_heartbeat;   // For detecting dead peers
} PeerInfo;

// Result queue for storing completed task results
#define MAX_RESULTS 50
typedef struct {
    int task_id;
    char command[256];              // Original command/file
    char result[2048];              // Output from execution
    long execution_time_ms;
    time_t completion_time;
    int success;                    // 1 for success, 0 for failure
} TaskResult;

typedef struct {
    TaskResult results[MAX_RESULTS];
    int head, tail;
} ResultQueue;

// Local worker state
typedef struct {
    char my_ip[16];
    int my_port;
    PeerInfo peers[MAX_PEERS];
    int peer_count;
    int my_load_percent;
    // Task queue for when all peers are busy
    Task task_queue[MAX_QUEUE];
    int queue_head, queue_tail;
    // Child process tracking
    pid_t child_pids[MAX_CONCURRENT_TASKS];
    int child_count;
    // Result queue for completed tasks
    ResultQueue result_queue;
} WorkerState;

// Binary task structures (unchanged)
typedef struct {
    int  task_id;
    long binary_size;
    char binary_data[MAX_BINARY_SIZE];
} BinaryTask;

typedef struct {
    int  task_id;
    long execution_ms;
    char output[4096];
} BinaryResult;

// Function declarations for common utilities
int recv_all(int sockfd, void *buffer, size_t length);
int send_all(int sockfd, void *buffer, size_t length);
void log_event(const char *event_type, const char *format, ...);
void get_current_time_string(char *buffer, size_t size);

// Logger module functions
void logger_init();
void logger_cleanup();
void log_error(const char *component, const char *message, const char *details);
void log_warning(const char *component, const char *message);
void log_peer_event(const char *event, const char *ip, int port);
void log_network_event(const char *event, const char *peer_ip, int peer_port, const char *details);
void log_process_event(const char *event, int pid, int task_id);
void log_queue_event(const char *event, int task_id, int queue_depth);
void log_orphaned_result(int task_id, const char *result, const char *source_ip, int source_port);

// Peer manager functions
void peer_manager_init(const char *my_ip, int my_port);
int peer_manager_load_peers(const char *filename);
int peer_manager_add_peer(const char *ip, int port);
void peer_manager_broadcast_discovery();
void peer_manager_handle_discovery(const char *message, const char *sender_ip);
void peer_manager_connect_to_peer(const char *ip, int port);
void peer_manager_connect_to_all();
int peer_manager_get_peer_count();
int peer_manager_get_peer_socket(int index);
PeerInfo *peer_manager_get_peer_info(int index);
int peer_manager_find_peer_index(const char *ip, int port);
void peer_manager_handle_peer_join(const char *ip, int port);
int peer_manager_get_connected_count();
int peer_manager_get_best_peer(int *visited_peers);
void peer_manager_cleanup();

// Task queue functions
void task_queue_init();
int task_queue_enqueue(Task *task);
int task_queue_get_depth();
void task_queue_check_and_process();
void task_queue_print_status();

// Result queue functions
void result_queue_init();
int result_queue_enqueue(int task_id, const char *command, const char *result, long exec_time_ms, int success);
int result_queue_is_empty();
int result_queue_get_depth();
void result_queue_display_all();
void result_queue_display_latest(int count);

// Process manager functions
void process_manager_init();
int process_manager_can_execute();
int process_manager_execute_task(Task *task);
void process_manager_send_result(Task *task, const char *result);
void process_manager_cleanup();
void process_manager_get_stats(int *active_children, int *max_children);
void process_manager_check_completed();

// Mesh monitor functions
void mesh_monitor_init();
void mesh_monitor_update();
void mesh_monitor_broadcast_load_update();
void mesh_monitor_check_peer_health();
void mesh_monitor_retry_failed_connections();  // NEW: retry failed peer connections
void mesh_monitor_mark_peer_dead(int peer_idx);
void mesh_monitor_handle_load_update(const char *peer_ip, int peer_port, int load_percent, int queue_depth);
void mesh_monitor_handle_peer_join(const char *peer_ip, int peer_port);
void mesh_monitor_get_stats(int *total_peers, int *connected_peers, int *avg_load, int *total_queue_depth);
void mesh_monitor_print_status();
void mesh_monitor_force_load_broadcast();
void mesh_monitor_stop();
void mesh_monitor_start();

#endif
