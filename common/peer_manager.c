/* peer_manager.c — P2P Mesh peer connection and discovery management
 * Handles full mesh connections, peer discovery, and health monitoring
 */

#include "../include/common.h"
#include <fcntl.h>
#include <errno.h>

// Global worker state (shared across all mesh components)
WorkerState worker_state;

// Forward declarations
void peer_manager_broadcast_join();
void peer_manager_connect_to_peer(const char *ip, int port);
void peer_manager_handle_peer_join(const char *peer_ip, int peer_port);
int peer_manager_find_peer_index(const char *ip, int port);

// Initialize peer manager
void peer_manager_init(const char *my_ip, int my_port) {
    memset(&worker_state, 0, sizeof(WorkerState));
    strcpy(worker_state.my_ip, my_ip);
    worker_state.my_port = my_port;

    // Initialize peer sockets to -1 (disconnected)
    for (int i = 0; i < MAX_PEERS; i++) {
        worker_state.peers[i].socket_fd = -1;
        worker_state.peers[i].is_alive = 0;
    }

    // Initialize task queue
    worker_state.queue_head = 0;
    worker_state.queue_tail = 0;

    log_event("PEER_MANAGER", "Initialized for %s:%d", my_ip, my_port);
}

// Add a peer dynamically
int peer_manager_add_peer(const char *ip, int port) {
    // Check if we already have this peer
    if (peer_manager_find_peer_index(ip, port) >= 0) {
        return 0; // Already exists
    }

    // Check if we have space
    if (worker_state.peer_count >= MAX_PEERS) {
        log_event("PEER_MANAGER", "Maximum peers reached, cannot add %s:%d", ip, port);
        return -1;
    }

    // Don't add ourselves
    if (strcmp(ip, worker_state.my_ip) == 0 && port == worker_state.my_port) {
        return 0;
    }

    // Add peer to list
    int idx = worker_state.peer_count;
    strcpy(worker_state.peers[idx].ip, ip);
    worker_state.peers[idx].port = port;
    worker_state.peers[idx].socket_fd = -1;
    worker_state.peers[idx].is_alive = 0;
    worker_state.peers[idx].last_heartbeat = time(NULL);

    worker_state.peer_count++;

    log_event("PEER_MANAGER", "Added peer %s:%d dynamically", ip, port);
    return 0;
}

// Load peers from configuration file
int peer_manager_load_peers(const char *config_file) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        // File doesn't exist - this is OK, return 0 peers loaded
        log_event("PEER_MANAGER", "No peers.conf found - starting with empty mesh");
        return 0;
    }

    char line[256];
    int peer_count = 0;

    while (fgets(line, sizeof(line), fp) && peer_count < MAX_PEERS) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') continue;

        // Parse IP:PORT
        char *colon = strchr(line, ':');
        if (!colon) {
            log_warning("PEER_MANAGER", "Invalid peer format, skipping");
            continue;
        }

        *colon = '\0';
        char *ip = line;
        int port = atoi(colon + 1);

        // Don't add ourselves
        if (strcmp(ip, worker_state.my_ip) == 0 && port == worker_state.my_port) {
            continue;
        }

        // Add peer to list
        strcpy(worker_state.peers[peer_count].ip, ip);
        worker_state.peers[peer_count].port = port;
        worker_state.peers[peer_count].socket_fd = -1;
        worker_state.peers[peer_count].is_alive = 0;
        worker_state.peers[peer_count].last_heartbeat = time(NULL);

        peer_count++;
    }

    fclose(fp);
    worker_state.peer_count = peer_count;

    log_event("PEER_MANAGER", "Loaded %d peers from %s", peer_count, config_file);
    return peer_count;
}

// Connect to all known peers
void peer_manager_connect_to_all() {
    for (int i = 0; i < worker_state.peer_count; i++) {
        peer_manager_connect_to_peer(
            worker_state.peers[i].ip,
            worker_state.peers[i].port
        );
    }

    // Broadcast that we joined the mesh
    peer_manager_broadcast_join();
}

// Broadcast join message to all connected peers
void peer_manager_broadcast_join() {
    Message join_msg;
    memset(&join_msg, 0, sizeof(join_msg));
    join_msg.type = MSG_PEER_JOIN;
    strcpy(join_msg.source_ip, worker_state.my_ip);
    join_msg.source_port = worker_state.my_port;

    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].socket_fd != -1) {
            send_all(worker_state.peers[i].socket_fd, &join_msg, sizeof(join_msg));
        }
    }

    log_event("PEER_MANAGER", "Broadcasted JOIN message to mesh");
}

