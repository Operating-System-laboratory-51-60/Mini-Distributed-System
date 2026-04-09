/* process_manager.c — Multi-process task execution using fork()
 * Manages child processes for concurrent task execution
 */

#include "../include/common.h"
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

// Forward declarations
void process_manager_handle_sigchld(int sig);
void process_manager_remove_child(pid_t pid);
void process_manager_child_execute(Task *task);
void process_manager_execute_shell_command(const char *command, char *output, size_t output_size);
void process_manager_execute_binary(Task *task, char *output, size_t output_size);
void process_manager_send_result(Task *task, const char *result);

// External reference to worker state
extern WorkerState worker_state;

// Process tracking structure
typedef struct {
    pid_t pid;
    int task_id;
    time_t start_time;
} ChildProcess;

static ChildProcess child_processes[MAX_CONCURRENT_TASKS];

// Initialize process manager
void process_manager_init() {
    worker_state.child_count = 0;
    memset(child_processes, 0, sizeof(child_processes));

    // Setup signal handler for SIGCHLD
    signal(SIGCHLD, process_manager_handle_sigchld);

    log_event("PROCESS_MANAGER", "Initialized with capacity %d concurrent tasks", MAX_CONCURRENT_TASKS);
}

// Signal handler for child process termination
void process_manager_handle_sigchld(int sig) {
    pid_t pid;
    int status;

    // Reap all terminated children
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        process_manager_remove_child(pid);

        if (WIFEXITED(status)) {
            log_process_event("CHILD_EXITED", pid, WIFEXITED(status));
        } else if (WIFSIGNALED(status)) {
            log_process_event("CHILD_SIGNALED", pid, WIFSIGNALED(status));
        }
    }
}

// Add child process to tracking
int process_manager_add_child(pid_t pid, int task_id) {
    if (worker_state.child_count >= MAX_CONCURRENT_TASKS) {
        log_error("PROCESS_MANAGER", "Maximum concurrent tasks reached", "");
        return -1;
    }

    for (int i = 0; i < MAX_CONCURRENT_TASKS; i++) {
        if (child_processes[i].pid == 0) {
            child_processes[i].pid = pid;
            child_processes[i].task_id = task_id;
            child_processes[i].start_time = time(NULL);
            worker_state.child_count++;
            log_process_event("CHILD_ADDED", pid, task_id);
            return i;
        }
    }

    return -1;
}

// Remove child process from tracking
void process_manager_remove_child(pid_t pid) {
    for (int i = 0; i < MAX_CONCURRENT_TASKS; i++) {
        if (child_processes[i].pid == pid) {
            child_processes[i].pid = 0;
            child_processes[i].task_id = 0;
            child_processes[i].start_time = 0;
            worker_state.child_count--;
            log_process_event("CHILD_REMOVED", pid, -1);
            return;
        }
    }
}

// Check for completed processes (signal handler handles this on Linux)
void process_manager_check_completed() {
    // On Linux, the signal handler handles process completion
    // This function is kept for interface compatibility
}

// Check if we can start more child processes
int process_manager_can_execute() {
    return worker_state.child_count < MAX_CONCURRENT_TASKS;
}

// Execute task using fork
int process_manager_execute_task(Task *task) {
    if (!process_manager_can_execute()) {
        log_warning("PROCESS_MANAGER", "Cannot execute - max concurrent tasks reached");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        log_error("PROCESS_MANAGER", "Fork failed", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process
        signal(SIGCHLD, SIG_DFL);  // Let pclose wait for its own children normally
        
        // Scrub all inherited file descriptors (0=stdin, 1=stdout, 2=stderr)
        // This prevents the child from holding open inherited network sockets!
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }

        process_manager_child_execute(task);
        exit(0); // Child should not return
    } else {
        // Parent process
        process_manager_add_child(pid, task->task_id);
        log_process_event("TASK_FORKED", pid, task->task_id);
        return 0;
    }
}

