/* task_queue.c — Local task queue management for P2P mesh
 * Handles queuing when all peers are busy, with periodic retry
 */

#include "../include/common.h"
#include <sys/wait.h>

// External reference to worker state (defined in peer_manager.c)
extern WorkerState worker_state;

// Initialize task queue
void task_queue_init() {
    worker_state.queue_head = 0;
    worker_state.queue_tail = 0;
    log_event("TASK_QUEUE", "Initialized with capacity %d", MAX_QUEUE);
}

// Check if queue is empty
int task_queue_is_empty() {
    return worker_state.queue_head == worker_state.queue_tail;
}

// Check if queue is full
int task_queue_is_full() {
    return (worker_state.queue_tail + 1) % MAX_QUEUE == worker_state.queue_head;
}

// Get current queue depth
int task_queue_get_depth() {
    if (worker_state.queue_tail >= worker_state.queue_head) {
        return worker_state.queue_tail - worker_state.queue_head;
    } else {
        return MAX_QUEUE - worker_state.queue_head + worker_state.queue_tail;
    }
}

// Add task to queue
int task_queue_enqueue(Task *task) {
    if (task_queue_is_full()) {
        log_queue_event("QUEUE_FULL", task->task_id, task_queue_get_depth());
        return -1; // Queue full
    }

    // Copy task to queue
    memcpy(&worker_state.task_queue[worker_state.queue_tail], task, sizeof(Task));
    worker_state.queue_tail = (worker_state.queue_tail + 1) % MAX_QUEUE;

    log_queue_event("ENQUEUED", task->task_id, task_queue_get_depth());
    return 0;
}

// Remove task from queue
int task_queue_dequeue(Task *task) {
    if (task_queue_is_empty()) {
        return -1; // Queue empty
    }

    // Copy task from queue
    memcpy(task, &worker_state.task_queue[worker_state.queue_head], sizeof(Task));
    worker_state.queue_head = (worker_state.queue_head + 1) % MAX_QUEUE;

    log_queue_event("DEQUEUED", task->task_id, task_queue_get_depth());
    return 0;
}

// Peek at next task without removing
int task_queue_peek(Task *task) {
    if (task_queue_is_empty()) {
        return -1;
    }

    memcpy(task, &worker_state.task_queue[worker_state.queue_head], sizeof(Task));
    return 0;
}

// Check queue and try to process tasks
void task_queue_check_and_process() {
    static time_t last_check = 0;
    time_t now = time(NULL);

    // Only check every 1 second to avoid spam
    if (difftime(now, last_check) < 1.0) {
        return;
    }
    last_check = now;

    if (task_queue_is_empty()) {
        return; // Nothing to do
    }

    // Reset visited peers for fresh attempt
    int visited_peers[MAX_PEERS] = {0};

    // Try to find a peer for the next task
    int best_peer = peer_manager_get_best_peer(visited_peers);

    if (best_peer >= 0) {
        // Found available peer - dequeue and forward
        Task task;
        if (task_queue_dequeue(&task) == 0) {
            PeerInfo *peer = peer_manager_get_peer_info(best_peer);

            log_event("TASK_QUEUE", "Forwarding queued task %d to %s:%d (load %d%%)",
                     task.task_id, peer->ip, peer->port, peer->load_percent);

            // Forward task to peer using existing delegation logic
            mesh_main_delegate_task_to_peer(&task);
        }
    } else {
        // No peers available, log current status
        int connected_count = peer_manager_get_connected_count();
        int total_count = peer_manager_get_peer_count();

        log_event("TASK_QUEUE", "No peers available for dequeue (%d/%d connected, queue_depth=%d)",
                 connected_count, total_count, task_queue_get_depth());
    }
}

// Get queue statistics
void task_queue_get_stats(int *depth, int *max_capacity) {
    *depth = task_queue_get_depth();
    *max_capacity = MAX_QUEUE;
}

// Force retry all queued tasks (called when a peer becomes available)
void task_queue_retry_all() {
    log_event("TASK_QUEUE", "Forcing retry of all %d queued tasks", task_queue_get_depth());

    // Reset the queue head to try all tasks again
    // This is a simple approach - in production might want more sophisticated retry logic
    if (!task_queue_is_empty()) {
        task_queue_check_and_process();
    }
}

// Clear all tasks from queue (emergency cleanup)
void task_queue_clear() {
    int cleared_count = task_queue_get_depth();
    worker_state.queue_head = 0;
    worker_state.queue_tail = 0;

    if (cleared_count > 0) {
        log_event("TASK_QUEUE", "Cleared %d tasks from queue", cleared_count);
    }
}

// Get task information for monitoring
void task_queue_get_task_info(int position, Task *task) {
    if (position < 0 || position >= task_queue_get_depth()) {
        return;
    }

    int actual_index = (worker_state.queue_head + position) % MAX_QUEUE;
    memcpy(task, &worker_state.task_queue[actual_index], sizeof(Task));
}

// Print queue status (for debugging)
void task_queue_print_status() {
    int depth = task_queue_get_depth();

    printf("Task Queue Status:\n");
    printf("  Depth: %d/%d\n", depth, MAX_QUEUE);
    printf("  Head: %d, Tail: %d\n", worker_state.queue_head, worker_state.queue_tail);

    if (!task_queue_is_empty()) {
        printf("  Queued Tasks:\n");
        for (int i = 0; i < depth; i++) {
            Task task;
            task_queue_get_task_info(i, &task);
            printf("    [%d] Task ID: %d, Type: %d, Source: %s:%d\n",
                   i, task.task_id, task.type, task.source_ip, task.source_port);
        }
    }
}