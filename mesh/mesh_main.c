/* mesh_main.c — Main entry point for P2P mesh worker
 * Coordinates all mesh components and handles the main event loop
 */

#include "../include/common.h"
#include <getopt.h>
#include <errno.h>

// External reference to worker state
extern WorkerState worker_state;
#include <sys/select.h>

// Function declarations
void mesh_main_show_help();
void mesh_main_show_status();
void mesh_main_process_user_command(char *command);
void mesh_main_handle_peer_message(int peer_sock, Message *msg);
void mesh_main_accept_peer_connection(int listen_sock);
void mesh_main_handle_discovery_message(int discovery_sock);
void mesh_main_broadcast_message(Message *msg);
void mesh_main_delegate_task_to_peer(Task *task);

// Global state
static int listen_socket = -1;
static int discovery_socket = -1; // NEW: UDP socket for peer discovery
static char my_ip[16] = "127.0.0.1"; // Default to localhost
static int my_port = PORT;

// Pending incoming connections (waiting for MSG_PEER_JOIN to identify real port)
#define MAX_PENDING_CONNECTIONS 10
static int pending_sockets[MAX_PENDING_CONNECTIONS];
static int pending_count = 0;

int main(int argc, char *argv[]) {
    // Parse command line arguments
    int opt;
    int test_mode = 0;
    // Buffer for command-line peers so they survive initialization reset
    struct { char ip[64]; int port; } cmd_peers[10];
    int cmd_peer_count = 0;

    while ((opt = getopt(argc, argv, "i:p:P:ht")) != -1) {
        switch (opt) {
            case 'i':
                strncpy(my_ip, optarg, sizeof(my_ip) - 1);
                break;
            case 'p':
                my_port = atoi(optarg);
                break;
            case 'P': {
                // Parse peer IP:PORT
                char *colon = strchr(optarg, ':');
                if (colon && cmd_peer_count < 10) {
                    *colon = '\0';
                    strncpy(cmd_peers[cmd_peer_count].ip, optarg, 63);
                    cmd_peers[cmd_peer_count].port = atoi(colon + 1);
                    cmd_peer_count++;
                } else if (!colon) {
                    fprintf(stderr, "Invalid peer format: %s (use IP:PORT)\n", optarg);
                }
                break;
            }
            case 't':
                test_mode = 1;
                break;
            case 'h':
            default:
                mesh_main_show_help();
                return 0;
        }
    }

    printf("🚀 Starting P2P Mesh Worker\n");
    printf("   IP: %s, Port: %d\n", my_ip, my_port);
    printf("   Press 'h' for help, 'q' to quit\n\n");

    // Initialize all components
    logger_init();
    peer_manager_init(my_ip, my_port);
    task_queue_init();
    result_queue_init();
    process_manager_init();
    mesh_monitor_init();

    // Add back the peers parsed from command line
    for (int i = 0; i < cmd_peer_count; i++) {
        peer_manager_add_peer(cmd_peers[i].ip, cmd_peers[i].port);
    }

    // Load peer configuration (optional)
    int loaded_peers = peer_manager_load_peers("peers.conf");
    if (loaded_peers < 0) {
        printf("⚠️  No peers.conf found - starting with empty mesh\n");
        printf("   Peers will auto-discover each other. Use 'discover' to broadcast.\n\n");
    } else {
        printf("✅ Loaded %d peers from peers.conf\n\n", loaded_peers);
    }

    // Create listen socket
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        log_error("MESH_MAIN", "Failed to create listen socket", strerror(errno));
        return 1;
    }

    // Set socket options
    int opt_val = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(my_port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces to avoid EADDRNOTAVAIL

    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("MESH_MAIN", "Failed to bind socket", strerror(errno));
        close(listen_socket);
        return 1;
    }

    // Listen for connections
    if (listen(listen_socket, 10) < 0) {
        log_error("MESH_MAIN", "Failed to listen", strerror(errno));
        close(listen_socket);
        return 1;
    }

    printf("✅ Listening on %s:%d\n", my_ip, my_port);

    // Create UDP discovery socket
    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket >= 0) {
        // Enable SO_REUSEADDR and SO_REUSEPORT so multiple local terminals can share the same UDP discovery port
        int reuse = 1;
        setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        struct sockaddr_in discovery_addr;
        memset(&discovery_addr, 0, sizeof(discovery_addr));
        discovery_addr.sin_family = AF_INET;
        discovery_addr.sin_port = htons(8081); // Discovery port
        discovery_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(discovery_socket, (struct sockaddr*)&discovery_addr, sizeof(discovery_addr)) == 0) {
            printf("✅ Discovery listening on UDP port 8081\n");
        } else {
            log_warning("MESH_MAIN", "Failed to bind discovery socket");
            close(discovery_socket);
            discovery_socket = -1;
        }
    }

    // Connect to known peers
    peer_manager_connect_to_all();

    // Test mode - run automated test sequence
    if (test_mode) {
        printf("🧪 DEBUG: Test mode activated, running automated tests...\n");
        printf("🧪 DEBUG: About to call peer_manager_connect_to_all()\n");
        
        // Wait a bit for connections
        sleep(2);
        
        // Test local task execution
        printf("Testing local task execution...\n");
        Task local_task;
        memset(&local_task, 0, sizeof(local_task));
        local_task.task_id = 1001;
        local_task.type = TASK_SLEEP;
        snprintf(local_task.command, sizeof(local_task.command), "%d", 1);
        strncpy(local_task.source_ip, my_ip, sizeof(local_task.source_ip) - 1);
        local_task.source_port = my_port;
        
        if (process_manager_execute_task(&local_task) == 0) {
            printf("✅ Local task submitted\n");
        }
        
        // Wait for completion
        sleep(3);
        
        // Test delegation (will fail if no peers, but shows the logic)
        printf("Testing delegation logic...\n");
        mesh_main_process_user_command("test_delegate");
        
        // Show results
        sleep(2);
        mesh_main_process_user_command("results");
        
        printf("🧪 Test completed. Exiting...\n");
        goto cleanup;
    }

    // Main event loop
    fd_set read_fds;
    int max_fd;
    char input_buffer[1024];

    printf("✅ Mesh worker ready. Type commands or 'help' for assistance.\n\n");

    while (1) {
        // Clear and setup file descriptor set
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(listen_socket, &read_fds);
        max_fd = listen_socket;

        // Add discovery socket if available
        if (discovery_socket != -1) {
            FD_SET(discovery_socket, &read_fds);
            if (discovery_socket > max_fd) max_fd = discovery_socket;
        }

        // Add all peer sockets
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            int peer_sock = peer_manager_get_peer_socket(i);
            if (peer_sock != -1) {
                FD_SET(peer_sock, &read_fds);
                if (peer_sock > max_fd) max_fd = peer_sock;
            }
        }

        // Add pending incoming connection sockets
        for (int i = 0; i < pending_count; i++) {
            FD_SET(pending_sockets[i], &read_fds);
            if (pending_sockets[i] > max_fd) max_fd = pending_sockets[i];
        }

        // Wait for activity (timeout every 1 second for monitoring)
        struct timeval timeout = {1, 0}; // 1 second timeout
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            log_error("MESH_MAIN", "Select error", strerror(errno));
            continue;
        }

        // Handle timeouts (periodic monitoring)
        if (activity == 0) {
            mesh_monitor_update();
            continue;
        }

        // Check for new peer connections
        if (FD_ISSET(listen_socket, &read_fds)) {
            mesh_main_accept_peer_connection(listen_socket);
        }

        // Check for discovery messages
        if (discovery_socket != -1 && FD_ISSET(discovery_socket, &read_fds)) {
            mesh_main_handle_discovery_message(discovery_socket);
        }

        // Check for peer messages
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            int peer_sock = peer_manager_get_peer_socket(i);
            if (peer_sock != -1 && FD_ISSET(peer_sock, &read_fds)) {
                Message msg;
                ssize_t bytes_read = recv_all(peer_sock, &msg, sizeof(msg));

                if (bytes_read <= 0) {
                    // Peer disconnected or error
                    mesh_monitor_mark_peer_dead(i);
                } else {
                    mesh_main_handle_peer_message(peer_sock, &msg);
                }
            }
        }

        // Check for messages from pending connections
        for (int i = 0; i < pending_count; ) {
            if (FD_ISSET(pending_sockets[i], &read_fds)) {
                Message msg;
                ssize_t bytes_read = recv_all(pending_sockets[i], &msg, sizeof(msg));

                if (bytes_read > 0) {
                    // Only add to routing table if this is ACTUAL mesh traffic! 
                    // Local child processes connect to this exact same port just to drop off MSG_TASK_RESULT
                    // If we blindly add MSG_TASK_RESULT sockets to the peer table, the child process 
                    // impersonates the original target and completely clobbers the TCP routing map!
                    
                    int is_child_process = (msg.type == MSG_TASK_RESULT);
                    int was_new = 0;

                    if (!is_child_process) {
                        int peer_idx = peer_manager_find_peer_index(msg.source_ip, msg.source_port);
                        if (peer_idx < 0) {
                            // New peer - add them
                            peer_manager_add_peer(msg.source_ip, msg.source_port);
                            peer_idx = peer_manager_find_peer_index(msg.source_ip, msg.source_port);
                            was_new = 1;
                        }
                        if (peer_idx >= 0) {
                            worker_state.peers[peer_idx].socket_fd = pending_sockets[i];
                            worker_state.peers[peer_idx].is_alive = 1;
                            worker_state.peers[peer_idx].last_heartbeat = time(NULL);
                            worker_state.peers[peer_idx].retry_count = 0;
                        }
                    }

                    // Process the message (handles results, joins, routing, etc)
                    mesh_main_handle_peer_message(pending_sockets[i], &msg);
                    
                    // Gossip this direct connection to the rest of the network!
                    if (was_new && msg.type == MSG_PEER_JOIN) {
                        mesh_main_broadcast_message(&msg);
                    }
                    
                    // If this was a local child process dropping off a result, 
                    // no one else is tracking this socket, so we MUST close it!
                    if (is_child_process) {
                        close(pending_sockets[i]);
                    }
                    
                    // Remove from pending
                    pending_count--;
                    pending_sockets[i] = pending_sockets[pending_count];
                    continue;
                } else {
                    // Connection closed or error
                    close(pending_sockets[i]);
                    pending_count--;
                    pending_sockets[i] = pending_sockets[pending_count];
                    continue;
                }
            }
            i++;
        }

        // Check for user input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
                // Remove newline
                input_buffer[strcspn(input_buffer, "\n")] = 0;

                if (strcmp(input_buffer, "q") == 0 || strcmp(input_buffer, "quit") == 0) {
                    break;
                }

                mesh_main_process_user_command(input_buffer);
            }
        }

        // Periodic maintenance
        mesh_monitor_update();
    }

