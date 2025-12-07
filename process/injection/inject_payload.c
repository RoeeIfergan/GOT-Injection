
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>

#include <sys/mman.h>

#include <dlfcn.h>
#include <link.h>
#include <stdlib.h>

#include "../utils/unix_socket.h"
#include "./utils/got_injection.h"
#include "../utils/helpers.h"

#include <sys/socket.h>
#include <netinet/in.h>   // struct sockaddr_in (optional, for logging)
#include <arpa/inet.h>
#include <errno.h>

#include "../connection_management.h"

typedef int (*accept_f_type)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static accept_f_type real_accept = NULL;
//TODO: Supress all logs in this file so server doesn't know it was injected. use the debug_print()

/* -------- our replacement printf -------- */

int unix_socket = -1;
int my_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
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

    fprintf(stdout, "current unix socket: %d\n", unix_socket);

    if (unix_socket == -1) {
        int client_fd = real_accept(sockfd, addr, addrlen);

        return client_fd;
    }

    char buf[100];

    // int rc = read(unix_socket, buf, sizeof(buf) - 1);
    char required_buffer[sizeof(HOME_IDENTIFIER)];
    fprintf(stderr, "Waiting for socket from process. buff size: %lu\n", sizeof(required_buffer));

    int * listening_fd = (int*) calloc(1, sizeof(int));

    int amount_of_bytes_read = recv_fd_over_unix_socket(unix_socket, listening_fd, required_buffer, sizeof(required_buffer));

    // if (*listening_fd == 0) {
    //     printf("Failed to receive initial FD over unix socket. fd: %d\n", *listening_fd);
    //     return -1;
    // }

    if (amount_of_bytes_read != sizeof(HOME_IDENTIFIER)) {
        printf("Received invalid msg size from process with socket. fd: %d\n", *listening_fd);
        return -1;
    }

    fprintf(stderr, "received from process?\n");

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
/* -------- constructor -------- */
void initialize_icp(void *arg) {
    fprintf(stderr, "connection to unix-socket..\n");
    int unix_socket_fd = connect_to_unix_socket();

    int * web_server_fd = (int*) calloc(1, sizeof(int));

    int rc = read(unix_socket_fd, web_server_fd, sizeof(*web_server_fd));
    char buf[100];

    if (rc > 0) {
        buf[rc] = '\0';
        printf("Server received: %s\n", buf);
    } else if (rc == -1) {
        perror("read");
    }
    fprintf(stderr, "connected to unix-socket!\n");

    int web_server_fd_int = *web_server_fd;
    fprintf(stderr, "[libhook] created unix-socket! fd=%d, web_server_fd=%d, rc=%d\n", unix_socket_fd, web_server_fd_int, rc);

    char required_buffer[1] = {0};
    if (send_fd_over_unix_socket(unix_socket_fd, web_server_fd_int, required_buffer, sizeof(required_buffer)) == -1) {
        printf("Failed to send web server FD over unix socket to web server hook. fd: %d\n", *web_server_fd);
        return;
    }
    printf("sent listening fd: %d\n", web_server_fd_int);

    unix_socket = unix_socket_fd;

    printf("set global unix socket fd: %d\n", unix_socket_fd);

    // while (1) {
    //     sleep(3);
    //     fprintf(stdout, "Slept for 3 second %d\n", *web_server_fd);
    // }

    // struct sockaddr_un addr;
    //
    // memset(&addr, 0, sizeof(struct sockaddr_un));
    // addr.sun_family = AF_UNIX;
    // strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    //
    // if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
    //     debug_print(stderr, "Failed to connect to socket!");
    //     perror("connect");
    //     close(fd);
    //     return -1;
    // }
}

__attribute__((constructor))
static void injected_init(void)
{
    fprintf(stderr, "[libhook] constructor in pid=%d\n", getpid());

    set_timeout(2, initialize_icp, NULL);
    // int unix_socket_fd = initiate_unix_socket();

    //sleep(5);

    if (hook_plt_symbol("accept", (void *)my_accept,
                        (void **)&real_accept) == 0) {
        fprintf(stderr,
                "[libhook] GOT hook for accept installed, real_accept=%p\n",
                real_accept);
    } else {
        fprintf(stderr, "[libhook] FAILED to hook accept\n");
    }

    // int client_fd = listen_to_unix_socket(unix_socket_fd);
    //
    // fprintf(stderr, "[libhook] Listening to unix-socket! fd=%d\n", client_fd);
}
