#include "common.h"

#define HISTORY_SIZE 20 // store last 20 task records

typedef struct
{
    time_t start_time; // When did this task start?
    int duration; // How many seconds did it run?
} TaskRecord;

TaskRecord history[HISTORY_SIZE];
int history_count = 0;

// Calculates real load based on last 2 minutes
int calculate_load()
{
    time_t now = time(NULL);
    int busy_seconds = 0;
    time_t window_start = now - 120; // 2 minutes ago

    for(int i=0;i<history_count;i++)
    {
        time_t task_end = history[i].start_time + history[i].duration;

        // Only count tasks that overlapped with our 2-minute window
        if(task_end > window_start)
        {
            time_t overlap_start = history[i].start_time > window_start ? history[i].start_time : window_start;
            time_t overlap_end = task_end < now ? task_end : now;
            busy_seconds += (int)(overlap_end - overlap_start);
        }
    }
    // cap at 100% and calculate percentage
    if(busy_seconds > 120) busy_seconds = 120;
    return (busy_seconds * 100) / 120;
}

int main(int argc, char *argv[])
{
    if(argc != 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        exit(1);
    }

    // STEP 1: CREATE THE SOCKET
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    // STEP 2: SET THE DESTINATION
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT); // Dial port 8080

    // inet_pton = "Internet Pointer-to-Network"
    // We use 127.0.0.1 (localhost) to connect to the server running on our own laptop.
    // inet_pton converts the string "127.0.0.1" into binary network format.

    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid address %s / Address not supported.\n", argv[1]);
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
                time_t task_start = time(NULL);
                printf("Task received! Simultaing work for %d seconds...\n", msg.task_arg);
                sleep(msg.task_arg); // simulate work

                //save history
                if(history_count < HISTORY_SIZE)
                {
                    history[history_count].start_time = task_start;
                    history[history_count].duration = msg.task_arg;
                    history_count++;
                }
            }

            Message result;
            memset(&result, 0, sizeof(Message));
            result.type = MSG_TASK_RESULT;
            result.task_id = msg.task_id;
            result.task_result = msg.task_arg*2; // Dummy result
            send(sock, &result, sizeof(Message), 0);
            printf("Task done! Sent result back to the server.\n");
        }
        else
        {
            // Timeout fired - send load update
            int real_load = calculate_load();
            Message update;
            memset(&update, 0, sizeof(Message));
            update.type = MSG_LOAD_UPDATE;
            update.load_percent = real_load;
            send(sock, &update, sizeof(Message), 0);
            printf("Sent load update: %d%%.\n",real_load);
        }
    }
   
    return 0;
}