/* network.c — Reliable TCP receive helper
 * Member: 2
 * Logic copied verbatim from worker.c — recv_all()
 */
#include "../include/common.h"
#include "../include/network.h"

/* Keeps looping recv() until ALL bytes are received.
 * On a real network, TCP splits large data into many small packets.
 * A single recv() only gets the first packet (~1500 bytes).
 * This loop waits for ALL packets to arrive before returning. */
int recv_all(int sock, void *buf, int total_size)
{
    int received = 0;
    char *ptr = (char *)buf;
    while(received < total_size)
    {
        int r = recv(sock, ptr + received, total_size - received, 0);
        if(r <= 0) return r;  // Disconnected or error
        received += r;
    }
    return received;
}
