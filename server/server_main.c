#include "common.h"
#include "worker_manager.h"
#include "load_balancer.h"
#include "task_dispatcher.h"
#include "result_handler.h"
#include "network.h"

int main()
{
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1)
    {
        perror("Socket creation failed.\n");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed.\n");
        exit(1);
    }

    if(listen(server_fd, 5) < 0)
    {
        perror("Listen failed");
        exit(1);
    }

    srand((unsigned int)time(NULL));
    worker_manager_init();

    fd_set readfds;
    printf("Waiting for workers to connect...\n");

    while(1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_sd = server_fd;

        for(int i=0; i<MAX_WORKERS; i++)
        {
            int sd = client_sockets[i];
            if(sd > 0) FD_SET(sd, &readfds);
            if(sd > max_sd) max_sd = sd;
        }

        FD_SET(STDIN_FILENO, &readfds);
        if(STDIN_FILENO > max_sd) max_sd = STDIN_FILENO;

        int activity = select(max_sd+1, &readfds, NULL, NULL, NULL);
        if(activity < 0) perror("Select error");

        // New worker calling
        if(FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in worker_addr;
            socklen_t addr_size = sizeof(worker_addr);
            int new_socket = accept(server_fd, (struct sockaddr *)&worker_addr, &addr_size);

            printf("New worker connected! Socket ID is %d.\n", new_socket);
            int idx = worker_add(new_socket);
            if(idx == -1)
            {
                printf("Server full. Rejecting worker.\n");
                close(new_socket);
            }
        }
        
        // Keyboard command typed
        if(FD_ISSET(STDIN_FILENO, &readfds))
        {
            char command[512];
            if (fgets(command, sizeof(command), stdin) != NULL) {
                handle_user_command(command);
            }
        }
        
        // Data from workers
        for(int i=0; i<MAX_WORKERS; i++)
        {
            int sd = client_sockets[i];
            if(sd > 0 && FD_ISSET(sd, &readfds))
            {
                Message msg;
                int valread = recv(sd, &msg, sizeof(Message), 0);
                
                if(valread == 0) // Disconnected
                {
                    printf("!!! Worker[%d] CRASHED or disconnected!\n", i);
                    if(has_task[i] == 1)
                    {
                        printf("Worker [%d] had an active task! Attempting to reassign...\n", i);
                        Message failed_task = active_tasks[i];
                        worker_remove(i); // Clear dead worker

                        int new_idx = find_availabe_worker();
                        if(new_idx == -1)
                        {
                            printf("NO WORKERS AVAILABLE! Task #%d is lost.\n", failed_task.task_id);
                        }
                        else
                        {
                            send(client_sockets[new_idx], &failed_task, sizeof(Message), 0);
                            active_tasks[new_idx] = failed_task;
                            has_task[new_idx] = 1;
                            printf("Task #%d reassigned to Worker [%d] successfully!\n", failed_task.task_id, new_idx);
                        }
                    }
                    else
                    {
                        worker_remove(i);
                    }
                }
                else if(valread > 0)
                {
                    handle_worker_message(i, sd, &msg);
                }
            }
        }
    }
    return 0;
}