// Connect to a specific peer
void peer_manager_connect_to_peer(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_error("PEER_MANAGER", "Failed to create socket", strerror(errno));
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // Make socket non-blocking for a fast connect timeout
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    int result = connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    // If it's in progress, wait at most 1 second
    if (result < 0 && errno == EINPROGRESS) {
        struct timeval connect_tv;
        connect_tv.tv_sec = 1; // 1 second timeout for connecting to dead IPs
        connect_tv.tv_usec = 0;
        
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sockfd, &wset);
        
        if (select(sockfd + 1, NULL, &wset, NULL, &connect_tv) == 1) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                result = 0; // successfully connected!
            }
        }
    }

    // Restore blocking mode for standard operations
    fcntl(sockfd, F_SETFL, flags);

    // Set receive timeout to prevent recv_all freezing the entire event loop if a peer drops
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    if (result < 0) {
        log_network_event("CONNECT_FAILED", ip, port, "Timeout or Refusal");
        close(sockfd);
        return;
    }

    // Find peer index and update
    int peer_idx = peer_manager_find_peer_index(ip, port);
    if (peer_idx >= 0) {
        worker_state.peers[peer_idx].socket_fd = sockfd;
        worker_state.peers[peer_idx].is_alive = 1;
        worker_state.peers[peer_idx].last_heartbeat = time(NULL);

        log_network_event("CONNECTED", ip, port, "outgoing connection established");

        // Send join handshake so the remote peer can register us correctly
        Message join_msg;
        memset(&join_msg, 0, sizeof(join_msg));
        join_msg.type = MSG_PEER_JOIN;
        strcpy(join_msg.source_ip, worker_state.my_ip);
        join_msg.source_port = worker_state.my_port;
        send_all(sockfd, &join_msg, sizeof(join_msg));
    } else {
        close(sockfd);
    }
}

