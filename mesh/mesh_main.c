/* mesh_main.c — Main entry point for P2P mesh worker
 * Coordinates all mesh components and handles the main event loop
 */

#include "../include/common.h"
#include "../include/mesh_http.h"
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>

// ANSI color codes for terminal UI
#define C_RST "\033[0m"
#define C_CYD "\033[1;36m"
#define C_GRN "\033[1;32m"
#define C_RED "\033[1;31m"
#define C_YEL "\033[1;33m"
#define C_MAG "\033[1;35m"

#include <sys/select.h>

// External reference to worker state
extern WorkerState worker_state;

// Function declarations
void mesh_main_show_help();
void mesh_main_show_status();
void mesh_main_process_user_command(char *command);
void mesh_main_handle_peer_message(int peer_sock, Message *msg);
void mesh_main_accept_peer_connection(int listen_sock);
void mesh_main_handle_discovery_message(int discovery_sock);
void mesh_main_broadcast_message(Message *msg);
int mesh_main_delegate_task_to_peer(Task *task);

// Global state
static int listen_socket = -1;
static int discovery_socket = -1; // NEW: UDP socket for peer discovery
static int http_listen_socket = -1; // NEW: HTTP dashboard server
static char my_ip[16] = "127.0.0.1"; // Default to localhost
static int my_port = PORT;

// Auto-detect LAN IP by enumerating network interfaces via getifaddrs().
// Strictly picks the first non-loopback, UP, IPv4 (AF_INET) interface,
// skipping virtual adapters like docker0, veth*, br-*.
#include <ifaddrs.h>
#include <net/if.h>
static void auto_detect_lan_ip(char *ip_buf, size_t buf_len) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        // Strictly IPv4 only
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        // Must be UP and RUNNING
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;

        // Skip loopback (lo)
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        // Skip common virtual/container interfaces
        if (strncmp(ifa->ifa_name, "docker", 6) == 0 ||
            strncmp(ifa->ifa_name, "veth",   4) == 0 ||
            strncmp(ifa->ifa_name, "br-",    3) == 0 ||
            strncmp(ifa->ifa_name, "virbr",  5) == 0) continue;

        // Found a real LAN interface — extract its IPv4 address
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ip_buf, buf_len);
        break;
    }

    freeifaddrs(ifaddr);
}

// Pending incoming connections (waiting for MSG_PEER_JOIN to identify real port)
#define MAX_PENDING_CONNECTIONS 10
static int pending_sockets[MAX_PENDING_CONNECTIONS];
static int pending_count = 0;

