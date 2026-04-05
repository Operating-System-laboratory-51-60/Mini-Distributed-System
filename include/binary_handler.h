/* binary_handler.h — Receive and run pre-compiled binaries (WORKER side)
 */
#ifndef BINARY_HANDLER_H
#define BINARY_HANDLER_H

#include "common.h"

/* handle_binary_task: Receives the 64KB binary buffer, writes it to /tmp,
 * chmod +x it, executes it, measures time, and returns the stdout output. */
void handle_binary_task(int sock, Message *msg);

#endif
