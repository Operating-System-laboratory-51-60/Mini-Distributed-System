/* task_dispatcher.h — Parse keyboard commands and dispatch tasks (SERVER side)
 */
#ifndef TASK_DISPATCHER_H
#define TASK_DISPATCHER_H

/* handle_user_command: called when the server user types a command.
 * Parses the string and dispatches the right task to the best worker.
 * Supported commands: task <N>  |  exec <cmd>  |  bin <path>  |  run <file.c> */
void handle_user_command(char *command);

#endif