cleanup:
    // Cleanup
    printf("\n🛑 Shutting down mesh worker...\n");

    mesh_monitor_stop();
    process_manager_cleanup();
    peer_manager_cleanup();
    logger_cleanup();

    if (listen_socket != -1) {
        close(listen_socket);
    }
    if (discovery_socket != -1) {
        close(discovery_socket);
    }

    printf("✅ Shutdown complete.\n");
    return 0;
}

void mesh_main_show_help() {
    printf("P2P Mesh Worker - Distributed Computing System\n\n");
    printf("Usage: ./mesh_bin [options]\n\n");
    printf("Options:\n");
    printf("  -i <ip>       IP address to bind to (default: 127.0.0.1)\n");
    printf("  -p <port>     Port to listen on (default: %d)\n", PORT);
    printf("  -P <ip:port>  Add a peer to connect to (can be used multiple times)\n");
    printf("  -t            Run in test mode (automated testing)\n");
    printf("  -h            Show this help\n\n");
    printf("Commands (once running):\n");
    printf("  help          Show command help\n");
    printf("  status        Show mesh status\n");
    printf("  discover      Broadcast discovery to find peers\n");
    printf("  run <file>    Compile and execute C file or run binary\n");
    printf("  exec <cmd>    Execute shell command\n");
    printf("  task <n>      Execute sleep task for n seconds\n");
    printf("  results       Show task results queue\n");
    printf("  peers         Show peer information\n");
    printf("  queue         Show task queue\n");
    printf("  load          Show load information\n");
    printf("  q/quit        Exit\n\n");
}

