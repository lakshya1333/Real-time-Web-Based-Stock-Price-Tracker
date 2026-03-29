#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <signal.h>

#include "network.h"
#include "websocket.h"
#include "database.h"
#include "stock.h"

client_t clients[MAX_EVENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
sim_args_t sim_args;

void remove_client(int epoll_fd, int fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd == fd) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            if (clients[i].ssl) {
                SSL_shutdown(clients[i].ssl);
                SSL_free(clients[i].ssl);
            }
            close(fd);
            clients[i].fd = -1;
            clients[i].ssl = NULL;
            clients[i].is_websocket = 0;
            clients[i].authenticated = 0;
            printf("Client disconnected: %d\n", fd);
            db_log_event("DISCONNECT", "Client disconnected");
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    if (db_init("stock_tracker.db") < 0) {
        return EXIT_FAILURE;
    }
    db_log_event("STARTUP", "Server started");

    SSL_CTX *ctx = create_ssl_context();
    configure_ssl_context(ctx);

    for (int i = 0; i < MAX_EVENTS; i++) {
        clients[i].fd = -1;
    }

    int server_fd = create_and_bind(PORT);
    if (server_fd == -1) return EXIT_FAILURE;
    make_socket_non_blocking(server_fd);

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return EXIT_FAILURE;
    }

    struct epoll_event event;
    event.data.fd = server_fd;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        return EXIT_FAILURE;
    }

    sim_args.clients = clients;
    sim_args.max_clients = MAX_EVENTS;
    sim_args.clients_mutex = &clients_mutex;

    pthread_t stock_thread;
    pthread_create(&stock_thread, NULL, stock_simulator_thread, &sim_args);

    printf("Server listening on wss://localhost:%d\n", PORT);

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                while (1) {
                    struct sockaddr_in in_addr;
                    socklen_t in_len = sizeof(in_addr);
                    int infd = accept(server_fd, (struct sockaddr *)&in_addr, &in_len);
                    if (infd == -1) {
                        break; 
                    }
                    make_socket_non_blocking(infd);

                    SSL *ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, infd);
                    SSL_set_accept_state(ssl);

                    pthread_mutex_lock(&clients_mutex);
                    int added = 0;
                    for (int j = 0; j < MAX_EVENTS; j++) {
                        if (clients[j].fd == -1) {
                            clients[j].fd = infd;
                            clients[j].ssl = ssl;
                            clients[j].is_websocket = 0;
                            clients[j].authenticated = 0;
                            clients[j].handshake_done = 0;
                            
                            struct epoll_event ev;
                            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                            ev.data.fd = infd;
                            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &ev);
                            
                            printf("New client connected: %d\n", infd);
                            db_log_event("CONNECT", "New TLS client connected");
                            added = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    
                    if (!added) {
                        printf("Max clients reached. Rejecting.\n");
                        SSL_free(ssl);
                        close(infd);
                    }
                }
            } else {
                int client_fd = events[i].data.fd;
                client_t *c = NULL;
                for (int j = 0; j < MAX_EVENTS; j++) {
                    if (clients[j].fd == client_fd) {
                        c = &clients[j];
                        break;
                    }
                }
                if (!c) continue;

                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    remove_client(epoll_fd, client_fd);
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    if (!c->handshake_done) {
                        int ret = SSL_do_handshake(c->ssl);
                        if (ret != 1) {
                            int err = SSL_get_error(c->ssl, ret);
                            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                                continue;
                            }
                            // Print why the client was disconnected (TLS error e.g., browser rejected cert!)
                            ERR_print_errors_fp(stderr);
                            remove_client(epoll_fd, client_fd);
                            continue;
                        }
                        c->handshake_done = 1;
                    }

                    if (!c->is_websocket) {
                        char buf[4096];
                        int r = SSL_read(c->ssl, buf, sizeof(buf) - 1);
                        if (r > 0) {
                            buf[r] = '\0';
                            if (strstr(buf, "Upgrade: websocket") != NULL) {
                                char resp[1024];
                                if (handle_http_upgrade(c->ssl, buf, resp, sizeof(resp)) == 0) {
                                    c->is_websocket = 1;
                                    printf("Client %d upgraded to WebSocket\n", client_fd);
                                    db_log_event("UPGRADE", "Client upgraded to WebSocket");
                                } else {
                                    remove_client(epoll_fd, client_fd);
                                }
                            } else {
                                remove_client(epoll_fd, client_fd);
                            }
                        } else {
                            int err = SSL_get_error(c->ssl, r);
                            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                                remove_client(epoll_fd, client_fd);
                            }
                        }
                    } else {
                        char payload[4096];
                        size_t len = sizeof(payload) - 1;
                        int opcode = 0;
                        int r = ws_read_frame(c->ssl, payload, &len, &opcode);
                        if (r > 0) {
                            payload[len] = '\0';
                            if (opcode == WEBSOCKET_OPCODE_CLOSE) {
                                remove_client(epoll_fd, client_fd);
                            } else if (opcode == WEBSOCKET_OPCODE_TEXT) {
                                if (!c->authenticated) {
                                    if (strstr(payload, "auth_token=supersecret")) {
                                        c->authenticated = 1;
                                        printf("Client %d authenticated\n", client_fd);
                                        db_log_event("AUTH", "Client authenticated successfully");
                                        
                                        const char *msg = "{\"type\":\"auth\",\"status\":\"success\"}";
                                        ws_write_frame(c->ssl, msg, strlen(msg), WEBSOCKET_OPCODE_TEXT);
                                    } else {
                                        printf("Client %d auth failed\n", client_fd);
                                        db_log_event("AUTH_FAIL", "Client authentication failed");
                                        remove_client(epoll_fd, client_fd);
                                    }
                                } else {
                                    if (strstr(payload, "\"type\":\"subscribe\"")) {
                                        char *sym_start = strstr(payload, "\"symbol\":\"");
                                        if (sym_start) {
                                            sym_start += 10;
                                            char *sym_end = strchr(sym_start, '"');
                                            if (sym_end && (sym_end - sym_start < 32)) {
                                                char symbol[32] = {0};
                                                strncpy(symbol, sym_start, sym_end - sym_start);
                                                printf("Client %d subscribed to: %s\n", client_fd, symbol);
                                                add_stock(symbol);
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (r <= 0) {
                            int err = SSL_get_error(c->ssl, r);
                            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                                remove_client(epoll_fd, client_fd);
                            }
                        }
                    }
                }
            }
        }
    }

    db_close();
    close(server_fd);
    SSL_CTX_free(ctx);
    EVP_cleanup();

    return 0;
}
