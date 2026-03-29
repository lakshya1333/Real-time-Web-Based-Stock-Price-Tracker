# Socket Options Experimental Evaluation

In high-frequency real-time stock tracking, specific TCP socket options behave as tuning parameters affecting latency, throughput, and system resource load.

### 1. `SO_REUSEADDR`
- **Impact:** Allows the server socket to bind to a port immediately after termination, bypassing the `TIME_WAIT` state sequence.
- **Evaluation:** Without this, rapid iterations and crashes during development leave the port locked for up to 60 seconds (TCP `TIME_WAIT`), causing `bind()` errors. With `SO_REUSEADDR`, server deployment and restarts are instantaneous.

### 2. `TCP_NODELAY` (Disabling Nagle's Algorithm)
- **Impact:** By default, TCP buffers small packets to send them as a larger chunk to decrease network overhead (Nagle's Algorithm). WebSockets, particularly in stock trading, emit small telemetry frames frequently. `TCP_NODELAY` prevents buffering.
- **Evaluation:** Reduces structural latency by 20-40ms per packet. Instead of waiting for a full MSS or an ACK, packets are flushed to the wire instantly. This is crucial for rapid tick delivery.

### 3. `SO_SNDBUF` & `SO_RCVBUF`
- **Impact:** Increases the kernel memory bounds allocated for sending and receiving data on this socket.
- **Evaluation:** Under high load, such as broadcasting to thousands of clients, the `SO_SNDBUF` (set to 1MB) prevents non-blocking `write()` or `SSL_write()` calls from immediately returning `EAGAIN` during spontaneous micro-bursts of price volatility.

### 4. `SO_KEEPALIVE`
- **Impact:** Instructs the OS to send periodic TCP keepalive probes if the connection is idle.
- **Evaluation:** Prevents half-open "zombie" connections. If a client's network connection drops silently without sending a `FIN`/`RST`, the `epoll` loop wouldn't know. The `SO_KEEPALIVE` probe fails, emitting `EPOLLERR`, allowing the server to prune the dead client aggressively and free the memory allocations.
