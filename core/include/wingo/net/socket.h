
/*
 * Wingo — P2P Internet Sharing Tool (Repo: Bowie)
 * Copyright (C) 2024 ASBM Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WINGO_NET_SOCKET_H
#define WINGO_NET_SOCKET_H

/*
 * ============================================================================
 * WINGO SOCKET
 * ============================================================================
 *
 * This header provides a cross-platform socket abstraction for UDP and TCP.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    WINGO SOCKET                             │
 *   │                                                             │
 *   │   ┌─────────────┐              ┌─────────────┐             │
 *   │   │     UDP     │              │     TCP     │             │
 *   │   │             │              │             │             │
│   │   │  Send/Recv  │              │  Connect    │             │
│   │   │  Datagram   │              │  Stream     │             │
│   │   │             │              │             │             │
│   │   └─────────────┘              └─────────────┘             │
│   │                                                             │
│   │   ┌─────────────────────────────────────────────────────┐   │
│   │   │              ADDRESS ABSTRACTION                    │   │
│   │   │                                                     │   │
│   │   │   IPv4 + IPv6 + Hostname Resolution                 │   │
│   │   │                                                     │   │
│   │   └─────────────────────────────────────────────────────┘   │
│   │                                                             │
│   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * SOCKET TYPES
 * ============================================================================ */

/*
 * Socket type.
 */
typedef enum {
    WINGO_SOCK_UDP = 0,
    WINGO_SOCK_TCP = 1,
} wingo_sock_type_t;

/*
 * Socket state.
 */
typedef enum {
    WINGO_SOCK_STATE_CLOSED   = 0,
    WINGO_SOCK_STATE_OPEN     = 1,
    WINGO_SOCK_STATE_BOUND    = 2,
    WINGO_SOCK_STATE_LISTEN   = 3,
    WINGO_SOCK_STATE_CONNECT  = 4,
    WINGO_SOCK_STATE_ERROR    = 5,
} wingo_sock_state_t;

/* ============================================================================
 * SOCKET ADDRESS
 * ============================================================================ */

/*
 * Maximum address string length.
 */
#define WINGO_ADDR_STR_MAX 64

/*
 * Address family.
 */
typedef enum {
    WINGO_ADDR_UNSPEC = 0,
    WINGO_ADDR_IPV4   = 1,
    WINGO_ADDR_IPV6   = 2,
} wingo_addr_family_t;

/*
 * Socket address.
 *
 * This is an opaque type. Use wingo_addr_*() functions.
 */
typedef struct wingo_addr wingo_addr_t;

/* ============================================================================
 * ADDRESS FUNCTIONS
 * ============================================================================ */

/*
 * Create an address from a string.
 *
 * @param host      Hostname or IP address
 * @param port      Port number
 * @return          Address, or NULL on error
 */
wingo_addr_t *wingo_addr_new(const char *host, wingo_u16 port);

/*
 * Create an IPv4 address.
 *
 * @param ip        IP address (e.g., "192.168.1.1")
 * @param port      Port number
 * @return          Address, or NULL on error
 */
wingo_addr_t *wingo_addr_new_ipv4(const char *ip, wingo_u16 port);

/*
 * Create an IPv6 address.
 *
 * @param ip        IP address (e.g., "::1")
 * @param port      Port number
 * @return          Address, or NULL on error
 */
wingo_addr_t *wingo_addr_new_ipv6(const char *ip, wingo_u16 port);

/*
 * Copy an address.
 *
 * @param addr      Address
 * @return          New address, or NULL on error
 */
wingo_addr_t *wingo_addr_copy(const wingo_addr_t *addr);

/*
 * Free an address.
 *
 * @param addr      Address (NULL is safe)
 */
void wingo_addr_free(wingo_addr_t *addr);

/*
 * Get address family.
 *
 * @param addr      Address
 * @return          Address family
 */
wingo_addr_family_t wingo_addr_family(const wingo_addr_t *addr);

