#include "common.h"


// Finds the first worker with load < 50%, returns its index or -1 if none
int find_availabe_worker(int *client_sockets,int *worker_loads,int *has_task)
{
    int min_load = 100, min_idx = -1;
    for(int i=0;i<MAX_WORKERS;i++)
    {
        // Only consider connected workers (slot not empty)
        if(client_sockets[i] > 0 && has_task[i] == 0)
        {
            // If any worker is under 50%, return immediately
            if(worker_loads[i] < 50)
                return i;
            
            // Track the least-loaded worker as fallback
            if(worker_loads[i] < min_load)
            {
                min_load = worker_loads[i];
                min_idx = i;
            }
        }
    }
    return min_idx; // All workers are busy so giving the load to the minimum loaded worker
}


int main()
{
    // 1. The File Descriptor.
    // Think of this as the "ID tag" Linux uses to track your socket.
    int server_fd;

    // 2. The struct that holds our "Phone Number" (IP + Port)
    struct sockaddr_in server_addr;

    // ==========================================
    // STEP 1: CREATE THE SOCKET (Buy the phone)
    // ==========================================
    // AF_INET = IPv4 Internet Protocol
    // SOCK_STREAM = TCP (Reliable delivery)
    // 0 = Default protocol for TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // if it returns -1, the OS resufed to give us a socket
    if(server_fd == -1)
    {
        perror("Socket creation failed.\n");
        exit(1);
    }

    // ==========================================
    // STEP 2: BIND THE SOCKET (Assign the number)
    // ==========================================
    server_addr.sin_family = AF_INET;       // we are using IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces (e.g., Ethernet, Wi-Fi)
    server_addr.sin_port = htons(PORT);     // Use PORT 8080 from common.h. htons formats it safely.

    // Attach the "Phone Number" (server_addr) to our socket (server_fd)
    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed.\n");
        exit(1);
    }

    // ==========================================
    // STEP 3: LISTEN (Turn the ringer on)
    // ==========================================
    // The "5" is the backlog: It means "if 5 workers call at the EXACT same millisecond, 
    // put them in a waiting line. If a 6th calls, reject them until I answer someone."
    if(listen(server_fd, 5) < 0)
    {
        perror("Listen failed");
        exit(1);
    }

    // ==========================================
    // PHASE 2: THE SELECT() LOOP
    // ==========================================

    // An array to keep track of all connected worker sockets
    int client_sockets[MAX_WORKERS];
    for(int i=0;i<MAX_WORKERS;i++) client_sockets[i] = 0; // 0 means empty slot

    int worker_loads[MAX_WORKERS]; // Tracks last known load % of each worker
    for(int i=0;i<MAX_WORKERS;i++) worker_loads[i] = 0;

    Message active_tasks[MAX_WORKERS]; // Stored the task each worker is currently doing
    for(int i=0;i<MAX_WORKERS;i++) memset(&active_tasks[i], 0, sizeof(Message));

    int has_task[MAX_WORKERS]; // 0 = idle, 1 = currently executing a task
    for(int i=0;i<MAX_WORKERS;i++) has_task[i] = 0;

    fd_set readfds; // The receptionist's checklist of phones to watch

    printf("Waiting for workers to connect...\n");

    while(1)
    {
        FD_ZERO(&readfds); // Earse the checklist
        FD_SET(server_fd, &readfds); // ADD the main server
        int max_sd = server_fd;

        // Add all existing worker phones to the checklist
        for(int i=0;i<MAX_WORKERS;i++)
        {
            int sd = client_sockets[i];
            if(sd > 0) FD_SET(sd, &readfds);
            if(sd > max_sd) max_sd = sd; // select() needs the highest ID number!
        }

        // Also watch the keyboard for task commands
        FD_SET(STDIN_FILENO, &readfds); // STDIN_FILENO is the ID for the keyboard 
        if(STDIN_FILENO > max_sd) max_sd =  STDIN_FILENO; // 0 is the ID for the keyboard

        // The Receptionist waits here! (Blocks until ANY phone rings)
        int activity = select(max_sd+1, &readfds, NULL, NULL, NULL);

        if(activity < 0) perror("Select error");

        // Did the main server phone ring? (A NEW WORKER IS CALLING!)
        if(FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in worker_addr;
            socklen_t addr_size = sizeof(worker_addr);

            // Pick up the phone
            int new_socket = accept(server_fd, (struct sockaddr *)&worker_addr, &addr_size);

            printf("New worker connected! Socket ID is %d.\n", new_socket);

            // Save this new worker to our array so we watch it next loop
            for(int i=0;i<MAX_WORKERS;i++)
            {
                if(client_sockets[i] == 0)
                {
                    client_sockets[i] = new_socket;
                    printf("Added to array at index %d.\n", i);
                    break;
                }
            }
        }
        
        // Did we type a command to the keyboard?
        if(FD_ISSET(STDIN_FILENO, &readfds))
        {
            char command[50];
            int duration;

            // Reads what was typed, eg: "Task 5"
            fgets(command, sizeof(command), stdin);

            if(sscanf(command, "task %d", &duration) == 1)
            {
                // Find the best worker
                int idx = find_availabe_worker(client_sockets, worker_loads, has_task);
                if(idx == -1) printf("No workers connected!\n");
                else
                {
                    // Build the task message
                    Message task;
                    memset(&task, 0, sizeof(Message));
                    task.type = MSG_TASK_ASSIGN;
                    task.task_type = TASK_SLEEP;
                    task.task_arg = duration; // sleep duration
                    task.task_id = rand() % 1000; // random task ID

                    // send to the chosen worker
                    send(client_sockets[idx], &task, sizeof(Message), 0);
                    active_tasks[idx] = task; // Remember what we sent
                    has_task[idx] = 1; // Mark worker as busy
                    printf("Dispatched task (sleep %ds) to Worker[%d] (load: %d%%)\n",
                        duration, idx, worker_loads[idx]);
                }
            }
            else if(strncmp(command, "exec ", 5) == 0)
            {
                // Everything after "exec " is the shell command
                char *shell_cmd = command + 5;
                // Remove the trailing newline from fgets
                shell_cmd[strcspn(shell_cmd, "\n")] = 0;

                int idx = find_availabe_worker(client_sockets, worker_loads, has_task);
                if(idx == -1) printf("No workers conncted.\n");
                else
                {
                    Message task;
                    memset(&task, 0, sizeof(Message));
                    task.type = MSG_TASK_ASSIGN;
                    task.task_type = TASK_EXEC;
                    task.task_id = rand() % 1000;
                    strncpy(task.command, shell_cmd, sizeof(task.command) - 1);
                    send(client_sockets[idx], &task, sizeof(Message), 0);

                    active_tasks[idx] = task;
                    has_task[idx] = 1;
                    printf("Dispatched command \"%s\" to Worker [%d]\n", shell_cmd, idx);
                }

            }
        }
        // ==========================================
        // PHASE 3: RECEIVING DATA FROM WORKERS
        // ==========================================
        
        // loop through all our connected workers
        for(int i=0;i<MAX_WORKERS;i++)
        {
            int sd = client_sockets[i];

            // Did this specific worker's phone ring? (They sent us data!!)

            if(sd > 0 && FD_ISSET(sd, &readfds))
            {
                Message msg; // Create a blank box to hold the incoming data

                // recv() reads the vinary data from the network directly into our struct

                int valread = recv(sd, &msg, sizeof(Message), 0);
                if(valread == 0)
                {
                    // 0 means the worker hung up the phone
                    // DISCONNECTED

                    printf("!!! Worker[%d] CRASHED or disconnected!\n",i);
                    close(sd); // Actually hang up the line
                    client_sockets[i] = 0; // Free the slot for the new worker
                    worker_loads[i] = 0; // Reset load so slot looks fresh

                    if(has_task[i] == 1)
                    {
                        printf("Worker [%d] had an active task! Attempting to reassign...\n",i);
                        has_task[i] = 0;
                        int new_idx = find_availabe_worker(client_sockets, worker_loads, has_task);
                        if(new_idx == -1)
                        {
                            printf("NO WORKERS AVAILABLE! Task #%d is lost. Shutting down.\n",active_tasks[i].task_id);
                            exit(1);
                        }
                        send(client_sockets[new_idx], &active_tasks[i], sizeof(Message), 0);
                        active_tasks[new_idx] = active_tasks[i];
                        has_task[new_idx] = 1;
                        printf("Task #%d reassigned to Worker [%d] successfully!\n",active_tasks[i].task_id, new_idx);
                    }
                }
                else if(valread > 0)
                {
                    // DATA RECEIVED!
                    // msg.type tells us what to do
                    // msg.data[] holds the numbers

                    if(msg.type == MSG_REGISTER)
                    printf("Recieved: Worker %d registered.\n", i);
                    else if(msg.type == MSG_LOAD_UPDATE)
                    {
                        worker_loads[i] = msg.load_percent; // Save the load
                        printf("Worker[%d] load updated to %d%%.\n",i,worker_loads[i]);
                    }
                    else if(msg.type == MSG_TASK_RESULT)
                    {
                        // worker completer its task and sent us the answer!
                        printf("=== TASK COMPLETE ===\n");
                        printf("Worker [%d] finished task #%d. Result = %d.\n",i, msg.task_id,msg.task_result);

                        if(strlen(msg.output) > 0) printf("--- Output ---\n%s\n ------------\n", msg.output);
                        else printf("Result = %d\n", msg.task_result);
                        has_task[i] = 0; // Mark worker as idle
                        memset(&active_tasks[i], 0, sizeof(Message)); // Clear the task
                    }
                }
            }
        }
    }
    return 0;
}