void mesh_main_show_status() {
    int total_peers, connected_peers, avg_load, total_queue_depth;
    mesh_monitor_get_stats(&total_peers, &connected_peers, &avg_load, &total_queue_depth);

    int active_children, max_children;
    process_manager_get_stats(&active_children, &max_children);

    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    MESH STATUS DASHBOARD                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Worker ID: %-20s Load: ███████░ %-3d%% ║\n", my_ip, worker_state.my_load_percent);
    printf("║ Mesh Peers: %-2d/%-2d connected         Queue: %-3d tasks    ║\n",
           connected_peers, total_peers, task_queue_get_depth());
    printf("║ Active Tasks: %-2d/%-2d                 Results: %-3d items  ║\n",
           active_children, max_children, result_queue_get_depth());
    printf("║                                                              ║\n");
    printf("║ PEER STATUS:                                                 ║\n");

    for (int i = 0; i < peer_manager_get_peer_count(); i++) {
        PeerInfo *peer = peer_manager_get_peer_info(i);
        if (peer) {
            printf("║  ┌─ %-15s ─┐                                         ║\n",
                   peer->is_alive ? "ALIVE" : "DEAD");
            printf("║  │ Load: %-3d%%  Queue: %-2d │                                   ║\n",
                   peer->load_percent, peer->queue_depth);
            printf("║  └─────────────────────┘                             ║\n");
        }
    }

    printf("║                                                              ║\n");
    printf("║ RECENT ACTIVITY:                                             ║\n");
    printf("║  (Check events.log for detailed activity)                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void mesh_main_process_user_command(char *command) {
    if (strlen(command) == 0) return;

    log_event("USER_COMMAND", "Received: %s", command);

    if (strcmp(command, "help") == 0 || strcmp(command, "h") == 0) {
        // Show help (we'll implement a more detailed help system later)
        printf("Available commands:\n");
        printf("  help          Show this help\n");
        printf("  status        Show mesh status\n");
        printf("  discover      Broadcast discovery to find peers\n");
        printf("  run <file>    Compile and execute C file or run binary\n");
        printf("  exec <cmd>    Execute shell command\n");
        printf("  task <n>      Execute sleep task for n seconds\n");
        printf("  results       Show task results queue\n");
        printf("  peers         Show peer information\n");
        printf("  queue         Show task queue\n");
        printf("  load          Show load information\n");
        printf("  test_delegate Test task delegation (forces delegation)\n");
        printf("  q/quit        Exit\n");

    } else if (strcmp(command, "status") == 0) {
        mesh_main_show_status();

    } else if (strcmp(command, "discover") == 0) {
        printf("🔍 Broadcasting discovery message...\n");
        peer_manager_broadcast_discovery();
        printf("✅ Discovery broadcast sent. Other peers should find you.\n");
        printf("\nPeer Information:\n");
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            PeerInfo *peer = peer_manager_get_peer_info(i);
            if (peer) {
                printf("  %s:%d - %s (Load: %d%%, Queue: %d)\n",
                       peer->ip, peer->port,
                       peer->is_alive ? "CONNECTED" : "DISCONNECTED",
                       peer->load_percent, peer->queue_depth);
            }
        }
        printf("\n");

    } else if (strcmp(command, "peers") == 0) {
        printf("Peer information:\n");
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            PeerInfo *peer = peer_manager_get_peer_info(i);
            if (peer) {
                printf("  %s:%d - %s | Load: %d%% | Queue: %d\n",
                       peer->ip, peer->port,
                       peer->is_alive ? "CONNECTED" : "DISCONNECTED",
                       peer->load_percent, peer->queue_depth);
            }
        }
        printf("\n");

    } else if (strcmp(command, "queue") == 0) {
        task_queue_print_status();

    } else if (strcmp(command, "load") == 0) {
        int active_children, max_children;
        process_manager_get_stats(&active_children, &max_children);
        
        printf("📊 Load Information:\n");
        printf("  Current load: %d%%\n", worker_state.my_load_percent);
        printf("  Active tasks: %d/%d\n", active_children, max_children);
        printf("  Queue depth: %d/%d\n", task_queue_get_depth(), MAX_QUEUE);
        printf("  Results stored: %d\n", result_queue_get_depth());
        printf("  Peers connected: %d/%d\n", peer_manager_get_connected_count(), peer_manager_get_peer_count());
        printf("\n");

    } else if (strncmp(command, "run ", 4) == 0) {
        // Compile and execute C file or run binary (async non-blocking)
        const char *filename = command + 4;
        struct stat st;
        if (stat(filename, &st) != 0) {
            printf("❌ File not found: %s\n", filename);
        } else {
            // Check if it's a C file (for logging purposes)
            int is_c_file = (strlen(filename) > 2 && strcmp(filename + strlen(filename) - 2, ".c") == 0);
            TaskType task_type = TASK_BINARY;
            
            // Check if we should execute locally or delegate
            int load_percent = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;
            int is_high_load = (load_percent >= 70);

            if (is_high_load && peer_manager_get_connected_count() > 0) {
                // Create task for delegation
                Task task;
                memset(&task, 0, sizeof(task));
                task.task_id = rand() % 10000;
                task.type = task_type;
                task.hop_count = 0;
                
                strncpy(task.filename, filename, sizeof(task.filename) - 1);
                strncpy(task.command, filename, sizeof(task.command) - 1);
                
                strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
                task.source_port = my_port;
                
                printf("⚖️  Local load high (%d%%), delegating to peer...\n", load_percent);
                mesh_main_delegate_task_to_peer(&task);
            } else {
                // Execute locally using async fork (non-blocking)
                Task task;
                memset(&task, 0, sizeof(task));
                task.task_id = rand() % 10000;
                task.type = task_type;
                task.hop_count = 0;
                
                strncpy(task.filename, filename, sizeof(task.filename) - 1);
                strncpy(task.command, filename, sizeof(task.command) - 1);
                
                strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
                task.source_port = my_port;

                if (process_manager_execute_task(&task) == 0) {
                    if (is_c_file) {
                        printf("▶️  Compiling and executing: %s (Task ID: %d, async)\n", filename, task.task_id);
                    } else {
                        printf("▶️  Executing binary: %s (Task ID: %d, async)\n", filename, task.task_id);
                    }
                    printf("✅ Task submitted. Main loop continues accepting commands.\n");
                } else {
                    printf("⚠️ Local max capacity reached (%d/%d). Adding to queue...\n", 
                           worker_state.child_count, MAX_CONCURRENT_TASKS);
                    if (task_queue_enqueue(&task) < 0) {
                        printf("❌ System completely overwhelmed. Task rejected.\n");
                    }
                }
            }
        }

    } else if (strncmp(command, "exec ", 5) == 0) {
        // Execute shell command (async non-blocking)
        const char *cmd = command + 5;
        int load_percent = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;
        int is_high_load = (load_percent >= 70);

        if (is_high_load && peer_manager_get_connected_count() > 0) {
            // Create task for delegation
            Task task;
            memset(&task, 0, sizeof(task));
            task.task_id = rand() % 10000;
            task.type = TASK_EXEC;
            task.hop_count = 0;
            strncpy(task.command, cmd, sizeof(task.command) - 1);
            strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
            task.source_port = my_port;
            
            printf("⚖️  Local load high (%d%%), delegating to peer...\n", load_percent);
            mesh_main_delegate_task_to_peer(&task);
        } else {
            // Execute locally using async fork (non-blocking)
            Task task;
            memset(&task, 0, sizeof(task));
            task.task_id = rand() % 10000;
            task.type = TASK_EXEC;
            task.hop_count = 0;
            strncpy(task.command, cmd, sizeof(task.command) - 1);
            strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
            task.source_port = my_port;

            if (process_manager_execute_task(&task) == 0) {
                printf("▶️  Executing command: %s (Task ID: %d, async)\n", cmd, task.task_id);
                printf("✅ Task submitted. Main loop continues accepting commands.\n");
            } else {
                printf("⚠️ Local max capacity reached (%d/%d). Adding to queue...\n", 
                       worker_state.child_count, MAX_CONCURRENT_TASKS);
                if (task_queue_enqueue(&task) < 0) {
                    printf("❌ System completely overwhelmed. Task rejected.\n");
                }
            }
        }
    } else if (strcmp(command, "results") == 0) {
        // Display results queue
        if (result_queue_is_empty()) {
            printf("📋 No results in queue yet.\n");
        } else {
            result_queue_display_latest(10);
        }

    } else if (strncmp(command, "task ", 5) == 0) {
        // Execute sleep task: task <n>
        const char *arg = command + 5;
        int sleep_seconds = atoi(arg);

        if (sleep_seconds <= 0 || sleep_seconds > 3600) {
            printf("❌ Invalid sleep duration. Use task <1-3600>\n");
        } else {
            int load_percent = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;
            int is_high_load = (load_percent >= 70);

            if (is_high_load && peer_manager_get_connected_count() > 0) {
                // Create task for delegation
                Task task;
                memset(&task, 0, sizeof(task));
                task.task_id = rand() % 10000;
                task.type = TASK_SLEEP;
                task.hop_count = 0;
                snprintf(task.command, sizeof(task.command), "%d", sleep_seconds);
                strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
                task.source_port = my_port;
                
                printf("⚖️  Local load high (%d%%), delegating to peer...\n", load_percent);
                mesh_main_delegate_task_to_peer(&task);
            } else {
                // Execute locally using async fork (non-blocking)
                Task task;
                memset(&task, 0, sizeof(task));
                task.task_id = rand() % 10000;
                task.type = TASK_SLEEP;
                task.hop_count = 0;
                snprintf(task.command, sizeof(task.command), "%d", sleep_seconds);
                strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
                task.source_port = my_port;

                if (process_manager_execute_task(&task) == 0) {
                    printf("▶️  Executing sleep task %d: %d seconds (async)\n", task.task_id, sleep_seconds);
                    printf("✅ Task submitted. Main loop continues accepting commands.\n");
                } else {
                    printf("⚠️ Local max capacity reached (%d/%d). Adding to queue...\n", 
                           worker_state.child_count, MAX_CONCURRENT_TASKS);
                    if (task_queue_enqueue(&task) < 0) {
                        printf("❌ System completely overwhelmed. Task rejected.\n");
                    }
                }
            }
        }

    } else if (strcmp(command, "test_delegate") == 0) {
        // Force delegation test - create a task and delegate it regardless of load
        Task task;
        memset(&task, 0, sizeof(task));
        task.task_id = rand() % 10000;
        task.type = TASK_SLEEP;
        task.hop_count = 0;
        snprintf(task.command, sizeof(task.command), "%d", 2); // 2 second sleep
        strncpy(task.source_ip, my_ip, sizeof(task.source_ip) - 1);
        task.source_port = my_port;

        printf("🧪 Testing delegation: Task %d (sleep 2s)\n", task.task_id);
        mesh_main_delegate_task_to_peer(&task);

    } else {
        printf("❌ Unknown command: %s\n", command);
        printf("Type 'help' for available commands.\n");
    }
}

