/* mesh_monitor.c — Mesh monitoring and load broadcasting
 * Handles periodic load updates and peer health monitoring
 */

#include "../include/common.h"
#include <unistd.h>

// External references
extern WorkerState worker_state;

// Monitoring state
static int monitoring_active = 0;
static time_t last_load_broadcast = 0;
static time_t last_health_check = 0;
static time_t last_discovery_broadcast = 0;  // NEW: periodic discovery
static time_t last_reconnect_attempt = 0;   // NEW: periodic reconnect

// Initialize mesh monitor
void mesh_monitor_init() {
    monitoring_active = 1;
    last_load_broadcast = time(NULL);
    last_health_check = time(NULL);
    last_discovery_broadcast = time(NULL);  // NEW: start discovery immediately
    last_reconnect_attempt = time(NULL);    // NEW: start reconnect immediately

    log_event("MESH_MONITOR", "Initialized monitoring system");
}

// Main monitoring loop (call this periodically)
void mesh_monitor_update() {
    if (!monitoring_active) return;

    time_t now = time(NULL);

    // Broadcast load every 3 seconds
    if (difftime(now, last_load_broadcast) >= 3.0) {
        mesh_monitor_broadcast_load_update();
        last_load_broadcast = now;
    }

    // Check peer health every 5 seconds
    if (difftime(now, last_health_check) >= 5.0) {
        mesh_monitor_check_peer_health();
        last_health_check = now;
    }

    // Broadcast discovery every 10 seconds so new peers can join
    if (difftime(now, last_discovery_broadcast) >= 10.0) {
        peer_manager_broadcast_discovery();
        last_discovery_broadcast = now;
    }

    // Retry failed peer connections every 6 seconds
    if (difftime(now, last_reconnect_attempt) >= 6.0) {
        mesh_monitor_retry_failed_connections();
        last_reconnect_attempt = now;
    }

    // Check task queue for processing opportunities
    task_queue_check_and_process();
}


// Broadcast current load to all peers
void mesh_monitor_broadcast_load_update() {
    // Get current load (this would come from load_monitor in real implementation)
    // For now, simulate based on child process count
    int current_load = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;

    // Ensure load doesn't exceed 100%
    if (current_load > 100) current_load = 100;

    // Update our own load
    worker_state.my_load_percent = current_load;

    // Create load update message
    Message load_msg;
    memset(&load_msg, 0, sizeof(load_msg));
    load_msg.type = MSG_LOAD_UPDATE;
    load_msg.load_percent = current_load;
    load_msg.queue_depth = task_queue_get_depth();
    strcpy(load_msg.source_ip, worker_state.my_ip);
    load_msg.source_port = worker_state.my_port;

    // Send to all connected peers
    int sent_count = 0;
    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].socket_fd != -1 && worker_state.peers[i].is_alive) {
            if (send_all(worker_state.peers[i].socket_fd, &load_msg, sizeof(load_msg)) > 0) {
                sent_count++;
            }
        }
    }

    log_event("MESH_MONITOR", "Broadcasted load %d%% to %d/%d peers",
             current_load, sent_count, peer_manager_get_connected_count());
}

// Check health of all peers
void mesh_monitor_check_peer_health() {
    time_t now = time(NULL);
    int dead_count = 0;

    for (int i = 0; i < worker_state.peer_count; i++) {
        PeerInfo *peer = &worker_state.peers[i];

        // Check if peer is still alive
        if (peer->is_alive && peer->socket_fd != -1) {
            // If we haven't heard from this peer in 10 seconds, mark as dead
            if (difftime(now, peer->last_heartbeat) > 10.0) {
                mesh_monitor_mark_peer_dead(i);
                dead_count++;
            }
        }
    }

    if (dead_count > 0) {
        log_event("MESH_MONITOR", "Marked %d peers as dead during health check", dead_count);
    }
}

// Mark a peer as dead
void mesh_monitor_mark_peer_dead(int peer_idx) {
    if (peer_idx < 0 || peer_idx >= worker_state.peer_count) return;

    PeerInfo *peer = &worker_state.peers[peer_idx];

    if (peer->is_alive) {
        peer->is_alive = 0;

        if (peer->socket_fd != -1) {
            close(peer->socket_fd);
            peer->socket_fd = -1;
        }

        log_peer_event("PEER_DEAD", peer->ip, peer->port);

        // TODO: Trigger task redistribution for tasks that were sent to this peer
        // This would require tracking which tasks were sent to which peers
    }
}

