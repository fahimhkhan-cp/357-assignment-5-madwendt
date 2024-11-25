#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include "net.h"

#define PORT 2828

// Signal handler for SIGCHLD
void sigchld_handler(int sig) {
    (void)sig; // Unused parameter
    while (waitpid(-1, NULL, WNOHANG) > 0); // Reap all terminated child processes
}

// Function to handle client requests in a child process
void handle_request(int nfd) {
    FILE *network = fdopen(nfd, "r+"); // Open the socket for reading and writing
    char *line = NULL;
    size_t size = 0;

    if (network == NULL) {
        perror("fdopen");
        close(nfd);
        exit(EXIT_FAILURE);
    }

    // Read the request line
    if (getline(&line, &size, network) == -1) {
        perror("getline");
        fprintf(network, "HTTP/1.1 400 Bad Request\r\n\r\nInvalid Request\n");
        fclose(network);
        close(nfd);
        free(line);
        exit(EXIT_FAILURE);
    }

    // Parse the HTTP request
    char method[10], path[256], protocol[10];
    if (sscanf(line, "%s %s %s", method, path, protocol) != 3) {
        fprintf(network, "HTTP/1.1 400 Bad Request\r\n\r\nInvalid Request\n");
        fclose(network);
        close(nfd);
        free(line);
        exit(EXIT_FAILURE);
    }

    // Check if the method is GET and protocol is HTTP/1.1
    if (strcmp(method, "GET") != 0 || strcmp(protocol, "HTTP/1.1") != 0) {
        fprintf(network, "HTTP/1.1 405 Method Not Allowed\r\n\r\nOnly GET is supported\n");
        fclose(network);
        close(nfd);
        free(line);
        exit(EXIT_FAILURE);
    }

    // Remove the leading '/' from the file path
    char *file_path = path + 1;

    // Try to open the requested file
    FILE *file = fopen(file_path, "r");
    if (!file) {
        fprintf(network, "HTTP/1.1 404 Not Found\r\n\r\nFile not found\n");
        fclose(network);
        close(nfd);
        free(line);
        exit(EXIT_FAILURE);
    }

    // Send a 200 OK response
    fprintf(network, "HTTP/1.1 200 OK\r\n\r\n");

    // Read the file and send its contents
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, bytes_read, network);
    }

    // Clean up
    fclose(file);
    fclose(network);
    close(nfd);
    free(line);
    exit(EXIT_SUCCESS);
}

void run_service(int fd) {
    while (1) {
        int nfd = accept_connection(fd);
        if (nfd != -1) {
            printf("Connection established\n");

            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                close(nfd);
            } else if (pid == 0) { // Child process
                close(fd); // Child doesn't need the listening socket
                handle_request(nfd);
            } else { // Parent process
                close(nfd); // Parent doesn't need the client socket
            }
        }
    }
}

int main(void) {
    // Register the SIGCHLD handler
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    int fd = create_service(PORT);
    if (fd == -1) {
        perror("create_service");
        exit(EXIT_FAILURE);
    }

    printf("Listening on port: %d\n", PORT);
    run_service(fd);
    close(fd);

    return 0;
}
