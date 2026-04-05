/* exec_handler.h — Run shell commands (WORKER side)
 */
#ifndef EXEC_HANDLER_H
#define EXEC_HANDLER_H

#include "common.h"

/* handle_exec_task: Runs a shell command via popen() and sends the
 * stdout result back to the server. */
void handle_exec_task(int sock, Message *msg);

#endif