void mesh_main_handle_peer_message(int peer_sock, Message *msg) {
    log_event("PEER_MESSAGE", "Received type %d from peer", msg->type);

    switch (msg->type) {
        case MSG_LOAD_UPDATE:
            mesh_monitor_handle_load_update(msg->source_ip, msg->source_port,
                                          msg->load_percent, msg->queue_depth);
            break;

        case MSG_PEER_JOIN: {
            int is_new = (peer_manager_find_peer_index(msg->source_ip, msg->source_port) < 0);
            mesh_monitor_handle_peer_join(msg->source_ip, msg->source_port);
            
            // Peer Gossiping: if this is a newly discovered peer, broadcast their existence to everyone else
            // This builds a full mesh automatically from a single connection!
            if (is_new) {
                mesh_main_broadcast_message(msg);
            }
            break;
        }

        case MSG_TASK_ASSIGN: {
            // Handle incoming task assignment from peer
            log_event("TASK_RECEIVED", "Task %d assigned from %s:%d",
                     msg->task_id, msg->source_ip, msg->source_port);
            
            // Create Task struct from message with safety checks
            Task task;
            memset(&task, 0, sizeof(task));
            task.task_id = msg->task_id;
            task.type = msg->task_type;
            task.hop_count = 0;  // Reset hop count for local execution
            
            // Safely copy strings with null termination
            if (msg->source_ip[0]) {
                strncpy(task.source_ip, msg->source_ip, sizeof(task.source_ip) - 1);
                task.source_ip[sizeof(task.source_ip) - 1] = '\0';
            }
            task.source_port = msg->source_port;
            
            // Handle different task types with safety checks
            if (msg->task_type == TASK_BINARY) {
                // For binary tasks, command contains the filename
                if (msg->command[0]) {
                    strncpy(task.command, msg->command, sizeof(task.command) - 1);
                    task.command[sizeof(task.command) - 1] = '\0';
                    
                    // RECEIVE BINARY TASK PAYLOAD (The source .c file or binary)
                    BinaryTask bt;
                    if (recv_all(peer_sock, &bt, sizeof(BinaryTask)) <= 0) {
                        log_error("MESH_MAIN", "Failed to receive binary payload", "");
                        break;
                    }

                    // Write payload to a temporary local file so it can be compiled/run
                    char tmp_file[256];
                    snprintf(tmp_file, sizeof(tmp_file), "/tmp/mesh_incoming_%d.c", msg->task_id);
                    FILE *fp = fopen(tmp_file, "wb");
                    if (fp) {
                        fwrite(bt.binary_data, 1, bt.binary_size, fp);
                        fclose(fp);
                        
                        // Update task.filename to point to the local copy
                        strncpy(task.filename, tmp_file, sizeof(task.filename) - 1);
                        task.filename[sizeof(task.filename) - 1] = '\0';
                    } else {
                        log_error("MESH_MAIN", "Failed to write incoming binary file", tmp_file);
                        break;
                    }
                } else {
                    log_warning("MESH_MAIN", "Binary task received without filename");
                    break;
                }
            } else {
                // For other tasks, command is the command to execute
                if (msg->command[0]) {
                    strncpy(task.command, msg->command, sizeof(task.command) - 1);
                    task.command[sizeof(task.command) - 1] = '\0';
                } else {
                    log_warning("MESH_MAIN", "Task received without command");
                    break;
                }
            }

            // Execute the delegated task
            if (process_manager_execute_task(&task) == 0) {
                printf("▶️  Executing delegated task %d from %s:%d\n",
                       task.task_id, msg->source_ip, msg->source_port);
            } else {
                printf("❌ Cannot execute delegated task - max concurrent tasks reached\n");
            }
            break;
        }

        case MSG_TASK_RESULT:
            // Check if we are the originating source of this task
            if (strcmp(msg->source_ip, my_ip) == 0 && msg->source_port == my_port) {
                log_event("TASK_RESULT", "Result for task %d received natively. Task Complete.", msg->task_id);
                // Store result in local queue
                result_queue_enqueue(msg->task_id, msg->command, msg->output, 0, 1);
                printf("\n📨 [RESULT] Task %d completed successfully!\n%s\n", msg->task_id, msg->output);
            } else {
                // We are NOT the source, we just executed it. Route the result back to the TRUE source!
                log_event("TASK_RESULT", "Routing result for task %d back to source %s:%d",
                          msg->task_id, msg->source_ip, msg->source_port);
                
                int source_idx = peer_manager_find_peer_index(msg->source_ip, msg->source_port);
                if (source_idx >= 0) {
                    int source_sock = peer_manager_get_peer_socket(source_idx);
                    if (source_sock > 0) {
                        send(source_sock, msg, sizeof(Message), 0);
                        printf("🔄 Routed task %d result back to source %s:%d\n", 
                               msg->task_id, msg->source_ip, msg->source_port);
                    } else {
                        log_warning("MESH_MAIN", "Cannot route result: source socket disconnected!");
                        log_orphaned_result(msg->task_id, msg->output, msg->source_ip, msg->source_port);
                    }
                } else {
                    log_warning("MESH_MAIN", "Cannot route result: source peer not found in peer list!");
                    log_orphaned_result(msg->task_id, msg->output, msg->source_ip, msg->source_port);
                }
            }
            break;

        default:
            log_warning("MESH_MAIN", "Unknown message type received");
            break;
    }
}

