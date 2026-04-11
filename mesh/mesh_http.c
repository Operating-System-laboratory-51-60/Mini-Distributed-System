#include "../include/mesh_http.h"
#include <fcntl.h>
#include <sys/stat.h>

extern WorkerState worker_state;
static HttpServer http_server;

// Helper: send HTTP response
static void send_response(int sock, const char *status, const char *content_type, const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n\r\n", 
             status, content_type, strlen(body));
    send_all(sock, header, strlen(header));
    send_all(sock, (void*)body, strlen(body));
}

// Helper: serve static file
static void serve_file(int sock, const char *filepath, const char *content_type) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        send_response(sock, "404 Not Found", "text/plain", "File not found.");
        return;
    }

    struct stat st;
    fstat(fd, &st);

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n", 
             content_type, st.st_size);
    send_all(sock, header, strlen(header));

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        send_all(sock, buf, n);
    }
    close(fd);
}

// Simple JSON string extraction
static void extract_json_string(const char *json, const char *key, char *out, size_t out_len) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);
    out[0] = '\0';
    
    char *start = strstr(json, search_key);
    if (!start) {
        // Try with space after colon: "key": "value"
        snprintf(search_key, sizeof(search_key), "\"%s\": \"", key);
        start = strstr(json, search_key);
    }
    if (start) {
        start += strlen(search_key);
        // Find closing quote, but skip escaped quotes
        int i = 0, j = 0;
        while (start[i] && j < (int)out_len - 1) {
            if (start[i] == '\\' && start[i+1] == '"') {
                out[j++] = '"';
                i += 2;
            } else if (start[i] == '\\' && start[i+1] == 'n') {
                out[j++] = '\n';
                i += 2;
            } else if (start[i] == '\\' && start[i+1] == 'r') {
                i += 2; // skip \r
            } else if (start[i] == '\\' && start[i+1] == 't') {
                out[j++] = '\t';
                i += 2;
            } else if (start[i] == '\\' && start[i+1] == '\\') {
                out[j++] = '\\';
                i += 2;
            } else if (start[i] == '"') {
                break; // unescaped quote = end of string
            } else {
                out[j++] = start[i++];
            }
        }
        out[j] = '\0';
    }
}