int main(int argc, char *argv[]) {
    // Auto-detect LAN IP before parsing args (so -i can override)
    auto_detect_lan_ip(my_ip, sizeof(my_ip));

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

    printf("\n" C_MAG
           "╔══════════════════════════════════════════╗\n"
           "║       P2P MESH DISTRIBUTED WORKER        ║\n"
           "╚══════════════════════════════════════════╝\n\n" C_RST);
    printf("  " C_MAG "IP  " C_RST ": " C_YEL "%s\n" C_RST, my_ip);
    printf("  " C_MAG "Port" C_RST ": " C_YEL "%d\n\n" C_RST, my_port);
    printf("  Use " C_GRN "'h'" C_RST " for help | " C_RED "'q'" C_RST " to quit\n\n");

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
        printf(C_YEL "⚠️  No peers.conf found - starting in empty mesh mode\n" C_RST);
        printf("   Peers will auto-discover each other. Use " C_CYD "'discover'" C_RST " to broadcast.\n\n");
    } else {
        printf(C_GRN "✅ Loaded %d peers from peers.conf\n\n" C_RST, loaded_peers);
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

    printf(C_GRN "✅ Listening on " C_YEL "%s:%d\n" C_RST, my_ip, my_port);

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
            printf(C_GRN "✅ Discovery listening on " C_YEL "UDP port 8081\n" C_RST);
        } else {
            log_warning("MESH_MAIN", "Failed to bind discovery socket");
            close(discovery_socket);
            discovery_socket = -1;
        }
    }

    // Connect to known peers
    peer_manager_connect_to_all();
    
    // Initialize Web Dashboard HTTP Server
    int http_port = my_port + 1000;
    http_listen_socket = mesh_http_init(http_port);
    if (http_listen_socket >= 0) {
        printf(C_GRN "✅ Web Dashboard running at " C_YEL "http://%s:%d\n" C_RST, my_ip, http_port);
    } else {
        log_warning("MESH_MAIN", "Failed to start HTTP server");
    }

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

    printf(C_GRN "✅ Mesh worker ready. Type commands or 'help' for assistance.\n\n" C_RST);
    printf(C_CYD "mesh> " C_RST);
    fflush(stdout);

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
        
        // Add HTTP socket
        if (http_listen_socket != -1) {
            FD_SET(http_listen_socket, &read_fds);
            if (http_listen_socket > max_fd) max_fd = http_listen_socket;
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
        
        // Check for HTTP dashboard requests
        if (http_listen_socket != -1 && FD_ISSET(http_listen_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int http_client = accept(http_listen_socket, (struct sockaddr *)&client_addr, &client_len);
            if (http_client >= 0) {
                mesh_http_handle_client(http_client);
            }
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
                printf(C_CYD "mesh> " C_RST);
                fflush(stdout);
            }
        }

        // Periodic maintenance
        mesh_monitor_update();
    }

cleanup:
    // Cleanup
    printf("\n" C_RED "🛑 Shutting down mesh worker...\n" C_RST);

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

    printf(C_GRN "✅ Shutdown complete.\n" C_RST);
    return 0;
}

void mesh_main_show_help() {
    printf(C_MAG "P2P Mesh Worker - Distributed Computing System\n\n" C_RST);
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

    // Build ASCII load bar (10 chars, always fixed width - no unicode ambiguity)
    char load_bar[11];
    int filled = worker_state.my_load_percent / 10;
    for (int b = 0; b < 10; b++) load_bar[b] = (b < filled) ? '=' : '-';
    load_bar[10] = '\0';

    // ── Title block: fixed string literals so box always has correct width ──
    printf("\n" C_MAG
           "╔══════════════════════════════════════════╗\n"
           "║         MESH STATUS DASHBOARD            ║\n"
           "╚══════════════════════════════════════════╝\n\n" C_RST);

    printf("  " C_MAG "Node   " C_RST ": " C_YEL "%s" C_RST ":" C_YEL "%d\n" C_RST,
           my_ip, my_port);
    printf("  " C_MAG "Load   " C_RST ": [" C_GRN "%-10s" C_RST "] " C_YEL "%d%%\n" C_RST,
           load_bar, worker_state.my_load_percent);
    printf("  " C_MAG "Tasks  " C_RST ": " C_GRN "%d" C_RST " active / " C_YEL "%d" C_RST " max\n",
           active_children, max_children);
    printf("  " C_MAG "Peers  " C_RST ": " C_GRN "%d" C_RST " connected / " C_YEL "%d" C_RST " total\n",
           connected_peers, total_peers);
    printf("  " C_MAG "Queue  " C_RST ": " C_CYD "%d" C_RST " tasks   "
           C_MAG "Results" C_RST ": " C_GRN "%d\n\n" C_RST,
           task_queue_get_depth(), result_queue_get_depth());

    // ── Peer section ──
    printf(C_CYD "  ┌─ " C_MAG "PEER STATUS" C_CYD
           " ──────────────────────────────────────────────┐\n" C_RST);

    if (peer_manager_get_peer_count() == 0) {
        printf(C_CYD "  │  " C_RST "(no peers connected)\n");
    }
    for (int i = 0; i < peer_manager_get_peer_count(); i++) {
        PeerInfo *peer = peer_manager_get_peer_info(i);
        if (peer) {
            char peer_addr[32];
            snprintf(peer_addr, sizeof(peer_addr), "%s:%d", peer->ip, peer->port);
            // Color passed as %s (zero display width); plain "ALIVE"/"DEAD " goes to %-5s
            printf(C_CYD "  │  " C_RST "> " C_CYD "%-22s" C_RST
                   " [%s%-5s" C_RST "]"
                   "  Load: " C_YEL "%3d%%" C_RST "  Queue: %d\n",
                   peer_addr,
                   peer->is_alive ? C_GRN : C_RED,
                   peer->is_alive ? "ALIVE" : "DEAD ",
                   peer->load_percent,
                   peer->queue_depth);
        }
    }
    printf(C_CYD "  └──────────────────────────────────────────────────────────┘\n\n");

    printf(C_MAG "  RECENT ACTIVITY  " C_RST
           "(see events.log for full detail)\n\n" C_RST);
}

