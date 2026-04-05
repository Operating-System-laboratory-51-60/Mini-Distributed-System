/* task_dispatcher.c — Parse keyboard commands and dispatch tasks (SERVER side)
 * Logic copied verbatim from the STDIN_FILENO block in server.c main().
 */
#include "common.h"
#include "worker_manager.h"
#include "load_balancer.h"
#include "task_dispatcher.h"

void handle_user_command(char *command)
{
    int duration;

    // Trim newline and check for empty command
    command[strcspn(command, "\n")] = 0;
    if(strlen(command) == 0) return;

    /* ── task <N> ── sleep task ─────────────────────────────────────── */
    if(sscanf(command, "task %d", &duration) == 1)
    {
        int idx = find_availabe_worker();
        if(idx == -1) printf("No workers connected!\n");
        else
        {
            Message task;
            memset(&task, 0, sizeof(Message));
            task.type      = MSG_TASK_ASSIGN;
            task.task_type = TASK_SLEEP;
            task.task_arg  = duration;
            task.task_id   = rand() % 1000;
            send(client_sockets[idx], &task, sizeof(Message), 0);
            active_tasks[idx] = task;
            has_task[idx] = 1;
            printf("Dispatched task (sleep %ds) to Worker[%d] (load: %d%%)\n",
                    duration, idx, worker_loads[idx]);
        }
    }

    /* ── exec <shell command> ────────────────────────────────────────── */
    else if(strncmp(command, "exec ", 5) == 0)
    {
        char *shell_cmd = command + 5;
        shell_cmd[strcspn(shell_cmd, "\n")] = 0;

        int idx = find_availabe_worker();
        if(idx == -1) printf("No workers connected.\n");
        else
        {
            Message task;
            memset(&task, 0, sizeof(Message));
            task.type      = MSG_TASK_ASSIGN;
            task.task_type = TASK_EXEC;
            task.task_id   = rand() % 1000;
            strncpy(task.command, shell_cmd, sizeof(task.command) - 1);
            task.command[sizeof(task.command) - 1] = '\0';
            send(client_sockets[idx], &task, sizeof(Message), 0);
            active_tasks[idx] = task;
            has_task[idx] = 1;
            printf("Dispatched command \"%s\" to Worker [%d]\n", shell_cmd, idx);
        }
    }

    /* ── bin <path> ─── send pre-built binary ────────────────────────── */
    else if(strncmp(command, "bin ", 4) == 0)
    {
        char *filepath = command + 4;
        filepath[strcspn(filepath, "\n")] = 0;

        FILE *f = fopen(filepath, "rb");
        if(f == NULL)
        {
            printf("Error: cannot open file: %s\n", filepath);
            exit(1);
        }
        int idx = find_availabe_worker();
        if(idx == -1)
        {
            printf("No workers available.\n");
            fclose(f);
        }
        else
        {
            BinaryTask bt;
            memset(&bt, 0, sizeof(BinaryTask));
            bt.task_id     = rand() % 1000;
            size_t bytes_read = fread(bt.binary_data, 1, MAX_BINARY_SIZE, f);
            bt.binary_size = bytes_read;
            fclose(f);

            Message task;
            memset(&task, 0, sizeof(Message));
            task.type      = MSG_TASK_ASSIGN;
            task.task_type = TASK_BINARY;
            task.task_id   = bt.task_id;
            send(client_sockets[idx], &task, sizeof(Message), 0);
            send(client_sockets[idx], &bt,   sizeof(BinaryTask), 0);
            active_tasks[idx] = task;
            has_task[idx] = 1;
            printf("Sent binary (%ld bytes) to Worker[%d]\n", bt.binary_size, idx);
        }
    }

    /* ── run <file.c/.cpp> ─── compile locally then send ────────────── */
    else if(strncmp(command, "run ", 4) == 0)
    {
        char *src_file = command + 4;
        src_file[strcspn(src_file, "\n")] = 0;

        // Detect extension — use gcc for .c, g++ for .cpp/.cc
        char compiler[8];
        if(strstr(src_file, ".cpp") || strstr(src_file, ".cc"))
            strcpy(compiler, "g++");
        else
            strcpy(compiler, "gcc");

        // Build and run compile command on THIS machine (Machine A)
        char compile_cmd[512];
        snprintf(compile_cmd, sizeof(compile_cmd),
                 "%s %s -o /tmp/task_compiled 2>&1", compiler, src_file);

        printf("Compiling %s with %s...\n", src_file, compiler);
        int ret = system(compile_cmd);

        if(ret != 0)
        {
            printf("Compilation FAILED! Fix errors and try again.\n");
            return;
        }
        else
        {
            printf("Compiled successfully! Sending binary to worker...\n");

            int idx = find_availabe_worker();
            if(idx == -1)
            {
                printf("No workers available!\n");
            }
            else
            {
                FILE *f = fopen("/tmp/task_compiled", "rb");
                if(f == NULL)
                {
                    printf("Error: cannot open compiled binary!\n");
                }
                else
                {
                    BinaryTask bt;
                    memset(&bt, 0, sizeof(BinaryTask));
                    bt.task_id     = rand() % 1000;
                    size_t bytes_read = fread(bt.binary_data, 1, MAX_BINARY_SIZE, f);
                    bt.binary_size = bytes_read;
                    fclose(f);

                    Message task;
                    memset(&task, 0, sizeof(Message));
                    task.type      = MSG_TASK_ASSIGN;
                    task.task_type = TASK_BINARY;
                    task.task_id   = bt.task_id;
                    send(client_sockets[idx], &task, sizeof(Message), 0);
                    send(client_sockets[idx], &bt,   sizeof(BinaryTask), 0);
                    active_tasks[idx] = task;
                    has_task[idx] = 1;
                    printf("Sent binary (%ld bytes) to Worker[%d]. Waiting for output...\n",
                           bt.binary_size, idx);
                }
            }
        }
    }
}