void mesh_main_handle_discovery_message(int discovery_sock) {
    char buffer[256];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    ssize_t bytes_read = recvfrom(discovery_sock, buffer, sizeof(buffer) - 1, 0,
                                  (struct sockaddr*)&sender_addr, &sender_len);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        char sender_ip[16];
        inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));

        log_event("DISCOVERY", "Received from %s: %s", sender_ip, buffer);
        peer_manager_handle_discovery(buffer, sender_ip);
    }
}

void mesh_main_accept_peer_connection(int listen_sock) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock < 0) {
        log_error("MESH_MAIN", "Accept failed", strerror(errno));
        return;
    }

    char peer_ip[16];
    inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip));

    // Set receive timeout so recv_all doesn't hang the select loop 
    // if a phantom peer connects but doesn't send the full Message struct
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms timeout
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    log_event("PEER_CONNECTION", "Accepted connection from %s (awaiting identification)", peer_ip);

    // Store socket in pending list - will be claimed by MSG_PEER_JOIN handler
    if (pending_count < MAX_PENDING_CONNECTIONS) {
        pending_sockets[pending_count++] = client_sock;
    } else {
        log_warning("MESH_MAIN", "Too many pending connections, dropping");
        close(client_sock);
    }
}

void mesh_main_broadcast_message(Message *msg) {
    for (int i = 0; i < peer_manager_get_peer_count(); i++) {
        int peer_sock = peer_manager_get_peer_socket(i);
        if (peer_sock != -1) {
            send_all(peer_sock, msg, sizeof(Message));
        }
    }
}