void mesh_main_process_user_command(char *command) {
    if (strlen(command) == 0) return;

    log_event("USER_COMMAND", "Received: %s", command);

    if (strcmp(command, "help") == 0 || strcmp(command, "h") == 0) {
        printf("\n" C_MAG "╔══════════════════════════════════════════╗\n");
        printf("║         P2P MESH - HELP COMMANDS         ║\n");
        printf("╚══════════════════════════════════════════╝\n" C_RST);
        printf(C_MAG "  Mesh Info:\n" C_RST);
        printf("    " C_CYD "status" C_RST "          Show mesh dashboard\n");
        printf("    " C_CYD "peers" C_RST "           List all peers + status\n");
        printf("    " C_CYD "load" C_RST "            Show load & queue metrics\n");
        printf("    " C_CYD "queue" C_RST "           Show pending task queue\n");
        printf("    " C_CYD "results" C_RST "         Show completed task results\n");
        printf(C_MAG "  Task Execution:\n" C_RST);
        printf("    " C_CYD "task " C_YEL "<n>" C_RST "         Sleep task for n seconds\n");
        printf("    " C_CYD "exec " C_YEL "<cmd>" C_RST "       Run a shell command\n");
        printf("    " C_CYD "run "  C_YEL "<file>" C_RST "      Compile & run a C file\n");
        printf(C_MAG "  Networking:\n" C_RST);
        printf("    " C_CYD "discover" C_RST "        UDP broadcast to find peers\n");
        printf("    " C_CYD "test_delegate" C_RST "   Force a task delegation test\n");
        printf(C_MAG "  Misc:\n" C_RST);
        printf("    " C_CYD "help" C_RST "            Show this help\n");
        printf("    " C_CYD "q" C_RST "/" C_CYD "quit" C_RST "          Exit the mesh node\n");
        printf("\n");

    } else if (strcmp(command, "status") == 0) {
        mesh_main_show_status();

    } else if (strcmp(command, "discover") == 0) {
        printf(C_CYD "🔍 Broadcasting discovery message...\n" C_RST);
        peer_manager_broadcast_discovery();
        printf(C_GRN "✅ Discovery broadcast sent. Other peers should find you.\n" C_RST);
        printf("\n" C_MAG "Peer Information:\n" C_RST);
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            PeerInfo *peer = peer_manager_get_peer_info(i);
            if (peer) {
                const char* conn_str = peer->is_alive ? (C_GRN "CONNECTED   " C_RST) : (C_RED "DISCONNECTED" C_RST);
                printf("  " C_CYD "%s:%d" C_RST " - %s (Load: " C_YEL "%d%%" C_RST ", Queue: %d)\n",
                       peer->ip, peer->port, conn_str,
                       peer->load_percent, peer->queue_depth);
            }
        }
        printf("\n");

    } else if (strcmp(command, "peers") == 0) {
        printf(C_MAG "Peer information:\n" C_RST);
        for (int i = 0; i < peer_manager_get_peer_count(); i++) {
            PeerInfo *peer = peer_manager_get_peer_info(i);
            if (peer) {
                const char* conn_str = peer->is_alive ? (C_GRN "CONNECTED   " C_RST) : (C_RED "DISCONNECTED" C_RST);
                printf("  " C_CYD "%s:%d" C_RST " - %s | Load: " C_YEL "%d%%" C_RST " | Queue: %d\n",
                       peer->ip, peer->port, conn_str,
                       peer->load_percent, peer->queue_depth);
            }
        }
        printf("\n");

    } else if (strcmp(command, "queue") == 0) {
        task_queue_print_status();

    } else if (strcmp(command, "load") == 0) {
        int active_children, max_children;
        process_manager_get_stats(&active_children, &max_children);
        
        printf(C_MAG "📊 Load Information:\n" C_RST);
        printf("  Current load: " C_YEL "%d%%\n" C_RST, worker_state.my_load_percent);
        printf("  Active tasks: " C_CYD "%d/%d\n" C_RST, active_children, max_children);
        printf("  Queue depth: " C_CYD "%d/%d\n" C_RST, task_queue_get_depth(), MAX_QUEUE);
        printf("  Results stored: " C_GRN "%d\n" C_RST, result_queue_get_depth());
        printf("  Peers connected: " C_GRN "%d/%d\n" C_RST, peer_manager_get_connected_count(), peer_manager_get_peer_count());
        printf("\n");

    } else if (strncmp(command, "run ", 4) == 0) {
        // Compile and execute C file or run binary (async non-blocking)
        const char *filename = command + 4;
        struct stat st;
        if (stat(filename, &st) != 0) {
            printf(C_RED "❌ File not found: %s\n" C_RST, filename);
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
                
                printf(C_YEL "⚖️  Local load high (%d%%), attempting to delegate...\n" C_RST, load_percent);
                if (mesh_main_delegate_task_to_peer(&task) < 0) {
                    printf(C_YEL "⚠️ Mesh overwhelmed. Adding to local queue...\n" C_RST);
                    if (task_queue_enqueue(&task) < 0) {
                        printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
                    }
                }
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
                        printf(C_GRN "▶️  Compiling and executing" C_RST ": %s (Task ID: " C_CYD "%d" C_RST ", async)\n", filename, task.task_id);
                    } else {
                        printf(C_GRN "▶️  Executing binary" C_RST ": %s (Task ID: " C_CYD "%d" C_RST ", async)\n", filename, task.task_id);
                    }
                    printf(C_GRN "✅ Task submitted. Main loop continues accepting commands.\n" C_RST);
                } else {
                    printf(C_YEL "⚠️ Local max capacity reached (%d/%d). Adding to queue...\n" C_RST, 
                           worker_state.child_count, MAX_CONCURRENT_TASKS);
                    if (task_queue_enqueue(&task) < 0) {
                        printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
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
            
            printf(C_YEL "⚖️  Local load high (%d%%), attempting to delegate...\n" C_RST, load_percent);
            if (mesh_main_delegate_task_to_peer(&task) < 0) {
                printf(C_YEL "⚠️ Mesh overwhelmed. Adding to local queue...\n" C_RST);
                if (task_queue_enqueue(&task) < 0) {
                    printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
                }
            }
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
                printf(C_GRN "▶️  Executing command" C_RST ": " C_CYD "%s" C_RST " (Task ID: " C_YEL "%d" C_RST ", async)\n", cmd, task.task_id);
                printf(C_GRN "✅ Task submitted. Main loop continues accepting commands.\n" C_RST);
            } else {
                printf(C_YEL "⚠️ Local max capacity reached (%d/%d). Adding to queue...\n" C_RST, 
                       worker_state.child_count, MAX_CONCURRENT_TASKS);
                if (task_queue_enqueue(&task) < 0) {
                    printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
                }
            }
        }
    } else if (strcmp(command, "results") == 0) {
        // Display results queue
        if (result_queue_is_empty()) {
            printf(C_YEL "📋 No results in queue yet.\n" C_RST);
        } else {
            printf(C_MAG "\n══ Task Results ══\n" C_RST);
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
                
                printf(C_YEL "⚖️  Local load high (%d%%), attempting to delegate...\n" C_RST, load_percent);
                if (mesh_main_delegate_task_to_peer(&task) < 0) {
                    printf(C_YEL "⚠️ Mesh overwhelmed. Adding to local queue...\n" C_RST);
                    if (task_queue_enqueue(&task) < 0) {
                        printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
                    }
                }
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
                    printf(C_GRN "▶️  Executing sleep task " C_YEL "%d" C_GRN ": " C_YEL "%d" C_GRN " seconds (async)\n" C_RST, task.task_id, sleep_seconds);
                    printf(C_GRN "✅ Task submitted. Main loop continues accepting commands.\n" C_RST);
                } else {
                    printf(C_YEL "⚠️ Local max capacity reached (%d/%d). Adding to queue...\n" C_RST, 
                           worker_state.child_count, MAX_CONCURRENT_TASKS);
                    if (task_queue_enqueue(&task) < 0) {
                        printf(C_RED "❌ System completely overwhelmed. Task rejected.\n" C_RST);
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

        printf(C_CYD "🧪 Testing delegation: Task " C_YEL "%d" C_CYD " (sleep 2s)\n" C_RST, task.task_id);
        mesh_main_delegate_task_to_peer(&task);

    } else {
        printf(C_RED "❌ Unknown command: " C_YEL "'%s'" C_RED "\n" C_RST, command);
        printf("   Type " C_CYD "'help'" C_RST " to see available commands.\n");
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
                printf(C_CYD "▶️  Executing delegated task " C_YEL "%d" C_CYD " from " C_RST "%s:%d\n",
                       task.task_id, msg->source_ip, msg->source_port);
            } else {
                printf(C_RED "❌ Cannot execute delegated task - max concurrent tasks reached\n" C_RST);
            }
            break;
        }

        case MSG_TASK_RESULT:
            // Check if we are the originating source of this task
            if (strcmp(msg->source_ip, my_ip) == 0 && msg->source_port == my_port) {
                log_event("TASK_RESULT", "Result for task %d received natively. Task Complete.", msg->task_id);
                // Store result in local queue
                result_queue_enqueue(msg->task_id, msg->command, msg->output, 0, 1);
                printf("\n" C_GRN "📨 [RESULT] Task " C_CYD "%d" C_GRN " completed successfully!\n" C_RST "%s\n", msg->task_id, msg->output);
            } else {
                // We are NOT the source, we just executed it. Route the result back to the TRUE source!
                log_event("TASK_RESULT", "Routing result for task %d back to source %s:%d",
                          msg->task_id, msg->source_ip, msg->source_port);
                
                int source_idx = peer_manager_find_peer_index(msg->source_ip, msg->source_port);
                if (source_idx >= 0) {
                    int source_sock = peer_manager_get_peer_socket(source_idx);
                    if (source_sock > 0) {
                        send_all(source_sock, msg, sizeof(Message));
                        printf(C_MAG "🔄 Routed task " C_YEL "%d" C_MAG " result back to source " C_RST "%s:%d\n", 
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

int mesh_main_delegate_task_to_peer(Task *task) {
    // Find the peer with lowest load
    int best_peer_idx = -1;
    int lowest_load = 70; // Only delegate to peers strictly under 70% load

    for (int i = 0; i < peer_manager_get_peer_count(); i++) {
        PeerInfo *peer = peer_manager_get_peer_info(i);
        if (peer && peer->is_alive && peer->load_percent < lowest_load) {
            lowest_load = peer->load_percent;
            best_peer_idx = i;
        }
    }

    if (best_peer_idx < 0) {
        return -1; // Indicate failure to delegate so caller can queue it
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
            return 0;
        } else {
            printf("❌ Failed to send task to peer %s:%d\n", best_peer->ip, best_peer->port);
            return -1;
        }
    } else {
        printf("❌ No socket connection to peer %s:%d\n", best_peer->ip, best_peer->port);
        return -1;
    }
}