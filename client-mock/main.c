/*
 * Very simple HTTP client to test the web server.
 *
 * Usage:
 *   ./client [host] [port]
 *
 * Defaults:
 *   host = 127.0.0.1
 *   port = 443
 *
 * Connects with plain TCP (no TLS) and sends:
 *   GET / HTTP/1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    const char *host = "127.0.0.1";
    int port = 3000;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 1;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (inet_aton(host, &addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", host);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    /* Simple HTTP/1.0 GET request */
    char request[256];
    snprintf(request, sizeof(request),
             "GET / HTTP/1.0\r\n"
             "Host: %s\r\n"
             "\r\n", host);

    size_t len = strlen(request);
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t sent = send(sock, request + sent_total, len - sent_total, 0);
        if (sent < 0) {
            perror("send");
            close(sock);
            return 1;
        }
        sent_total += (size_t)sent;
    }

    /* Read and print the response */
    char buf[1024];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        if (write(STDOUT_FILENO, buf, (size_t)n) < 0) {
            perror("write");
            break;
        }
    }
    if (n < 0) {
        perror("recv");
    }

    close(sock);
    return 0;
}