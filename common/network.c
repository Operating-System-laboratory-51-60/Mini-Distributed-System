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
int recv_all(int sock, void *buf, size_t total_size)
{
    size_t received = 0;
    char *ptr = (char *)buf;
    while(received < total_size)
    {
        ssize_t r = recv(sock, ptr + received, total_size - received, 0);
        if(r <= 0) return r;  // Disconnected or error
        received += r;
    }
    return received;
}

/* Keeps looping send() until ALL bytes are sent.
 * On a real network, TCP may not send all data in one call.
 * This loop ensures all data is sent before returning. */
int send_all(int sock, void *buf, size_t total_size)
{
    size_t sent = 0;
    char *ptr = (char *)buf;
    while(sent < total_size)
    {
        ssize_t s = send(sock, ptr + sent, total_size - sent, 0);
        if(s <= 0) return s;  // Disconnected or error
        sent += s;
    }
    return sent;
}
