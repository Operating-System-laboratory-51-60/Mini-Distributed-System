#ifndef COMMON_H
#define COMMON_H

#include<stdio.h> // Standard Input Output
#include<stdlib.h> // Standard Library
#include<string.h> // String operations
#include<unistd.h> // POSIX operating system API
#include<sys/socket.h> // Socket API
#include<netinet/in.h> // Internet address family
#include<arpa/inet.h> // Address conversion utilities
#include<sys/time.h> // Time operations
#include<sys/select.h> // Select() API
#include<time.h> // Time operations

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_WORKERS 10

// Message types for our protocol
typedef enum {
    MSG_REGISTER,       // Worker registering with server
    MSG_LOAD_UPDATE,    // Worker sending its current load %
    MSG_TASK_ASSIGN,    // Server assigning a task to worker
    MSG_TASK_RESULT,    // Worker sending task result back
    MSG_HEARTBEAT       // Keep-alive/fault tolerance 
} MessageType;
// Task types (The Generic Payload)
typedef enum {
    TASK_SLEEP,
    TASK_MATH     
} TaskType;
// Generic Message Structure sent over the network wire
typedef struct {
    MessageType type;
    int worker_id;      // Worker array slot assigned by server
    int load_percent;   // e.g., 45% load
    
    // Task payload
    TaskType task_type;
    int task_id;
    int task_arg;       // e.g., sleep duration length
    int task_result;    // Computed answer returned to server
} Message;

#endif