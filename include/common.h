/* common.h — Shared protocol definitions used by ALL modules
 * Do NOT modify without informing the whole group — every file includes this.
 */
#ifndef COMMON_H
#define COMMON_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/select.h>
#include<time.h>
#include<sys/stat.h>

#define PORT            8080
#define BUFFER_SIZE     1024
#define MAX_WORKERS     10
#define MAX_BINARY_SIZE 65536   // 64KB

typedef enum {
    MSG_REGISTER,
    MSG_LOAD_UPDATE,
    MSG_TASK_ASSIGN,
    MSG_TASK_RESULT,
    MSG_HEARTBEAT,
    MSG_BINARY_RESULT
} MessageType;

typedef enum {
    TASK_SLEEP,
    TASK_MATH,
    TASK_EXEC,
    TASK_BINARY
} TaskType;

typedef struct {
    MessageType type;
    int worker_id;
    int load_percent;
    TaskType task_type;
    int task_id;
    int task_arg;
    int task_result;
    char command[256];
    char output[1024];
} Message;

typedef struct {
    int  task_id;
    long binary_size;
    char binary_data[MAX_BINARY_SIZE];
} BinaryTask;

typedef struct {
    int  task_id;
    long execution_ms;
    char output[4096];
} BinaryResult;

#endif