/*
 * Get address port.
 *
 * @param addr      Address
 * @return          Port number (host byte order)
 */
wingo_u16 wingo_addr_port(const wingo_addr_t *addr);

/*
 * Set address port.
 *
 * @param addr      Address
 * @param port      Port number
 */
void wingo_addr_set_port(wingo_addr_t *addr, wingo_u16 port);

/*
 * Get address IP as string.
 *
 * @param addr      Address
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_addr_ip_str(const wingo_addr_t *addr,
                                 char *buf, wingo_size size);

/*
 * Get address as string (IP:port).
 *
 * @param addr      Address
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_addr_str(const wingo_addr_t *addr,
                              char *buf, wingo_size size);

/*
 * Compare two addresses.
 *
 * @param a         First address
 * @param b         Second address
 * @return          0 if equal, non-zero otherwise
 */
int wingo_addr_cmp(const wingo_addr_t *a, const wingo_addr_t *b);

/*
 * Check if address is IPv4.
 *
 * @param addr      Address
 * @return          true if IPv4, false otherwise
 */
bool wingo_addr_is_ipv4(const wingo_addr_t *addr);

/*
 * Check if address is IPv6.
 *
 * @param addr      Address
 * @return          true if IPv6, false otherwise
 */
bool wingo_addr_is_ipv6(const wingo_addr_t *addr);

/*
 * Check if address is IPv4-mapped IPv6.
 *
 * @param addr      Address
 * @return          true if IPv4-mapped, false otherwise
 */
bool wingo_addr_is_ipv4_mapped(const wingo_addr_t *addr);

/*
 * Check if address is loopback.
 *
 * @param addr      Address
 * @return          true if loopback, false otherwise
 */
bool wingo_addr_is_loopback(const wingo_addr_t *addr);

/*
 * Check if address is multicast.
 *
 * @param addr      Address
 * @return          true if multicast, false otherwise
 */
bool wingo_addr_is_multicast(const wingo_addr_t *addr);

/*
 * Check if address is unspecified (0.0.0.0 or ::).
 *
 * @param addr      Address
 * @return          true if unspecified, false otherwise
 */
bool wingo_addr_is_unspecified(const wingo_addr_t *addr);

/*
 * Check if address is martian (invalid).
 *
 * @param addr      Address
 * @return          true if martian, false otherwise
 */
bool wingo_addr_is_martian(const wingo_addr_t *addr);

/* ============================================================================
 * SOCKET STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Socket handle.
 *
 * This is an opaque type. Use wingo_sock_*() functions.
 */
typedef struct wingo_sock wingo_sock_t;

/* ============================================================================
 * SOCKET CREATION
 * ============================================================================ */

/*
 * Create a new socket.
 *
 * @param type      Socket type (UDP or TCP)
 * @param family    Address family (IPv4, IPv6, or UNSPEC for both)
 * @return          Socket, or NULL on error
 */
wingo_sock_t *wingo_sock_new(wingo_sock_type_t type,
                              wingo_addr_family_t family);

/*
 * Create a UDP socket.
 *
 * @param family    Address family
 * @return          Socket, or NULL on error
 */
wingo_sock_t *wingo_sock_new_udp(wingo_addr_family_t family);

/*
 * Create a TCP socket.
 *
 * @param family    Address family
 * @return          Socket, or NULL on error
 */
wingo_sock_t *wingo_sock_new_tcp(wingo_addr_family_t family);

/*
 * Close a socket.
 *
 * @param sock      Socket (NULL is safe)
 */
void wingo_sock_close(wingo_sock_t *sock);

/*
 * Free a socket.
 *
 * @param sock      Socket (NULL is safe)
 */
void wingo_sock_free(wingo_sock_t *sock);

/* ============================================================================
 * SOCKET BIND
 * ============================================================================ */