// Child process execution logic
void process_manager_child_execute(Task *task) {
    char output[4096] = {0};
    struct timeval start_time, end_time;
    long execution_ms = 0;

    gettimeofday(&start_time, NULL);

    log_process_event("CHILD_STARTING", getpid(), task->task_id);

    // Validate task before processing
    if (!task) {
        log_error("PROCESS_MANAGER", "Invalid task pointer in child", "");
        strcpy(output, "Invalid task");
        goto finish;
    }

    switch (task->type) {
        case TASK_EXEC:
            log_process_event("CHILD_EXEC_CMD", getpid(), task->task_id);
            process_manager_execute_shell_command(task->command, output, sizeof(output));
            break;

        case TASK_BINARY:
            log_process_event("CHILD_EXEC_BINARY", getpid(), task->task_id);
            process_manager_execute_binary(task, output, sizeof(output));
            break;

        case TASK_SLEEP:
            // Extract sleep duration from command
            {
                int seconds = atoi(task->command);
                log_process_event("CHILD_SLEEP_START", getpid(), seconds);
                if (seconds > 0 && seconds <= 3600) {
                    sleep(seconds);
                    snprintf(output, sizeof(output), "Slept for %d seconds", seconds);
                } else {
                    strcpy(output, "Invalid sleep duration");
                }
            }
            break;

        default:
            log_process_event("CHILD_UNKNOWN_TASK", getpid(), task->type);
            strcpy(output, "Unknown task type");
            break;
    }

finish:
    gettimeofday(&end_time, NULL);
    execution_ms = ((end_time.tv_sec - start_time.tv_sec) * 1000) +
                   ((end_time.tv_usec - start_time.tv_usec) / 1000);

    log_process_event("CHILD_COMPLETED", getpid(), execution_ms);

    // Send result back to parent/originator
    process_manager_send_result(task, output);
}

// Execute shell command
void process_manager_execute_shell_command(const char *command, char *output, size_t output_size) {
    if (!command || !output || output_size == 0) {
        if (output && output_size > 0) {
            strcpy(output, "Invalid command or output buffer");
        }
        return;
    }

    char safe_command[1024];
    snprintf(safe_command, sizeof(safe_command), "%s 2>&1", command);

    FILE *fp = popen(safe_command, "r");
    if (fp) {
        size_t total_read = 0;
        size_t remaining = output_size - 1; // Leave space for null terminator
        
        while (remaining > 0 && !feof(fp)) {
            size_t bytes_read = fread(output + total_read, 1, remaining, fp);
            if (bytes_read == 0) break;
            total_read += bytes_read;
            remaining -= bytes_read;
        }
        
        output[total_read] = '\0';
        pclose(fp);
    } else {
        if (output && output_size > 0) {
            strcpy(output, "Failed to execute command");
        }
    }
}

