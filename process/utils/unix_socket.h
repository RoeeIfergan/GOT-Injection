#ifndef PROCESS_UNIX_SOCKET_H
#define PROCESS_UNIX_SOCKET_H

int initiate_unix_socket();

int listen_to_unix_socket(int unix_socket_fd);

int connect_to_unix_socket();

int send_fd_over_unix_socket(
    int unix_socket,
    int fd_to_send,
    char * buffer,
    size_t buffer_size
);

ssize_t recv_fd_over_unix_socket(
    int unix_socket,
    int * received_fd,
    void *buffer,
    size_t buffer_size
);

#endif //PROCESS_UNIX_SOCKET_H