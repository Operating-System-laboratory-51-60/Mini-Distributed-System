CC = gcc
CFLAGS = -Wall -Iinclude

SERVER_OBJS = server/server_main.o server/worker_manager.o server/load_balancer.o server/task_dispatcher.o server/result_handler.o common/network.o

WORKER_OBJS = worker/worker_main.o worker/load_monitor.o worker/exec_handler.o worker/binary_handler.o common/network.o

all: server_bin worker_bin demo_bin

server_bin: $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) -o server_bin

worker_bin: $(WORKER_OBJS)
	$(CC) $(WORKER_OBJS) -o worker_bin

demo_bin: demo.c
	$(CC) demo.c -o demo_bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f server_bin worker_bin demo_bin server/*.o worker/*.o common/*.o /tmp/task_compiled /tmp/worker_bin
