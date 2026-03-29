#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

static void base64_encode(const unsigned char *input, int length, char *output) {
    EVP_EncodeBlock((unsigned char *)output, input, length);
}

int handle_http_upgrade(SSL *ssl, const char *request, char *response_buf, size_t response_max) {
    const char *key_hdr = "Sec-WebSocket-Key: ";
    char *key_start = strstr(request, key_hdr);
    if (!key_start) return -1;
    
    key_start += strlen(key_hdr);
    char *key_end = strchr(key_start, '\r');
    if (!key_end) return -1;
    
    char key[256] = {0};
    int key_len = key_end - key_start;
    if (key_len >= (int)sizeof(key)) return -1;
    strncpy(key, key_start, key_len);
    
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[512];
    snprintf(combined, sizeof(combined), "%s%s", key, magic);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    
    #if OPENSSL_VERSION_NUMBER >= 0x30000000L
        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL);
        EVP_DigestUpdate(mdctx, combined, strlen(combined));
        EVP_DigestFinal_ex(mdctx, hash, NULL);
        EVP_MD_CTX_free(mdctx);
    #else
        SHA1((unsigned char *)combined, strlen(combined), hash);
    #endif
    
    char base64_hash[256];
    base64_encode(hash, SHA_DIGEST_LENGTH, base64_hash);
    
    snprintf(response_buf, response_max,
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n", base64_hash);
             
    int written = SSL_write(ssl, response_buf, strlen(response_buf));
    return (written > 0) ? 0 : -1;
}

int ws_read_frame(SSL *ssl, char *out_payload, size_t *out_len, int *out_opcode) {
    unsigned char header[2];
    int n = SSL_read(ssl, header, 2);
    if (n <= 0) return n; // Disconnected or error
    
    int opcode = header[0] & 0x0F;
    int mask = (header[1] & 0x80) >> 7;
    int payload_len = header[1] & 0x7F;
    
    *out_opcode = opcode;
    if (payload_len == 126) {
        unsigned char extended[2];
        if (SSL_read(ssl, extended, 2) <= 0) return -1;
        payload_len = (extended[0] << 8) | extended[1];
    } else if (payload_len == 127) {
        unsigned char extended[8];
        if (SSL_read(ssl, extended, 8) <= 0) return -1;
        // Truncating large payloads for this simple server
        payload_len = (extended[6] << 8) | extended[7]; 
    }
    
    unsigned char masking_key[4];
    if (mask) {
        if (SSL_read(ssl, masking_key, 4) <= 0) return -1;
    }
    
    if (payload_len > (int)*out_len) payload_len = *out_len;
    
    int payload_read = 0;
    while (payload_read < payload_len) {
        int r = SSL_read(ssl, out_payload + payload_read, payload_len - payload_read);
        if (r <= 0) return r;
        payload_read += r;
    }
    *out_len = payload_read;
    
    if (mask) {
        for (int i = 0; i < payload_read; i++) {
            out_payload[i] ^= masking_key[i % 4];
        }
    }
    
    return payload_read;
}

int ws_write_frame(SSL *ssl, const char *payload, size_t len, int opcode) {
    if (!ssl) return -1;
    unsigned char *frame = malloc(10 + len);
    if (!frame) return -1;
    
    int header_len = 2;
    frame[0] = 0x80 | opcode;
    
    if (len <= 125) {
        frame[1] = len;
    } else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        header_len = 4;
    } else {
        frame[1] = 127;
        frame[2] = frame[3] = frame[4] = frame[5] = 0; // Padding top 4 bytes
        frame[6] = (len >> 24) & 0xFF;
        frame[7] = (len >> 16) & 0xFF;
        frame[8] = (len >> 8) & 0xFF;
        frame[9] = len & 0xFF;
        header_len = 10;
    }
    
    if (len > 0) {
        memcpy(frame + header_len, payload, len);
    }
    
    int total_len = header_len + len;
    int written = 0;
    int ret = -1;
    
    while (written < total_len) {
        int w = SSL_write(ssl, frame + written, total_len - written);
        if (w <= 0) {
            int err = SSL_get_error(ssl, w);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                usleep(100);
                continue;
            }
            break; // Fatal error
        }
        written += w;
    }
    
    if (written == total_len) ret = len;
    free(frame);
    return ret;
}
