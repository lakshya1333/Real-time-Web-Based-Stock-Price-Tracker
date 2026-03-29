# System Load and Resilience Evaluation

## High Client Load & Concurrency Logging
The architecture avoids thread-per-client overhead by multiplexing via Linux `epoll` (`O_NONBLOCK`). All network I/O operations strictly never block. State transitions (TLS handshakes) properly handle `SSL_ERROR_WANT_READ` by deferring back to the `epoll` loop. 
- **Database Write Performance:** Emitting SQLite inserts iteratively per client would destroy performance due to fsync overhead. The implementation optimizes this by logging only major state changes (Auth, Connect, Disconnect) per client and periodically recording general price history in a separate simulation thread.

## Rapid Price Fluctuations
Because `TCP_NODELAY` is active, extreme volatility results in high packet-per-second ratios. The network layer handles partial read/writes accurately using `epoll` edge-triggering (`EPOLLET`). If the kernel's `SO_SNDBUF` is exhausted during a market crash, the socket returns `EAGAIN`. For enhanced production robustness, an application-layer ring buffer per client could be utilized.

## Malicious Subscription Attempts
- **Unfinished Handshakes:** A malicious actor (e.g., Slowloris attack) opening TCP sockets but ignoring TLS handshakes would consume memory. The server scales resilience by limiting maximum concurrent connections (`MAX_EVENTS`). Epoll mitigates the cost of tracking these sockets entirely.
- **Authentication Gateway:** Until the specific `auth_token=supersecret` message is verified by the Application Layer, the server refuses to broadcast WebSocket frames to that file descriptor, explicitly preventing unauthenticated data scraping and logic evasion.
