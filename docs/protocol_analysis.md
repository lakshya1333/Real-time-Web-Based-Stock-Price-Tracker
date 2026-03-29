# Analysis of TCP to WebSocket Upgrade

## Transition from TCP Socket to WebSocket Protocol
The WebSockets protocol is built upon TCP to achieve full-duplex, persistent connections. However, web browsers cannot establish raw TCP sockets to arbitrary servers; they require HTTP-based handshakes. WebSockets bridges this gap:

1. **TCP Connection (3-Way Handshake):** The browser initiates a standard TCP connection (SYN, SYN-ACK, ACK) to the server's port (e.g., 8080).
2. **TLS Handshake:** Since the system uses `wss://`, an immediate TLS handshake occurs over the TCP socket, exchanging certificates, negotiating ciphers, and establishing symmetric encryption keys.
3. **HTTP Upgrade Request:** The client sends an HTTP GET request with specific headers:
   ```http
   GET / HTTP/1.1
   Host: localhost:8080
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Key: <base64_random_value>
   ```
4. **WebSocket Handshake Response (Sec-WebSocket-Accept):** The server reads this HTTP request over the TLS stream. It extracts the `Sec-WebSocket-Key`, concatenates it with a globally unique identifier (`258EAFA5-E914-47DA-95CA-C5AB0DC85B11`), hashes the string using SHA-1, encodes it in Base64, and replies:
   ```http
   HTTP/1.1 101 Switching Protocols
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Accept: <computed_hash>
   ```
5. **Continuous Full-Duplex Stream:** The connection is no longer bound by HTTP request-response semantics. Both client and server can send binary or text frames asynchronously over the continuously open TCP socket. The `epoll` reactor monitors the file descriptor and handles incoming frames instantly without the overhead of HTTP headers per message.

## TCP Lifecycle and Database Consistency
Because the underlying TCP stream is reliable and ordered, socket APIs like `send()` or `SSL_write()` ensure that fragments arrive incrementally. If a client disconnects, `epoll` triggers `EPOLLRDHUP` or `EPOLLERR`. The server hooks into this event, cleanly rolling back state, logging a `DISCONNECT` event to SQLite (`syslogs`), and shutting down the file descriptor to ensure resource accountability and data consistency.
