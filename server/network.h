#ifndef NETWORK_H
#define NETWORK_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024
#define PORT 8080

typedef struct {
    int fd;
    SSL *ssl;
    int is_websocket;
    int authenticated;
    int handshake_done;
} client_t;

int create_and_bind(int port);
int make_socket_non_blocking(int sfd);
SSL_CTX *create_ssl_context(void);
void configure_ssl_context(SSL_CTX *ctx);

#endif