/*
 * Bind socket to an address.
 *
 * @param sock      Socket
 * @param addr      Address (NULL = any)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_bind(wingo_sock_t *sock, const wingo_addr_t *addr);

/*
 * Bind socket to a port.
 *
 * @param sock      Socket
 * @param port      Port number
 * @param family    Address family
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_bind_port(wingo_sock_t *sock,
                                    wingo_u16 port,
                                    wingo_addr_family_t family);

/*
 * Bind socket to any address.
 *
 * @param sock      Socket
 * @param port      Port number
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_bind_any(wingo_sock_t *sock, wingo_u16 port);

/* ============================================================================
 * SOCKET CONNECT
 * ============================================================================ */

/*
 * Connect socket to an address (TCP).
 *
 * @param sock      Socket
 * @param addr      Remote address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_connect(wingo_sock_t *sock, const wingo_addr_t *addr);

/*
 * Connect socket with timeout (TCP).
 *
 * @param sock      Socket
 * @param addr      Remote address
 * @param timeout_ms Timeout in milliseconds
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_connect_timeout(wingo_sock_t *sock,
                                          const wingo_addr_t *addr,
                                          wingo_i64 timeout_ms);

/*
 * Disconnect socket.
 *
 * @param sock      Socket
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_disconnect(wingo_sock_t *sock);

/* ============================================================================
 * SOCKET LISTEN (TCP)
 * ============================================================================ */

/*
 * Listen for incoming connections (TCP).
 *
 * @param sock      Socket
 * @param backlog   Maximum number of pending connections
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_listen(wingo_sock_t *sock, int backlog);

/*
 * Accept an incoming connection (TCP).
 *
 * @param sock      Listening socket
 * @param addr      Output: remote address
 * @return          New socket, or NULL on error
 */
wingo_sock_t *wingo_sock_accept(wingo_sock_t *sock, wingo_addr_t **addr);

/* ============================================================================
 * SOCKET SEND
 * ============================================================================ */

/*
 * Send data over socket.
 *
 * @param sock      Socket
 * @param data      Data to send
 * @param len       Length of data
 * @return          Number of bytes sent, or -1 on error
 */
wingo_ssize wingo_sock_send(wingo_sock_t *sock,
                             const void *data,
                             wingo_size len);

/*
 * Send data to address (UDP).
 *
 * @param sock      Socket
 * @param data      Data to send
 * @param len       Length of data
 * @param addr      Destination address
 * @return          Number of bytes sent, or -1 on error
 */
wingo_ssize wingo_sock_sendto(wingo_sock_t *sock,
                               const void *data,
                               wingo_size len,
                               const wingo_addr_t *addr);

/*
 * Send all data over socket.
 *
 * @param sock      Socket
 * @param data      Data to send
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_sendall(wingo_sock_t *sock,
                                  const void *data,
                                  wingo_size len);

/* ============================================================================
 * SOCKET RECEIVE
 * ============================================================================ */

/*
 * Receive data from socket.
 *
 * @param sock      Socket
 * @param buf       Output buffer
 * @param len       Buffer size
 * @return          Number of bytes received, 0 on close, -1 on error
 */
wingo_ssize wingo_sock_recv(wingo_sock_t *sock, void *buf, wingo_size len);

/*
 * Receive data from address (UDP).
 *
 * @param sock      Socket
 * @param buf       Output buffer
 * @param len       Buffer size
 * @param addr      Output: source address
 * @return          Number of bytes received, or -1 on error
 */
wingo_ssize wingo_sock_recvfrom(wingo_sock_t *sock,
                                 void *buf,
                                 wingo_size len,
                                 wingo_addr_t **addr);

/*
 * Receive data with timeout.
 *
 * @param sock      Socket
 * @param buf       Output buffer
 * @param len       Buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return          Number of bytes received, 0 on timeout, -1 on error
 */
wingo_ssize wingo_sock_recv_timeout(wingo_sock_t *sock,
                                     void *buf,
                                     wingo_size len,
                                     wingo_i64 timeout_ms);