// Broadcast discovery message to find peers on local network
void peer_manager_broadcast_discovery() {
    int broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_sock < 0) {
        log_error("PEER_MANAGER", "Failed to create broadcast socket", strerror(errno));
        return;
    }

    // Enable broadcast
    int broadcast_enable = 1;
    setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(8081); // Use different port for discovery
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // Broadcast to all

    // Send discovery message
    char discovery_msg[64];
    snprintf(discovery_msg, sizeof(discovery_msg), "MESH_DISCOVER:%s:%d",
             worker_state.my_ip, worker_state.my_port);

    sendto(broadcast_sock, discovery_msg, strlen(discovery_msg), 0,
           (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    close(broadcast_sock);
    log_event("PEER_MANAGER", "Broadcasted discovery message");
}

// Handle incoming discovery message
void peer_manager_handle_discovery(const char *message, const char *sender_ip) {
    // Format: "MESH_DISCOVER:IP:PORT"
    if (strncmp(message, "MESH_DISCOVER:", 14) == 0) {
        char peer_ip[16];
        int peer_port;

        if (sscanf(message + 14, "%15[^:]:%d", peer_ip, &peer_port) == 2) {
            // Don't add ourselves
            if (strcmp(peer_ip, worker_state.my_ip) == 0 && peer_port == worker_state.my_port) {
                return;
            }

            // Add the discovered peer
            if (peer_manager_add_peer(peer_ip, peer_port) == 0) {
                log_event("PEER_MANAGER", "Discovered peer %s:%d via broadcast", peer_ip, peer_port);
                // Try to connect
                peer_manager_connect_to_peer(peer_ip, peer_port);
            }
        }
    }
}

// Handle incoming peer join message
void peer_manager_handle_peer_join(const char *peer_ip, int peer_port) {
    // SECURITY/ROUTING FIX: Never add ourselves to the routing table via reflected Gossip messages!
    if (strcmp(peer_ip, worker_state.my_ip) == 0 && peer_port == worker_state.my_port) {
        return;
    }

    int peer_idx = peer_manager_find_peer_index(peer_ip, peer_port);

    if (peer_idx < 0) {
        // Try to find by IP only, in case we accepted a connection with an ephemeral port
        for (int i = 0; i < worker_state.peer_count; i++) {
            if (strcmp(worker_state.peers[i].ip, peer_ip) == 0) {
                // Update port to the real listening port
                worker_state.peers[i].port = peer_port;
                peer_idx = i;
                break;
            }
        }
    }

    if (peer_idx >= 0) {
        // Known peer - update status
        worker_state.peers[peer_idx].is_alive = 1;
        worker_state.peers[peer_idx].last_heartbeat = time(NULL);
        log_peer_event("PEER_RECONNECTED", peer_ip, peer_port);
    } else if (worker_state.peer_count < MAX_PEERS) {
        // New peer - add to list
        strcpy(worker_state.peers[worker_state.peer_count].ip, peer_ip);
        worker_state.peers[worker_state.peer_count].port = peer_port;
        worker_state.peers[worker_state.peer_count].socket_fd = -1; // Will be set by accept handler
        worker_state.peers[worker_state.peer_count].is_alive = 1;
        worker_state.peers[worker_state.peer_count].last_heartbeat = time(NULL);
        worker_state.peer_count++;

        log_peer_event("PEER_DISCOVERED", peer_ip, peer_port);
    }
}

// Find peer index by IP:port
int peer_manager_find_peer_index(const char *ip, int port) {
    for (int i = 0; i < worker_state.peer_count; i++) {
        if (strcmp(worker_state.peers[i].ip, ip) == 0 &&
            worker_state.peers[i].port == port) {
            return i;
        }
    }
    return -1;
}

// Get best available peer (for load balancing)
int peer_manager_get_best_peer(int *visited_peers) {
    int best_peer = -1;
    int best_load = 100;

    for (int i = 0; i < worker_state.peer_count; i++) {
        PeerInfo *peer = &worker_state.peers[i];

        // Skip if not alive or already visited
        if (!peer->is_alive || (visited_peers && visited_peers[i])) {
            continue;
        }

        // Skip if load too high
        if (peer->load_percent >= 50) {
            continue;
        }

        // Find lowest load
        if (peer->load_percent < best_load) {
            best_load = peer->load_percent;
            best_peer = i;
        }
    }

    return best_peer;
}

// Update peer load information
void peer_manager_update_peer_load(const char *peer_ip, int peer_port, int load_percent) {
    int peer_idx = peer_manager_find_peer_index(peer_ip, peer_port);
    if (peer_idx >= 0) {
        worker_state.peers[peer_idx].load_percent = load_percent;
        worker_state.peers[peer_idx].last_load_update = time(NULL);
    }
}

// Check for dead peers and mark them
void peer_manager_check_peer_health() {
    time_t now = time(NULL);

    for (int i = 0; i < worker_state.peer_count; i++) {
        PeerInfo *peer = &worker_state.peers[i];

        // If we have a socket but haven't heard from peer recently
        if (peer->socket_fd != -1 &&
            difftime(now, peer->last_heartbeat) > 10) { // 10 second timeout

            // Mark as dead
            peer->is_alive = 0;
            close(peer->socket_fd);
            peer->socket_fd = -1;

            log_peer_event("PEER_DEAD", peer->ip, peer->port);
        }
    }
}

// Broadcast load update to all peers
void peer_manager_broadcast_load(int my_load) {
    worker_state.my_load_percent = my_load;

    Message load_msg;
    memset(&load_msg, 0, sizeof(load_msg));
    load_msg.type = MSG_LOAD_UPDATE;
    load_msg.load_percent = my_load;
    strcpy(load_msg.source_ip, worker_state.my_ip);
    load_msg.source_port = worker_state.my_port;

    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].socket_fd != -1) {
            send_all(worker_state.peers[i].socket_fd, &load_msg, sizeof(load_msg));
        }
    }
}

// Get peer socket for communication
int peer_manager_get_peer_socket(int peer_idx) {
    if (peer_idx >= 0 && peer_idx < worker_state.peer_count) {
        return worker_state.peers[peer_idx].socket_fd;
    }
    return -1;
}

// Get peer information
PeerInfo* peer_manager_get_peer_info(int peer_idx) {
    if (peer_idx >= 0 && peer_idx < worker_state.peer_count) {
        return &worker_state.peers[peer_idx];
    }
    return NULL;
}

// Get total peer count
int peer_manager_get_peer_count() {
    return worker_state.peer_count;
}

// Get connected peer count
int peer_manager_get_connected_count() {
    int connected = 0;
    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].is_alive) connected++;
    }
    return connected;
}

// Cleanup peer manager
void peer_manager_cleanup() {
    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].socket_fd != -1) {
            close(worker_state.peers[i].socket_fd);
            worker_state.peers[i].socket_fd = -1;
        }
    }

    log_event("PEER_MANAGER", "Cleanup completed");
}