// Read full HTTP request body, handling Content-Length properly
static int recv_http_request(int sock, char *buf, size_t buf_size) {
    int total = 0;
    int header_end = 0;
    
    // First, read until we find \r\n\r\n (end of headers)
    while (total < (int)buf_size - 1) {
        int n = recv(sock, buf + total, buf_size - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        
        if (strstr(buf, "\r\n\r\n")) {
            header_end = 1;
            break;
        }
    }
    
    if (!header_end) return total;
    
    // Parse Content-Length from headers
    int content_length = 0;
    char *cl = strstr(buf, "Content-Length:");
    if (!cl) cl = strstr(buf, "content-length:");
    if (cl) {
        content_length = atoi(cl + 15);
    }
    
    if (content_length <= 0) return total;
    
    // Calculate how much body we already have
    char *body_start = strstr(buf, "\r\n\r\n") + 4;
    int header_size = body_start - buf;
    int body_received = total - header_size;
    int body_remaining = content_length - body_received;
    
    // Read remaining body
    while (body_remaining > 0 && total < (int)buf_size - 1) {
        int n = recv(sock, buf + total, 
                     (body_remaining < (int)buf_size - 1 - total) ? body_remaining : (int)buf_size - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        body_remaining -= n;
    }
    buf[total] = '\0';
    return total;
}

int mesh_http_init(int port) {
    http_server.port = port;
    http_server.listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (http_server.listen_socket < 0) return -1;

    int opt = 1;
    setsockopt(http_server.listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(http_server.listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(http_server.listen_socket);
        return -1;
    }

    if (listen(http_server.listen_socket, 10) < 0) {
        close(http_server.listen_socket);
        return -1;
    }

    // Set non-blocking
    int flags = fcntl(http_server.listen_socket, F_GETFL, 0);
    fcntl(http_server.listen_socket, F_SETFL, flags | O_NONBLOCK);

    return http_server.listen_socket;
}

void mesh_http_handle_client(int client_sock) {
    // Use a large buffer for code uploads (MAX_BINARY_SIZE = 64KB + overhead)
    static char req[MAX_BINARY_SIZE + 4096];
    memset(req, 0, sizeof(req));
    
    int recv_len = recv_http_request(client_sock, req, sizeof(req));
    if (recv_len <= 0) {
        close(client_sock);
        return;
    }

    // Identify method and path
    char method[16] = {0}, path[256] = {0};
    sscanf(req, "%15s %255s", method, path);

    // =============================================
    // GET /api/status — Local node metrics
    // =============================================
    if (strncmp(path, "/api/status", 11) == 0) {
        char json[1024];
        snprintf(json, sizeof(json), 
                 "{\"my_ip\":\"%s\", \"my_port\":%d, \"load_percent\":%d, \"queue_depth\":%d, \"active_tasks\":%d, \"max_tasks\":%d}",
                 worker_state.my_ip, worker_state.my_port, worker_state.my_load_percent,
                 task_queue_get_depth(), worker_state.child_count, MAX_CONCURRENT_TASKS);
        send_response(client_sock, "200 OK", "application/json", json);
    } 
    // =============================================
    // GET /api/peers — Connected peer list
    // =============================================
    else if (strncmp(path, "/api/peers", 10) == 0) {
        char json[8192];
        strcpy(json, "{\"peers\":[");
        int count = 0;
        int alive = 0;
        for (int i = 0; i < worker_state.peer_count; i++) {
            PeerInfo *p = &worker_state.peers[i];
            char peer_json[256];
            snprintf(peer_json, sizeof(peer_json), 
                     "{\"ip\":\"%s\", \"port\":%d, \"load_percent\":%d, \"queue_depth\":%d, \"is_alive\":%s}",
                     p->ip, p->port, p->load_percent, p->queue_depth, p->is_alive ? "true" : "false");
            if (count > 0) strcat(json, ",");
            strcat(json, peer_json);
            count++;
            if (p->is_alive) alive++;
        }
        char end[64];
        snprintf(end, sizeof(end), "], \"alive_count\":%d}", alive);
        strcat(json, end);
        send_response(client_sock, "200 OK", "application/json", json);
    }
    // =============================================
    // GET /api/results — Completed task results
    // =============================================
    else if (strncmp(path, "/api/results", 12) == 0) {
        char json[32768];
        strcpy(json, "{\"results\":[");
        int count = 0;
        
        int total = result_queue_get_depth();
        int count_limit = 20;
        
        // Walk the circular buffer from head to tail
        int curr = worker_state.result_queue.head;
        int skip = 0;
        if (total > count_limit) {
            skip = total - count_limit;
        }
        
        for (int i = 0; i < total; i++) {
            TaskResult *res = &worker_state.result_queue.results[curr];
            curr = (curr + 1) % MAX_RESULTS;
            
            if (i < skip) continue; // Skip older entries
            
            if (count > 0) strcat(json, ",");
            
            // Escape output string for JSON
            char clean_out[2048] = {0};
            int j=0, k=0;
            while(res->result[j] && k < (int)sizeof(clean_out)-6) {
                if(res->result[j] == '\n') { clean_out[k++] = '\\'; clean_out[k++] = 'n'; }
                else if(res->result[j] == '"') { clean_out[k++] = '\\'; clean_out[k++] = '"'; }
                else if(res->result[j] == '\\') { clean_out[k++] = '\\'; clean_out[k++] = '\\'; }
                else if(res->result[j] == '\r') { /* skip */ }
                else if(res->result[j] == '\t') { clean_out[k++] = '\\'; clean_out[k++] = 't'; }
                else { clean_out[k++] = res->result[j]; }
                j++;
            }
            
            char res_json[4096];
            snprintf(res_json, sizeof(res_json), 
                "{\"task_id\":%d, \"success\":%s, \"execution_ms\":%ld, \"command\":\"%s\", \"output\":\"%s\"}",
                res->task_id, res->success ? "true":"false", res->execution_time_ms, 
                res->command, clean_out);
            strcat(json, res_json);
            count++;
        }
        
        // Include total count so JS can track properly
        char tail[64];
        snprintf(tail, sizeof(tail), "], \"total\":%d}", total);
        strcat(json, tail);
        send_response(client_sock, "200 OK", "application/json", json);
    }
    // =============================================
    // POST /api/task — Submit sleep/exec task
    // =============================================
    else if (strncmp(path, "/api/task", 9) == 0 && strcmp(method, "POST") == 0) {
        char *body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4;
            char type[32], arg[256];
            extract_json_string(body, "type", type, 32);
            extract_json_string(body, "arg", arg, 256);
            
            Task t;
            memset(&t, 0, sizeof(Task));
            t.task_id = rand() % 10000;
            strcpy(t.source_ip, worker_state.my_ip);
            t.source_port = worker_state.my_port;
            t.hop_count = 0;
            memset(t.peer_visited, 0, sizeof(t.peer_visited));
            
            if (strcmp(type, "sleep") == 0) {
                t.type = TASK_SLEEP;
                strcpy(t.command, arg);
            } else if (strcmp(type, "exec") == 0) {
                t.type = TASK_EXEC;
                strcpy(t.command, arg);
            } else {
                send_response(client_sock, "400 Bad Request", "application/json", "{\"status\":\"error\", \"error\":\"Unknown task type\"}");
                close(client_sock);
                return;
            }
            
            // Mirror the CLI execution flow:
            // 1. If load is high and peers are available, try delegation
            // 2. Otherwise, try direct local execution via fork
            // 3. Only queue as last resort when at max capacity
            int load_percent = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;
            int is_high_load = (load_percent >= 70);
            char resp[256];
            
            if (is_high_load && peer_manager_get_connected_count() > 0) {
                if (mesh_main_delegate_task_to_peer(&t) < 0) {
                    if (process_manager_execute_task(&t) == 0) {
                        snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"local\"}", t.task_id);
                    } else {
                        task_queue_enqueue(&t);
                        snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"queued\"}", t.task_id);
                    }
                } else {
                    snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"delegated\"}", t.task_id);
                }
            } else if (process_manager_execute_task(&t) == 0) {
                snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"local\"}", t.task_id);
            } else {
                task_queue_enqueue(&t);
                snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"queued\"}", t.task_id);
            }
            send_response(client_sock, "200 OK", "application/json", resp);
        } else {
            send_response(client_sock, "400 Bad Request", "application/json", "{\"status\":\"error\", \"error\":\"No body\"}");
        }
    }
    // =============================================
    // POST /api/upload — Upload and execute C file
    // =============================================
    else if (strncmp(path, "/api/upload", 11) == 0 && strcmp(method, "POST") == 0) {
        char *body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4;
            char filename[256];
            char code_body[MAX_BINARY_SIZE] = {0};
            
            extract_json_string(body, "filename", filename, sizeof(filename));
            extract_json_string(body, "code", code_body, sizeof(code_body));
            
            if (strlen(code_body) == 0) {
                send_response(client_sock, "400 Bad Request", "application/json", 
                    "{\"status\":\"error\", \"error\":\"Empty file content\"}");
                close(client_sock);
                return;
            }
            
            Task t;
            memset(&t, 0, sizeof(Task));
            t.task_id = rand() % 10000 + 10000;
            strcpy(t.source_ip, worker_state.my_ip);
            t.source_port = worker_state.my_port;
            t.type = TASK_BINARY;
            t.hop_count = 0;
            
            // Write the code to a temp file so process_manager can find it
            char tmp_path[256];
            snprintf(tmp_path, sizeof(tmp_path), "/tmp/mesh_web_%d.c", t.task_id);
            FILE *fp = fopen(tmp_path, "w");
            if (fp) {
                fwrite(code_body, 1, strlen(code_body), fp);
                fclose(fp);
            } else {
                send_response(client_sock, "500 Internal Server Error", "application/json",
                    "{\"status\":\"error\", \"error\":\"Failed to write temp file\"}");
                close(client_sock);
                return;
            }
            
            // Set filename to the temp path (this is what process_manager reads)
            strncpy(t.filename, tmp_path, sizeof(t.filename) - 1);
            strncpy(t.command, filename, sizeof(t.command) - 1); // original name for display
            
            // Also store code in binary_data for delegation to remote peers
            t.binary_size = strlen(code_body);
            memcpy(t.binary_data, code_body, t.binary_size);
            
            // Same delegation logic as /api/task
            int load_percent = (worker_state.child_count * 100) / MAX_CONCURRENT_TASKS;
            int is_high_load = (load_percent >= 70);
            char resp[256];
            
            if (is_high_load && peer_manager_get_connected_count() > 0) {
                if (mesh_main_delegate_task_to_peer(&t) < 0) {
                    if (process_manager_execute_task(&t) == 0) {
                        snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"local\"}", t.task_id);
                    } else {
                        task_queue_enqueue(&t);
                        snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"queued\"}", t.task_id);
                    }
                } else {
                    snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"delegated\"}", t.task_id);
                }
            } else if (process_manager_execute_task(&t) == 0) {
                snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"local\"}", t.task_id);
            } else {
                task_queue_enqueue(&t);
                snprintf(resp, sizeof(resp), "{\"status\":\"success\", \"task_id\":%d, \"where\":\"queued\"}", t.task_id);
            }
            send_response(client_sock, "200 OK", "application/json", resp);
        }
    }
    // =============================================
    // POST /api/discover — Trigger UDP broadcast
    // =============================================
    else if (strncmp(path, "/api/discover", 13) == 0 && strcmp(method, "POST") == 0) {
        peer_manager_broadcast_discovery();
        send_response(client_sock, "200 OK", "application/json", 
            "{\"status\":\"success\", \"message\":\"Discovery broadcast sent to LAN & WiFi\"}");
    }
    // =============================================
    // Static file serving
    // =============================================
    else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        serve_file(client_sock, "web/index.html", "text/html");
    }
    else if (strcmp(path, "/style.css") == 0) {
        serve_file(client_sock, "web/style.css", "text/css");
    }
    else if (strcmp(path, "/main.js") == 0) {
        serve_file(client_sock, "web/main.js", "application/javascript");
    }
    else {
        send_response(client_sock, "404 Not Found", "text/plain", "API or File not found");
    }

    close(client_sock);
}
