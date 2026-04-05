#include "common.h"
#include "network.h"
#include "binary_handler.h"
#include <sys/types.h>

void handle_binary_task(int sock, Message *msg)
{
    printf("BINARY task incoming! Receiving binary data...\n");

    // Step 1: Receive the actual binary data
    BinaryTask bt;
    recv_all(sock, &bt, sizeof(BinaryTask));
    printf("Received %ld bytes. Writing to /tmp/worker_bin...\n", bt.binary_size);
    
    // Step 2: write binary to a temp file
    FILE *f = fopen("/tmp/worker_bin", "wb");
    if(f == NULL)
    {
        printf("File opening error!\n");
        exit(1);
    }
    fwrite(bt.binary_data, 1, bt.binary_size, f);
    fclose(f);

    // Step 3: Make it executable
    chmod("/tmp/worker_bin", 0755);

    // Step 4: Time and run it
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    FILE *fp = popen("/tmp/worker_bin", "r");

    BinaryResult br;
    memset(&br, 0, sizeof(BinaryResult));
    br.task_id = bt.task_id;
    if(fp)
    {
        int bytes_read = fread(br.output, 1, sizeof(br.output)-1, fp);
        br.output[bytes_read] = '\0';
        pclose(fp);
    }
    else snprintf(br.output, sizeof(br.output), "ERROR: Could not execute binary");

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    // Calculate milliseconds
    br.execution_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 + (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    // Step 5: Send result back
    // First send a MSG_BINARY_RESULT control message
    Message ctrl;
    memset(&ctrl, 0, sizeof(Message));
    ctrl.type = MSG_BINARY_RESULT;
    ctrl.task_id = bt.task_id;
    send(sock, &ctrl, sizeof(Message), 0);

    // Then send the actual result struct
    send(sock, &br, sizeof(BinaryResult), 0);
    printf("Done! Executed in %ldms. Output sent.\n", br.execution_ms);
}
