/* load_balancer.c — Worker selection algorithm (SERVER side)
 * Logic copied verbatim from find_availabe_worker() in server.c.
 * Now reads from global arrays in worker_manager.c instead of parameters.
 */
#include "common.h"
#include "worker_manager.h"
#include "load_balancer.h"

/* Finds the first worker with load < 50%, returns its index or -1 if none.
 * Falls back to the least-loaded worker if all are >= 50%. */
int find_availabe_worker()
{
    int min_load = 100, min_idx = -1;
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        // Only consider connected workers (slot not empty) with no active task
        if(client_sockets[i] > 0 && has_task[i] == 0)
        {
            // If any worker is under 50%, return immediately
            if(worker_loads[i] < 50)
                return i;

            // Track the least-loaded worker as fallback
            if(worker_loads[i] < min_load)
            {
                min_load = worker_loads[i];
                min_idx  = i;
            }
        }
    }
    return min_idx; // All workers busy — assign to minimum loaded one
}
