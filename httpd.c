#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 1024

void handle_request(int client_fd);
void send_response(int client_fd, int status, const char *content_type, const char *body, size_t body_len);
void send_file(int client_fd, const char *file_path, int is_head);
void handle_sigchld(int sig);

int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    return server_fd;
}

void handle_request(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        perror("recv");
        close(client_fd);
        return;
    }

    buffer[bytes_read] = '\0';
    char method[8], path[256], version[16];
    if (sscanf(buffer, "%s %s %s", method, path, version) != 3) {
        send_response(client_fd, 400, "text/html", "Bad Request", strlen("Bad Request"));
        close(client_fd);
        return;
    }

    // Ensure path does not contain ".." to avoid directory traversal
    if (strstr(path, "..")) {
        send_response(client_fd, 403, "text/html", "Permission Denied", strlen("Permission Denied"));
        close(client_fd);
        return;
    }

    // Serve GET or HEAD request
    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        int is_head = strcmp(method, "HEAD") == 0;
        char file_path[512];
        snprintf(file_path, sizeof(file_path), ".%s", path);
        send_file(client_fd, file_path, is_head);
    } else {
        send_response(client_fd, 501, "text/html", "Not Implemented", strlen("Not Implemented"));
    }

    close(client_fd);
}

void send_response(int client_fd, int status, const char *content_type, const char *body, size_t body_len) {
    char header[BUFFER_SIZE];
    const char *status_text;

    switch (status) {
        case 200: status_text = "200 OK"; break;
        case 400: status_text = "400 Bad Request"; break;
        case 403: status_text = "403 Permission Denied"; break;
        case 404: status_text = "404 Not Found"; break;
        case 500: status_text = "500 Internal Server Error"; break;
        case 501: status_text = "501 Not Implemented"; break;
        default: status_text = "500 Internal Server Error"; break;
    }

    snprintf(header, sizeof(header),
             "HTTP/1.0 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             status_text, content_type, body_len);

    send(client_fd, header, strlen(header), 0);
    if (body && body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

void send_file(int client_fd, const char *file_path, int is_head) {
    struct stat file_stat;
    if (stat(file_path, &file_stat) < 0) {
        if (errno == ENOENT) {
            send_response(client_fd, 404, "text/html", "Not Found", strlen("Not Found"));
        } else {
            send_response(client_fd, 500, "text/html", "Internal Server Error", strlen("Internal Server Error"));
        }
        return;
    }

    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        send_response(client_fd, 403, "text/html", "Permission Denied", strlen("Permission Denied"));
        return;
    }

    send_response(client_fd, 200, "text/html", NULL, file_stat.st_size);

    if (!is_head) {
        char file_buffer[BUFFER_SIZE];
        ssize_t bytes_read;
        while ((bytes_read = read(file_fd, file_buffer, sizeof(file_buffer))) > 0) {
            send(client_fd, file_buffer, bytes_read, 0);
        }
    }

    close(file_fd);
}

void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Port must be between 1024 and 65535.\n");
        exit(1);
    }

    int server_fd = create_server_socket(port);
    printf("Server listening on port %d\n", port);

    signal(SIGCHLD, handle_sigchld);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("Connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);  // Child doesn't need the listening socket
            handle_request(client_fd);
            exit(0);
        } else if (pid > 0) {
            close(client_fd);  // Parent doesn't need the client socket
        } else {
            perror("fork");
        }
    }

    close(server_fd);
    return 0;
}
