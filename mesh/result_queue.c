/* result_queue.c — Result queue management for task execution results
 * Stores results in FIFO queue to avoid race conditions
 */

#include "../include/common.h"
#include <time.h>

// External reference to worker state
extern WorkerState worker_state;

// Initialize result queue
void result_queue_init() {
    worker_state.result_queue.head = 0;
    worker_state.result_queue.tail = 0;
    log_event("RESULT_QUEUE", "Initialized with capacity %d", MAX_RESULTS);
}

// Check if result queue is empty
int result_queue_is_empty() {
    return worker_state.result_queue.head == worker_state.result_queue.tail;
}

// Get current result queue depth
int result_queue_get_depth() {
    if (worker_state.result_queue.tail >= worker_state.result_queue.head) {
        return worker_state.result_queue.tail - worker_state.result_queue.head;
    } else {
        return MAX_RESULTS - worker_state.result_queue.head + worker_state.result_queue.tail;
    }
}

// Add result to queue
int result_queue_enqueue(int task_id, const char *command, const char *result, long exec_time_ms, int success) {
    // Check if queue is full
    int next_tail = (worker_state.result_queue.tail + 1) % MAX_RESULTS;
    if (next_tail == worker_state.result_queue.head) {
        log_warning("RESULT_QUEUE", "Queue is full, dropping oldest result");
        // Drop oldest result and continue
        worker_state.result_queue.head = (worker_state.result_queue.head + 1) % MAX_RESULTS;
    }

    TaskResult *res = &worker_state.result_queue.results[worker_state.result_queue.tail];
    
    res->task_id = task_id;
    strncpy(res->command, command, sizeof(res->command) - 1);
    res->command[sizeof(res->command) - 1] = '\0';
    strncpy(res->result, result, sizeof(res->result) - 1);
    res->result[sizeof(res->result) - 1] = '\0';
    res->execution_time_ms = exec_time_ms;
    res->completion_time = time(NULL);
    res->success = success;

    worker_state.result_queue.tail = next_tail;

    log_event("RESULT_ENQUEUED", "Task %d: %s (took %ldms)", task_id, command, exec_time_ms);
    return 0;
}

// Display all results in queue
void result_queue_display_all() {
    if (result_queue_is_empty()) {
        printf("  No results in queue yet.\n");
        return;
    }

    printf("  📋 Task Results Queue (%d items):\n", result_queue_get_depth());
    printf("  %-6s | %-30s | %-8s | %-20s\n", "ID", "Command/File", "Time(ms)", "Status");
    printf("  -------|--------------------------------|----------|----------------------\n");

    int i = worker_state.result_queue.head;
    while (i != worker_state.result_queue.tail) {
        TaskResult *res = &worker_state.result_queue.results[i];
        char cmd_trunc[31];
        strncpy(cmd_trunc, res->command, 30);
        cmd_trunc[30] = '\0';
        
        printf("  %-6d | %-30s | %-8ld | %s\n", 
               res->task_id, cmd_trunc, res->execution_time_ms,
               res->success ? "✅ Success" : "❌ Failed");
        
        i = (i + 1) % MAX_RESULTS;
    }
}

// Display latest N results
void result_queue_display_latest(int count) {
    if (result_queue_is_empty()) {
        printf("  No results in queue yet.\n");
        return;
    }

    int depth = result_queue_get_depth();
    if (count > depth) count = depth;

    printf("  📋 Latest %d Results:\n", count);
    printf("  %-6s | %-30s | %-8s | Output\n", "ID", "Command/File", "Time(ms)");
    printf("  -------|--------------------------------|----------|-------------------------------------\n");

    // Calculate starting position (last 'count' items)
    int start_idx = (worker_state.result_queue.tail - count + MAX_RESULTS) % MAX_RESULTS;
    
    int i = start_idx;
    for (int j = 0; j < count; j++) {
        TaskResult *res = &worker_state.result_queue.results[i];
        char cmd_trunc[31];
        strncpy(cmd_trunc, res->command, 30);
        cmd_trunc[30] = '\0';
        
        // Truncate output to fit on one line
        char out_trunc[40];
        strncpy(out_trunc, res->result, 39);
        out_trunc[39] = '\0';
        // Replace newlines with spaces
        for (int k = 0; k < 39 && out_trunc[k]; k++) {
            if (out_trunc[k] == '\n') out_trunc[k] = ' ';
        }
        
        printf("  %-6d | %-30s | %-8ld | %s\n", 
               res->task_id, cmd_trunc, res->execution_time_ms, out_trunc);
        
        i = (i + 1) % MAX_RESULTS;
    }
}
