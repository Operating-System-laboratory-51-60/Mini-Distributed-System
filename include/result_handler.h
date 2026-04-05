/* result_handler.h — Handle responses from workers (SERVER side)
 */
#ifndef RESULT_HANDLER_H
#define RESULT_HANDLER_H

#include "common.h"

/* handle_worker_message: Dispatches incoming messages from a worker socket.
 * Handles MSG_REGISTER, MSG_LOAD_UPDATE, MSG_TASK_RESULT, MSG_BINARY_RESULT.
 * idx is the worker slot index (0..MAX_WORKERS-1).
 * sd is the socket descriptor. */
void handle_worker_message(int idx, int sd, Message *msg);

#endif