// Handle incoming load update from peer
void mesh_monitor_handle_load_update(const char *peer_ip, int peer_port, int load_percent, int queue_depth) {
    int peer_idx = peer_manager_find_peer_index(peer_ip, peer_port);
    if (peer_idx >= 0) {
        worker_state.peers[peer_idx].load_percent = load_percent;
        worker_state.peers[peer_idx].queue_depth = queue_depth;
        worker_state.peers[peer_idx].last_load_update = time(NULL);
        worker_state.peers[peer_idx].last_heartbeat = time(NULL);

        // Mark peer as alive if it wasn't
        if (!worker_state.peers[peer_idx].is_alive) {
            worker_state.peers[peer_idx].is_alive = 1;
            log_peer_event("PEER_ALIVE", peer_ip, peer_port);
        }
    }
}

// Handle peer join notification
void mesh_monitor_handle_peer_join(const char *peer_ip, int peer_port) {
    peer_manager_handle_peer_join(peer_ip, peer_port);
}

// Retry connections to dead peers
void mesh_monitor_retry_failed_connections() {
    for (int i = 0; i < worker_state.peer_count; i++) {
        PeerInfo *peer = &worker_state.peers[i];

        // If peer is dead or disconnected, try to reconnect
        if (!peer->is_alive || peer->socket_fd == -1) {
            log_event("MESH_MONITOR", "Retrying connection to %s:%d", peer->ip, peer->port);
            peer_manager_connect_to_peer(peer->ip, peer->port);
        }
    }
}

// Get mesh statistics
void mesh_monitor_get_stats(int *total_peers, int *connected_peers, int *avg_load, int *total_queue_depth) {
    *total_peers = worker_state.peer_count;
    *connected_peers = peer_manager_get_connected_count();

    int total_load = 0;
    int connected_count = 0;
    *total_queue_depth = 0;

    for (int i = 0; i < worker_state.peer_count; i++) {
        if (worker_state.peers[i].is_alive) {
            total_load += worker_state.peers[i].load_percent;
            *total_queue_depth += worker_state.peers[i].queue_depth;
            connected_count++;
        }
    }

    if (connected_count > 0) {
        *avg_load = total_load / connected_count;
    } else {
        *avg_load = 0;
    }
}

// Print mesh status (for debugging)
void mesh_monitor_print_status() {
    int total_peers, connected_peers, avg_load, total_queue_depth;
    mesh_monitor_get_stats(&total_peers, &connected_peers, &avg_load, &total_queue_depth);

    printf("\n=== MESH STATUS ===\n");
    printf("Peers: %d/%d connected\n", connected_peers, total_peers);
    printf("Average load: %d%%\n", avg_load);
    printf("Total queued tasks: %d\n", total_queue_depth);
    printf("Local load: %d%%\n", worker_state.my_load_percent);
    printf("Local queue: %d tasks\n", task_queue_get_depth());
    printf("Active children: %d/%d\n", worker_state.child_count, MAX_CONCURRENT_TASKS);

    printf("\nPeer Details:\n");
    for (int i = 0; i < worker_state.peer_count; i++) {
        PeerInfo *peer = &worker_state.peers[i];
        printf("  %s:%d - %s, Load: %d%%, Queue: %d\n",
               peer->ip, peer->port,
               peer->is_alive ? "ALIVE" : "DEAD",
               peer->load_percent, peer->queue_depth);
    }
    printf("===================\n\n");
}

// Force immediate load broadcast
void mesh_monitor_force_load_broadcast() {
    last_load_broadcast = 0; // Force next update to broadcast
    mesh_monitor_broadcast_load_update();
}

// Stop monitoring
void mesh_monitor_stop() {
    monitoring_active = 0;
    log_event("MESH_MONITOR", "Monitoring stopped");
}

// Start monitoring
void mesh_monitor_start() {
    monitoring_active = 1;
    last_load_broadcast = 0;
    last_health_check = 0;
    log_event("MESH_MONITOR", "Monitoring started");
}