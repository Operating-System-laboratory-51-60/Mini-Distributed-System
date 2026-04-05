/* worker_manager.h — Global state for all connected workers (SERVER side)
 * These arrays are defined in worker_manager.c and declared extern here
 * so every server module can access them directly.
 */
#ifndef WORKER_MANAGER_H
#define WORKER_MANAGER_H

#include "common.h"

/* Global worker state — defined in worker_manager.c */
extern int     client_sockets[MAX_WORKERS]; // Socket FD for each worker, 0 = empty slot
extern int     worker_loads[MAX_WORKERS];   // Last reported load % of each worker
extern int     has_task[MAX_WORKERS];       // 1 = worker is currently executing a task
extern Message active_tasks[MAX_WORKERS];  // The task currently assigned to each worker

/* Initialise all arrays to zero/empty at server startup */
void worker_manager_init();

/* Add a newly accepted socket into the first free slot.
 * Prints the slot index. Returns slot index or -1 if full. */
int  worker_add(int new_socket);

/* Remove a worker: close socket, reset its slot to empty.
 * Call this when a worker disconnects and has NO active task. */
void worker_remove(int idx);

#endif