/* ============================================================================
 * SOCKET OPTIONS
 * ============================================================================ */

/*
 * Set socket blocking mode.
 *
 * @param sock      Socket
 * @param blocking  true for blocking, false for non-blocking
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_blocking(wingo_sock_t *sock, bool blocking);

/*
 * Set socket reuse address.
 *
 * @param sock      Socket
 * @param reuse     true to reuse, false otherwise
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_reuseaddr(wingo_sock_t *sock, bool reuse);

/*
 * Set socket reuse port.
 *
 * @param sock      Socket
 * @param reuse     true to reuse, false otherwise
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_reuseport(wingo_sock_t *sock, bool reuse);

/*
 * Set socket keepalive.
 *
 * @param sock      Socket
 * @param keepalive true to enable, false otherwise
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_keepalive(wingo_sock_t *sock, bool keepalive);

/*
 * Set socket send buffer size.
 *
 * @param sock      Socket
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_sndbuf(wingo_sock_t *sock, int size);

/*
 * Set socket receive buffer size.
 *
 * @param sock      Socket
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_rcvbuf(wingo_sock_t *sock, int size);

/*
 * Set socket timeout.
 *
 * @param sock      Socket
 * @param timeout_ms Timeout in milliseconds
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_set_timeout(wingo_sock_t *sock, wingo_i64 timeout_ms);

/* ============================================================================
 * SOCKET QUERY
 * ============================================================================ */

/*
 * Get socket type.
 *
 * @param sock      Socket
 * @return          Socket type
 */
wingo_sock_type_t wingo_sock_type(const wingo_sock_t *sock);

/*
 * Get socket state.
 *
 * @param sock      Socket
 * @return          Socket state
 */
wingo_sock_state_t wingo_sock_state(const wingo_sock_t *sock);

/*
 * Get socket file descriptor.
 *
 * @param sock      Socket
 * @return          File descriptor, or -1 on error
 */
int wingo_sock_fd(const wingo_sock_t *sock);

/*
 * Get local address.
 *
 * @param sock      Socket
 * @return          Address, or NULL on error
 */
wingo_addr_t *wingo_sock_local_addr(const wingo_sock_t *sock);

/*
 * Get remote address.
 *
 * @param sock      Socket
 * @return          Address, or NULL on error
 */
wingo_addr_t *wingo_sock_remote_addr(const wingo_sock_t *sock);

/*
 * Check if socket is open.
 *
 * @param sock      Socket
 * @return          true if open, false otherwise
 */
bool wingo_sock_is_open(const wingo_sock_t *sock);

/*
 * Check if socket is connected.
 *
 * @param sock      Socket
 * @return          true if connected, false otherwise
 */
bool wingo_sock_is_connected(const wingo_sock_t *sock);

/* ============================================================================
 * SOCKET UTILITY
 * ============================================================================ */

/*
 * Get last socket error.
 *
 * @param sock      Socket
 * @return          Error code
 */
wingo_error_t wingo_sock_last_error(const wingo_sock_t *sock);

/*
 * Clear socket error.
 *
 * @param sock      Socket
 */
void wingo_sock_clear_error(wingo_sock_t *sock);

/*
 * Check if socket has error.
 *
 * @param sock      Socket
 * @return          true if error, false otherwise
 */
bool wingo_sock_has_error(const wingo_sock_t *sock);

/*
 * Get socket error string.
 *
 * @param sock      Socket
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_sock_error_str(const wingo_sock_t *sock,
                                    char *buf, wingo_size size);

/* ============================================================================
 * SOCKET SET (for epoll integration)
 * ============================================================================ */

/*
 * Get socket for epoll registration.
 *
 * @param sock      Socket
 * @return          File descriptor for epoll
 */
int wingo_sock_epoll_fd(const wingo_sock_t *sock);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_SOCKET_H */
