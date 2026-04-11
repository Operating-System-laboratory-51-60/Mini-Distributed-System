#ifndef MESH_HTTP_H
#define MESH_HTTP_H

#include "common.h"

// HTTP server state
typedef struct {
    int listen_socket;
    int port;
} HttpServer;

// Initialize the HTTP server on a given port (typically port + 1000)
// Returns the listen socket or -1 on failure
int mesh_http_init(int port);

// Handle reading/writing to an active HTTP client socket
// Closes the socket when done
void mesh_http_handle_client(int client_sock);

#endif // MESH_HTTP_H
