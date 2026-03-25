#include "common.h"

int main()
{
    int sock;
    struct sockaddr_in serv_addr;

    // STEP 1: CREATE THE SOCKET
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    // STEP 2: SET THE DESTINATION
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT); // DIal port 8080

    // inet_pton = "Internet Pointer-to-Network"
    // We use 127.0.0.1 (localhost) to connect to the server running on our own laptop.
    // inet_pton converts the string "127.0.0.1" into binary network format.

    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address / Address not supported");
        exit(1);
    }

    // STEP 3: CONNECT (Ring the server)
    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        exit(1);
    }

    printf("Connected to server on port %d\n", PORT);

    // ==========================================
    // STEP 4: REGISTER WITH THE SERVER
    // ==========================================
    Message msg;

    // Zero out the box completely before packing it (prevents garbage data)
    memset(&msg, 0, sizeof(Message));

    // Pack it: Label = REGISTER, our ID = -1 (Server will assign un one later)
    msg.type = MSG_REGISTER;
    msg.worker_id = -1;

    // send() transmits the binary bytes of our struct over the network
    if(send(sock, &msg, sizeof(Message), 0) < 0)
    {
        perror("Register failed");
        exit(1);
    }

    printf("Registered with server. Waiting for tasks...");

    // ==========================================
    // STEP 5: PERIODICALLY REPORT LOAD
    // ==========================================
    int fake_load = 10; // start at 10%
    fd_set readfds;

    while(1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds); // Watch the server's socket

        // Timeout = 3 seconds (this replaces sleep(3))
        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        int activity = select(sock+1, &readfds, NULL, NULL, &timeout);

        if(activity > 0 && FD_ISSET(sock, &readfds))
        {
            // Server sent us something!
            Message msg;
            int valread = recv(sock, &msg, sizeof(Message), 0);

            if(valread <= 0)
            {
                printf("Server disconnected!\n");
                break;
            }
            if(msg.type == MSG_TASK_ASSIGN)
            {
                printf("Task received! Simultaing work for %d seconds...\n", msg.task_arg);
                fake_load = 80; // Load spikes when working!
                sleep(msg.task_arg); // Simulate actual work
                fake_load = 10; // Back to normal after work

                // Send result back to server
                Message result;
                memset(&result, 0, sizeof(Message));
                result.type = MSG_TASK_RESULT;
                result.task_id = msg.task_id;
                result.task_result = msg.task_arg*2; // Dummy result
                send(sock, &result, sizeof(Message), 0);
                printf("Task done! Sent result back to server.\n");
            }
        }
        else
        {
            // Timeout fired - send load update
            fake_load = (fake_load + 10) % 90;
            Message update;
            memset(&update, 0, sizeof(Message));
            update.type = MSG_LOAD_UPDATE;
            update.load_percent = fake_load;
            send(sock, &update, sizeof(Message), 0);
            printf("Sent load update: %d%%.\n",fake_load);
        }
    }
   
    return 0;
}