void mesh_main_delegate_task_to_peer(Task *task) {
    // Find the peer with lowest load
    int best_peer_idx = -1;
    int lowest_load = 101;

    for (int i = 0; i < peer_manager_get_peer_count(); i++) {
        PeerInfo *peer = peer_manager_get_peer_info(i);
        if (peer && peer->is_alive && peer->load_percent < lowest_load) {
            lowest_load = peer->load_percent;
            best_peer_idx = i;
        }
    }

    if (best_peer_idx < 0) {
        printf("❌ No alive peers available for delegation\n");
        return;
    }

    PeerInfo *best_peer = peer_manager_get_peer_info(best_peer_idx);
    
    // Create task assignment message
    Message task_msg;
    memset(&task_msg, 0, sizeof(task_msg));
    task_msg.type = MSG_TASK_ASSIGN;
    task_msg.task_id = task->task_id;
    task_msg.task_type = task->type;
    task_msg.source_port = my_port;
    strcpy(task_msg.source_ip, my_ip);
    strncpy(task_msg.command, task->command, sizeof(task_msg.command) - 1);

    // Send task to best peer
    int peer_sock = peer_manager_get_peer_socket(best_peer_idx);
    if (peer_sock != -1) {
        if (send_all(peer_sock, &task_msg, sizeof(task_msg)) > 0) {
            
            // SEND BINARY TASK PAYLOAD IF NECESSARY
            if (task->type == TASK_BINARY) {
                BinaryTask bt;
                memset(&bt, 0, sizeof(bt));
                bt.task_id = task->task_id;
                
                FILE *fp = fopen(task->filename, "rb");
                if (fp) {
                    bt.binary_size = fread(bt.binary_data, 1, MAX_BINARY_SIZE, fp);
                    fclose(fp);
                    send_all(peer_sock, &bt, sizeof(BinaryTask));
                } else {
                    log_error("MESH_MAIN", "Failed to open local binary/source file", task->filename);
                }
            }

            printf("✅ Task %d delegated to %s:%d (load: %d%%)\n",
                   task->task_id, best_peer->ip, best_peer->port, best_peer->load_percent);
            log_event("TASK_DELEGATED", "Task %d -> %s:%d",
                     task->task_id, best_peer->ip, best_peer->port);
        } else {
            printf("❌ Failed to send task to peer %s:%d\n", best_peer->ip, best_peer->port);
        }
    } else {
        printf("❌ No socket connection to peer %s:%d\n", best_peer->ip, best_peer->port);
    }
}