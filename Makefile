CC = gcc
CFLAGS = -Wall -Iinclude

# P2P mesh components
MESH_OBJS = mesh/mesh_main.o mesh/task_queue.o mesh/result_queue.o mesh/process_manager.o mesh/mesh_monitor.o mesh/mesh_http.o common/peer_manager.o common/logger.o common/network.o

all: mesh_bin demo_bin

mesh_bin: $(MESH_OBJS)
	$(CC) $(MESH_OBJS) -o mesh_bin

demo_bin: demo.c
	$(CC) $(CFLAGS) demo.c -o demo_bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mesh_bin demo_bin mesh/*.o common/*.o /tmp/task_compiled /tmp/worker_bin events.log orphaned_results.log

.PHONY: all clean
