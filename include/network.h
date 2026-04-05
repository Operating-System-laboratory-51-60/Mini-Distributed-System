/* network.h — Reliable TCP receive/send helpers
 * Solves the TCP packet-splitting problem on real LAN connections.
 */
#ifndef NETWORK_H
#define NETWORK_H

/* recv_all: loops recv() until ALL total_size bytes are received.
 * Required for large structs (BinaryTask = 65KB) that TCP splits
 * into many packets over Ethernet (MTU ~1500 bytes).
 * Returns total bytes received, or <=0 on error/disconnect. */
int recv_all(int sock, void *buf, int total_size);

#endif
