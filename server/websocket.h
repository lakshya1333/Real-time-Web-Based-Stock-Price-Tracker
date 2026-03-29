#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <openssl/ssl.h>

#define WEBSOCKET_OPCODE_CONT  0x0
#define WEBSOCKET_OPCODE_TEXT  0x1
#define WEBSOCKET_OPCODE_BIN   0x2
#define WEBSOCKET_OPCODE_CLOSE 0x8
#define WEBSOCKET_OPCODE_PING  0x9
#define WEBSOCKET_OPCODE_PONG  0xA

int handle_http_upgrade(SSL *ssl, const char *request, char *response_buf, size_t response_max);
int ws_read_frame(SSL *ssl, char *out_payload, size_t *out_len, int *out_opcode);
int ws_write_frame(SSL *ssl, const char *payload, size_t len, int opcode);

#endif
