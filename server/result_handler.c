/* result_handler.c — Handle responses from workers (SERVER side)
 * Logic copied verbatim from the select() loop in server.c.
 */
#include "common.h"
#include "worker_manager.h"
#include "load_balancer.h"
#include "network.h"
#include "result_handler.h"

void handle_worker_message(int idx, int sd, Message *msg)
{
    if(msg->type == MSG_REGISTER)
    {
        printf("Received: Worker %d registered.\n", idx);
    }
    else if(msg->type == MSG_LOAD_UPDATE)
    {
        worker_loads[idx] = msg->load_percent; // Save the load (silently, no spam)
    }
    else if(msg->type == MSG_TASK_RESULT)
    {
        // worker completed its task and sent us the answer!
        printf("=== TASK COMPLETE ===\n");
        printf("Worker [%d] finished task #%d. Result = %d.\n", idx, msg->task_id, msg->task_result);

        if(strlen(msg->output) > 0) printf("--- Output ---\n%s\n ------------\n", msg->output);
        else printf("Result = %d\n", msg->task_result);
        
        has_task[idx] = 0; // Mark worker as idle
        memset(&active_tasks[idx], 0, sizeof(Message)); // Clear the task
    }
    else if(msg->type == MSG_BINARY_RESULT)
    {
        BinaryResult br;
        int recv_status = recv_all(sd, &br, sizeof(BinaryResult));
        if(recv_status <= 0)
        {
            printf("Error receiving binary result from Worker[%d]!\n", idx);
            has_task[idx] = 0;
            return;
        }
        printf("=== BINARY TASK COMPLETE ===\n");
        printf("Worker[%d] Task #%d | Time: %ldms\n", idx, br.task_id, br.execution_ms);
        printf("---OUTPUT---\n%s\n--------------------\n", br.output);
        
        has_task[idx] = 0;
        memset(&active_tasks[idx], 0, sizeof(Message));
    }
}
