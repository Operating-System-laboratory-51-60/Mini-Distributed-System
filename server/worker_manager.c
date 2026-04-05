/* worker_manager.c — Track connected workers, their loads and tasks (SERVER side)
 * Logic extracted verbatim from the global arrays in server.c main().
 */
#include "common.h"
#include "worker_manager.h"

/* Global definitions (one copy, shared via extern in the header) */
int     client_sockets[MAX_WORKERS];
int     worker_loads[MAX_WORKERS];
int     has_task[MAX_WORKERS];
Message active_tasks[MAX_WORKERS];

/* Zero out all arrays at startup — same as the for-loops in server.c main() */
void worker_manager_init()
{
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        client_sockets[i] = 0;
        worker_loads[i]   = 0;
        has_task[i]       = 0;
        memset(&active_tasks[i], 0, sizeof(Message));
    }
}

/* Save a new worker socket into the first empty slot (client_sockets[i] == 0).
 * Mirrors the for-loop in server.c that runs after accept(). */
int worker_add(int new_socket)
{
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        if(client_sockets[i] == 0)
        {
            client_sockets[i] = new_socket;
            printf("Added to array at index %d.\n", i);
            return i;
        }
    }
    return -1; // All 10 slots occupied
}

/* Close socket and reset slot — called when a worker disconnects with no task. */
void worker_remove(int idx)
{
    close(client_sockets[idx]);
    client_sockets[idx] = 0;
    worker_loads[idx]   = 0;
    has_task[idx]       = 0;
    memset(&active_tasks[idx], 0, sizeof(Message));
}