// Execute binary task
void process_manager_execute_binary(Task *task, char *output, size_t output_size) {
    // Validate inputs
    if (!task || !output || output_size == 0) {
        strcpy(output, "Invalid task or output buffer");
        return;
    }

    if (!task->filename[0]) {
        strcpy(output, "No filename specified for binary task");
        return;
    }

    char compile_cmd[1024];
    char exec_cmd[512];
    char temp_output[4096] = {0};

    // Check if the file is a C file
    int is_c = (strlen(task->filename) > 2 && strcmp(task->filename + strlen(task->filename) - 2, ".c") == 0);
    int compile_status = 0;
    
    // Create unique executable name
    int exec_id = task->task_id % 100000;
    int ret;
    
    if (is_c) {
        // It's a C file, we must compile it first
        ret = snprintf(compile_cmd, sizeof(compile_cmd), "gcc -o /tmp/exec_%d \"%s\" 2>&1", exec_id, task->filename);
        if (ret < 0 || ret >= sizeof(compile_cmd)) {
            strcpy(output, "Filename too long for compilation");
            return;
        }
        
        FILE *compile_fp = popen(compile_cmd, "r");
        if (!compile_fp) {
            strcpy(output, "Failed to start compilation process");
            return;
        }
        
        size_t total_read = 0;
        size_t remaining = sizeof(temp_output) - 1;
        while (remaining > 0 && !feof(compile_fp)) {
            size_t bytes = fread(temp_output + total_read, 1, remaining, compile_fp);
            if (bytes == 0) break;
            total_read += bytes;
            remaining -= bytes;
        }
        temp_output[total_read] = '\0';
        compile_status = pclose(compile_fp);
        
        snprintf(exec_cmd, sizeof(exec_cmd), "/tmp/exec_%d 2>&1", exec_id);
    } else {
        // It's a pre-compiled binary, just add execute permissions and run the payload
        char chmod_cmd[256];
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s\"", task->filename);
        system(chmod_cmd);
        
        snprintf(exec_cmd, sizeof(exec_cmd), "\"%s\" 2>&1", task->filename);
    }
    
    if (compile_status == 0) {
        // Compilation successful (or no compilation needed), now execute
        FILE *exec_fp = popen(exec_cmd, "r");
        if (exec_fp) {
            char exec_output[2048] = {0};
            size_t total_read = 0;
            size_t remaining = sizeof(exec_output) - 1;
            while (remaining > 0 && !feof(exec_fp)) {
                size_t bytes = fread(exec_output + total_read, 1, remaining, exec_fp);
                if (bytes == 0) break;
                total_read += bytes;
                remaining -= bytes;
            }
            exec_output[total_read] = '\0';
            pclose(exec_fp);
            
            // Combine compile and execution results
            if (is_c) {
                ret = snprintf(output, output_size, "Compilation: OK\nExecution output:\n%s", exec_output);
                // Clean up local compiled executable
                char cleanup_cmd[256];
                snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -f /tmp/exec_%d", exec_id);
                system(cleanup_cmd);
            } else {
                ret = snprintf(output, output_size, "Execution output:\n%s", exec_output);
            }
            if (ret < 0 || ret >= output_size) {
                strcpy(output, "Output too large to fit in buffer");
            }
            
            // Clean up delegated temporary source file to avoid littering /tmp/
            if (strncmp(task->filename, "/tmp/mesh_incoming_", 19) == 0) {
                remove(task->filename);
            }
        } else {
            strcpy(output, "Compilation: OK\nExecution: Failed to run");
        }
    } else {
        // Compilation failed
        ret = snprintf(output, output_size, "Compilation failed:\n%s", temp_output);
        if (ret < 0 || ret >= output_size) {
            strcpy(output, "Compilation error output too large");
        }
    }
    
    // Always clean up delegated temporary source file to avoid littering /tmp/
    if (strncmp(task->filename, "/tmp/mesh_incoming_", 19) == 0) {
        remove(task->filename);
    }
}

// Send result back to the task originator
void process_manager_send_result(Task *task, const char *result) {
    if (!task || !result) {
        log_error("PROCESS_MANAGER", "Invalid task or result in send_result", "");
        return;
    }

    // Create result message
    Message result_msg;
    memset(&result_msg, 0, sizeof(result_msg));
    result_msg.type = MSG_TASK_RESULT;
    result_msg.task_id = task->task_id;
    result_msg.source_port = task->source_port;
    
    // Safely copy strings
    if (task->source_ip[0]) {
        strncpy(result_msg.source_ip, task->source_ip, sizeof(result_msg.source_ip) - 1);
        result_msg.source_ip[sizeof(result_msg.source_ip) - 1] = '\0';
    }
    
    // Include the original command in the result message
    if (task->command[0]) {
        strncpy(result_msg.command, task->command, sizeof(result_msg.command) - 1);
        result_msg.command[sizeof(result_msg.command) - 1] = '\0';
    }
    
    if (result) {
        strncpy(result_msg.output, result, sizeof(result_msg.output) - 1);
        result_msg.output[sizeof(result_msg.output) - 1] = '\0';
    }

    // Connect to parent process to deliver the result
    int local_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (local_sock >= 0) {
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(worker_state.my_port);
        
        // Use my_ip instead of 127.0.0.1 to match exactly what the server is bound to
        inet_pton(AF_INET, worker_state.my_ip, &serv_addr.sin_addr);
        
        if (connect(local_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            send_all(local_sock, &result_msg, sizeof(result_msg));
        } else {
            log_error("PROCESS_MANAGER", "Child failed to loopback result to parent", strerror(errno));
        }
        close(local_sock);
    }
}

// Cleanup process manager
void process_manager_cleanup() {
    // Terminate any remaining child processes
    for (int i = 0; i < MAX_CONCURRENT_TASKS; i++) {
        if (child_processes[i].pid != 0) {
            kill(child_processes[i].pid, SIGTERM);
        }
    }

    worker_state.child_count = 0;
    log_event("PROCESS_MANAGER", "Cleanup completed");
}

// Get process manager statistics
void process_manager_get_stats(int *active_children, int *max_children) {
    *active_children = worker_state.child_count;
    *max_children = MAX_CONCURRENT_TASKS;
}