#include "common.h"
#include "exec_handler.h"

void handle_exec_task(int sock, Message *msg)
{
    printf("EXEC task: running \"%s\"\n", msg->command);

    FILE *fp = popen(msg->command, "r");

    Message result;
    memset(&result, 0, sizeof(Message));
    result.type = MSG_TASK_RESULT;
    result.task_id = msg->task_id;

    if(fp == NULL)
    {
        snprintf(result.output, sizeof(result.output),
                 "ERROR: Could not run command: %s", msg->command);
    }
    else
    {
        int bytes_read = fread(result.output, 1, sizeof(result.output) - 1, fp);
        result.output[bytes_read] = '\0';
        pclose(fp);
    }

    send(sock, &result, sizeof(Message), 0);
    printf("EXEC done! Output sent to server.\n");
}
