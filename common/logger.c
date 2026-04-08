/* logger.c — Comprehensive logging infrastructure for P2P mesh
 * Handles events.log and orphaned_results.log with timestamps
 */

#include "../include/common.h"
#include <stdarg.h>
#include <sys/time.h>

// Global log file handles
static FILE *events_log = NULL;
static FILE *orphaned_log = NULL;

// Initialize logging system
void logger_init() {
    // Open events log
    events_log = fopen("events.log", "a");
    if (!events_log) {
        perror("Failed to open events.log");
        exit(1);
    }

    // Open orphaned results log
    orphaned_log = fopen("orphaned_results.log", "a");
    if (!orphaned_log) {
        perror("Failed to open orphaned_results.log");
        fclose(events_log);
        exit(1);
    }

    // Log system startup
    log_event("SYSTEM_START", "P2P Mesh Worker started");
}

// Cleanup logging system
void logger_cleanup() {
    if (events_log) {
        log_event("SYSTEM_SHUTDOWN", "P2P Mesh Worker shutting down");
        fclose(events_log);
        events_log = NULL;
    }
    if (orphaned_log) {
        fclose(orphaned_log);
        orphaned_log = NULL;
    }
}

// Get current timestamp string
void get_current_time_string(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Main event logging function
void log_event(const char *event_type, const char *format, ...) {
    if (!events_log) return;

    char timestamp[32];
    get_current_time_string(timestamp, sizeof(timestamp));

    // Write timestamp and event type
    fprintf(events_log, "[%s] %s: ", timestamp, event_type);
    fflush(events_log);

    // Handle variable arguments
    va_list args;
    va_start(args, format);
    vfprintf(events_log, format, args);
    va_end(args);

    // End line
    fprintf(events_log, "\n");
    fflush(events_log);
}

// Log orphaned result (when client crashes)
void log_orphaned_result(int task_id, const char *result, const char *source_ip, int source_port) {
    if (!orphaned_log) return;

    char timestamp[32];
    get_current_time_string(timestamp, sizeof(timestamp));

    fprintf(orphaned_log, "[%s] ORPHANED_RESULT: task_id=%d, source_ip=%s, source_port=%d\n",
            timestamp, task_id, source_ip, source_port);
    fprintf(orphaned_log, "Output:\n%s\n", result);
    fprintf(orphaned_log, "----------------------------------------\n");
    fflush(orphaned_log);
}

// Log peer status changes
void log_peer_event(const char *event, const char *peer_ip, int peer_port) {
    log_event("PEER_EVENT", "%s: %s:%d", event, peer_ip, peer_port);
}

// Log task lifecycle events
void log_task_event(const char *event, int task_id, const char *details) {
    if (details) {
        log_event("TASK_EVENT", "%s: task_id=%d, %s", event, task_id, details);
    } else {
        log_event("TASK_EVENT", "%s: task_id=%d", event, task_id);
    }
}

// Log load balancing decisions
void log_load_decision(int task_id, int local_load, const char *decision, const char *target_peer) {
    log_event("LOAD_DECISION", "task_id=%d, local_load=%d%%, decision=%s, target=%s",
              task_id, local_load, decision, target_peer ? target_peer : "local");
}

// Log queue operations
void log_queue_event(const char *operation, int task_id, int queue_depth) {
    log_event("QUEUE_EVENT", "%s: task_id=%d, queue_depth=%d",
              operation, task_id, queue_depth);
}

// Log network communication
void log_network_event(const char *event, const char *peer_ip, int peer_port, const char *details) {
    log_event("NETWORK_EVENT", "%s: %s:%d %s", event, peer_ip, peer_port,
              details ? details : "");
}

// Log process management
void log_process_event(const char *event, pid_t pid, int task_id) {
    log_event("PROCESS_EVENT", "%s: pid=%d, task_id=%d", event, pid, task_id);
}

// Log system performance
void log_performance_metric(const char *metric, double value, const char *unit) {
    log_event("PERFORMANCE", "%s: %.2f %s", metric, value, unit);
}

// Log errors with context
void log_error(const char *component, const char *error_msg, const char *context) {
    log_event("ERROR", "%s: %s (%s)", component, error_msg, context ? context : "no context");
}

// Log warnings
void log_warning(const char *component, const char *warning_msg) {
    log_event("WARNING", "%s: %s", component, warning_msg);
}

// Log debug information (only in verbose mode)
void log_debug(const char *component, const char *format, ...) {
    // TODO: Add verbose mode check
    char timestamp[32];
    get_current_time_string(timestamp, sizeof(timestamp));

    fprintf(events_log, "[%s] DEBUG_%s: ", timestamp, component);

    va_list args;
    va_start(args, format);
    vfprintf(events_log, format, args);
    va_end(args);

    fprintf(events_log, "\n");
    fflush(events_log);
}