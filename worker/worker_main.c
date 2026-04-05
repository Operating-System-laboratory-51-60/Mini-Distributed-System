/* worker_main.c — Worker entry point and select loop
 * Replaces worker.c
 */
#include "common.h"
#include "network.h"
#include "load_monitor.h"
#include "exec_handler.h"
#include "binary_handler.h"

int main(int argc, char *argv[])
{
    if(argc != 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        exit(1);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid address / Address not supported.\n");
        exit(1);
    }

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        exit(1);
    }

    printf("Connected to server on port %d\n", PORT);

    srand((unsigned int)time(NULL));

    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = MSG_REGISTER;
    msg.worker_id = -1;

    if(send(sock, &msg, sizeof(Message), 0) < 0)
    {
        perror("Register failed");
        exit(1);
    }
    printf("Registered with server. Waiting for tasks...\n");

    fd_set readfds;

    while(1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        int activity = select(sock+1, &readfds, NULL, NULL, &timeout);

        if(activity > 0 && FD_ISSET(sock, &readfds))
        {
            Message msg;
            int valread = recv(sock, &msg, sizeof(Message), 0);

            if(valread <= 0)
            {
                printf("Server disconnected!\n");
                break;
            }

            if(msg.type == MSG_TASK_ASSIGN)
            {
                if(msg.task_type == TASK_SLEEP)
                {
                    printf("SLEEP task: working for %d seconds...\n", msg.task_arg);
                    sleep(msg.task_arg);
                    record_task(msg.task_arg);

                    Message result;
                    memset(&result, 0, sizeof(Message));
                    result.type = MSG_TASK_RESULT;
                    result.task_id = msg.task_id;
                    result.task_result = msg.task_arg * 2;
                    send(sock, &result, sizeof(Message), 0);
                    printf("Task done! Sent result back to server.\n");
                }
                else if(msg.task_type == TASK_EXEC)
                {
                    handle_exec_task(sock, &msg);
                }
                else if(msg.task_type == TASK_BINARY)
                {
                    handle_binary_task(sock, &msg);
                }
            }
        }
        else
        {
            int real_load = calculate_load();
            Message update;
            memset(&update, 0, sizeof(Message));
            update.type = MSG_LOAD_UPDATE;
            update.load_percent = real_load;
            send(sock, &update, sizeof(Message), 0);
        }
    }
   
    return 0;
}

