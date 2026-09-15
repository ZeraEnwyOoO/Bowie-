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

/*
 * Socket implementation for Bowie.
 *
 * This file provides a cross-platform socket abstraction.
 * It uses raw POSIX socket API and handles:
 *   - Address creation (IPv4 + IPv6)
 *   - Address resolution (hostname → IP)
 *   - Socket creation (UDP + TCP)
 *   - Bind, connect, listen, accept
 *   - Send, recv (with timeout)
 *   - Socket options
 *   - Error handling
 *   - Non-blocking mode
 *   - Epoll integration
 *
 * NOTE: This implementation is IPv4-first with IPv6 support.
 *       If IPv6 is not available, it falls back to IPv4.
 */

#include "wingo/net/socket.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Socket address (concrete).
 */
struct wingo_addr {
    struct sockaddr_storage ss;     /* Socket address storage */
    socklen_t               ss_len; /* Length of address */
    char                    ip_str[WINGO_ADDR_STR_MAX]; /* Cached IP string */
    bool                    ip_str_valid;               /* Is cache valid? */
};

/*
 * Socket (concrete).
 */
struct wingo_sock {
    int                     fd;         /* File descriptor */
    wingo_sock_kind_t       kind;       /* UDP or TCP */
    wingo_sock_state_t      state;      /* Current state */
    wingo_addr_t           *local;      /* Local address (if bound) */
    wingo_addr_t           *remote;     /* Remote address (if connected) */
    wingo_error_t           last_error; /* Last error */
    bool                    blocking;   /* Blocking mode */
    wingo_i64               timeout_ms; /* Timeout (ms, 0 = none) */
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Get errno-based error code.
 */
static wingo_error_t errno_to_error(int err)
{
    switch (err) {
    case 0:             return WINGO_SUCCESS;
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
                        return WINGO_ERR_NET_WOULD_BLOCK;
    case EINPROGRESS:   return WINGO_ERR_NET_IN_PROGRESS;
    case EALREADY:      return WINGO_ERR_NET_ALREADY;
    case ECONNREFUSED:  return WINGO_ERR_NET_CONN_REFUSED;
    case ECONNRESET:    return WINGO_ERR_NET_CONN_RESET;
    case ECONNABORTED:  return WINGO_ERR_NET_CONN_ABORTED;
    case ETIMEDOUT:     return WINGO_ERR_NET_CONN_TIMEOUT;
    case EHOSTUNREACH:  return WINGO_ERR_NET_HOST_UNREACH;
    case ENETUNREACH:   return WINGO_ERR_NET_UNREACHABLE;
    case EADDRINUSE:    return WINGO_ERR_NET_ADDR_IN_USE;
    case EADDRNOTAVAIL: return WINGO_ERR_NET_ADDR_NOT_AVAIL;
    case EMSGSIZE:      return WINGO_ERR_NET_MSG_TOO_LONG;
    case ENOBUFS:       return WINGO_ERR_NET_NO_BUFS;
    case EAFNOSUPPORT:  return WINGO_ERR_NET_AF_NOT_SUPP;
    case EPROTONOSUPPORT: return WINGO_ERR_NET_PROTO_NOT_SUPP;
    case ESOCKTNOSUPPORT: return WINGO_ERR_NET_SOCK_NOT_SUPP;
    case EACCES:
    case EPERM:         return WINGO_ERR_PERMISSION;
    case EINVAL:        return WINGO_ERR_INVALID_ARG;
    case EMFILE:
    case ENFILE:        return WINGO_ERR_NOMEM;
    case ENOMEM:        return WINGO_ERR_NOMEM;
    case EBADF:         return WINGO_ERR_INVALID_ARG;
    case ENOTCONN:      return WINGO_ERR_NET_NOT_CONN;
    case EISCONN:       return WINGO_ERR_NET_ALREADY_CONN;
    case EOPNOTSUPP:    return WINGO_ERR_NOT_SUPPORTED;
    default:            return WINGO_ERR_NET;
    }
}

/*
 * Get port from sockaddr.
 */
static wingo_u16 sockaddr_port(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return ntohs(((struct sockaddr_in *)sa)->sin_port);
    }
    if (sa->sa_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
    }
    return 0;
}

/*
 * Set port in sockaddr.
 */
static void sockaddr_set_port(struct sockaddr *sa, wingo_u16 port)
{
    if (sa->sa_family == AF_INET) {
        ((struct sockaddr_in *)sa)->sin_port = htons(port);
    } else if (sa->sa_family == AF_INET6) {
        ((struct sockaddr_in6 *)sa)->sin6_port = htons(port);
    }
}

/*
 * Get sockaddr length.
 */
static socklen_t sockaddr_len(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return sizeof(struct sockaddr_in);
    }
    if (sa->sa_family == AF_INET6) {
        return sizeof(struct sockaddr_in6);
    }
    return sizeof(struct sockaddr_storage);
}

/*
 * Format IP address as string.
 */
static wingo_error_t sockaddr_ip_str(const struct sockaddr *sa,
                                     char *buf,
                                     wingo_size size)
{
    const void *addr;

    if (sa->sa_family == AF_INET) {
        addr = &((struct sockaddr_in *)sa)->sin_addr;
    } else if (sa->sa_family == AF_INET6) {
        addr = &((struct sockaddr_in6 *)sa)->sin6_addr;
    } else {
        return WINGO_ERR_INVALID_ARG;
    }

    if (inet_ntop(sa->sa_family, addr, buf, size) == NULL) {
        return errno_to_error(errno);
    }

    return WINGO_SUCCESS;
}

/*
 * Check if IPv4 address is martian.
 */
static bool ipv4_is_martian(const struct in_addr *addr)
{
    wingo_u32 ip = ntohl(addr->s_addr);

    /* 0.0.0.0/8 */
    if ((ip & 0xFF000000) == 0x00000000) return true;
    /* 10.0.0.0/8 */
    if ((ip & 0xFF000000) == 0x0A000000) return true;
    /* 100.64.0.0/10 (CGNAT) */
    if ((ip & 0xFFC00000) == 0x64400000) return true;
    /* 127.0.0.0/8 (loopback) */
    if ((ip & 0xFF000000) == 0x7F000000) return true;
    /* 169.254.0.0/16 (link-local) */
    if ((ip & 0xFFFF0000) == 0xA9FE0000) return true;
    /* 172.16.0.0/12 (private) */
    if ((ip & 0xFFF00000) == 0xAC100000) return true;
    /* 192.0.0.0/24 (IETF) */
    if ((ip & 0xFFFFFF00) == 0xC0000000) return true;
    /* 192.0.2.0/24 (TEST-NET-1) */
    if ((ip & 0xFFFFFF00) == 0xC0000200) return true;
    /* 192.168.0.0/16 (private) */
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true;
    /* 198.18.0.0/15 (benchmarking) */
    if ((ip & 0xFFFE0000) == 0xC6120000) return true;
    /* 198.51.100.0/24 (TEST-NET-2) */
    if ((ip & 0xFFFFFF00) == 0xC6336400) return true;
    /* 203.0.113.0/24 (TEST-NET-3) */
    if ((ip & 0xFFFFFF00) == 0xCB007100) return true;
    /* 224.0.0.0/4 (multicast) */
    if ((ip & 0xF0000000) == 0xE0000000) return true;
    /* 240.0.0.0/4 (reserved) */
    if ((ip & 0xF0000000) == 0xF0000000) return true;

    return false;
}

/*
 * Check if IPv6 address is martian.
 */
static bool ipv6_is_martian(const struct in6_addr *addr)
{
    /* ::/128 (unspecified) */
    if (IN6_IS_ADDR_UNSPECIFIED(addr)) return true;
    /* ::1/128 (loopback) */
    if (IN6_IS_ADDR_LOOPBACK(addr)) return true;
    /* fc00::/7 (unique local) */
    if ((addr->s6_addr[0] & 0xFE) == 0xFC) return true;
    /* fe80::/10 (link-local) */
    if (addr->s6_addr[0] == 0xFE && (addr->s6_addr[1] & 0xC0) == 0x80) {
        return true;
    }
    /* ff00::/8 (multicast) */
    if (addr->s6_addr[0] == 0xFF) return true;

    return false;
}

/*
 * Check if a socket kind is valid.
 */
static bool sock_kind_valid(wingo_sock_kind_t kind)
{
    return kind == WINGO_SOCK_KIND_UDP || kind == WINGO_SOCK_KIND_TCP;
}

/*
 * Get POSIX socket type from kind.
 */
static int sock_kind_to_posix(wingo_sock_kind_t kind)
{
    switch (kind) {
    case WINGO_SOCK_KIND_UDP: return SOCK_DGRAM;
    case WINGO_SOCK_KIND_TCP: return SOCK_STREAM;
    default:                  return -1;
    }
}

/*
 * Get AF from address family.
 */
static int addr_family_to_posix(wingo_addr_family_t family)
{
    switch (family) {
    case WINGO_ADDR_IPV4:   return AF_INET;
    case WINGO_ADDR_IPV6:   return AF_INET6;
    case WINGO_ADDR_UNSPEC: return AF_UNSPEC;
    default:                return AF_UNSPEC;
    }
}

/*
 * Set socket non-blocking mode.
 */
static wingo_error_t sock_set_nonblocking(int fd, bool nonblocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return errno_to_error(errno);
    }

    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        return errno_to_error(errno);
    }

    return WINGO_SUCCESS;
}

/*
 * Set socket close-on-exec.
 */
static wingo_error_t sock_set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return errno_to_error(errno);
    }

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return errno_to_error(errno);
    }

    return WINGO_SUCCESS;
}

/*
 * Wait for a socket to be readable/writable.
 *
 * @param fd        File descriptor
 * @param events    POLLIN, POLLOUT, etc.
 * @param timeout_ms Timeout in ms (-1 = infinite)
 * @return          WINGO_SUCCESS on success,
 *                  WINGO_ERR_TIMEOUT on timeout,
 *                  error code on failure
 */
static wingo_error_t sock_wait(int fd, short events, wingo_i64 timeout_ms)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    rc = poll(&pfd, 1, (int)timeout_ms);

    if (rc < 0) {
        return errno_to_error(errno);
    }

    if (rc == 0) {
        return WINGO_ERR_TIMEOUT;
    }

    if (pfd.revents & (POLLERR | POLLNVAL)) {
        return WINGO_ERR_NET;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * ADDRESS FUNCTIONS
 * ============================================================================ */

/*
 * Create a new address from a string.
 *
 * Uses getaddrinfo() to resolve hostname → IP.
 * Prefers IPv4 (AF_INET) for compatibility with routers
 * that don't support IPv6.
 */
wingo_addr_t *wingo_addr_new(const char *host, wingo_u16 port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    wingo_addr_t *addr;
    char port_str[8];
    int rc;

    if (host == NULL) {
        return NULL;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          /* IPv4 first */
    hints.ai_socktype = SOCK_DGRAM;     /* UDP (DHT uses UDP) */
    hints.ai_flags = AI_ADDRCONFIG;

    snprintf(port_str, sizeof(port_str), "%u", port);

    rc = getaddrinfo(host, port_str, &hints, &result);
    if (rc != 0) {
        WINGO_LOG_DEBUG("getaddrinfo(%s) failed: %s", host, gai_strerror(rc));
        return NULL;
    }

    /* Try each result until we find a usable one */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        addr = calloc(1, sizeof(wingo_addr_t));
        if (addr == NULL) {
            continue;
        }

        memcpy(&addr->ss, rp->ai_addr, rp->ai_addrlen);
        addr->ss_len = (socklen_t)rp->ai_addrlen;
        addr->ip_str_valid = false;

        freeaddrinfo(result);
        return addr;
    }

    freeaddrinfo(result);
    return NULL;
}

/*
 * Create an IPv4 address.
 */
wingo_addr_t *wingo_addr_new_ipv4(const char *ip, wingo_u16 port)
{
    wingo_addr_t *addr;
    struct sockaddr_in *sin;
    int rc;

    if (ip == NULL) {
        return NULL;
    }

    addr = calloc(1, sizeof(wingo_addr_t));
    if (addr == NULL) {
        return NULL;
    }

    sin = (struct sockaddr_in *)&addr->ss;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);

    rc = inet_pton(AF_INET, ip, &sin->sin_addr);
    if (rc != 1) {
        free(addr);
        return NULL;
    }

    addr->ss_len = sizeof(struct sockaddr_in);
    addr->ip_str_valid = false;

    return addr;
}

/*
 * Create an IPv6 address.
 */
wingo_addr_t *wingo_addr_new_ipv6(const char *ip, wingo_u16 port)
{
    wingo_addr_t *addr;
    struct sockaddr_in6 *sin6;
    int rc;

    if (ip == NULL) {
        return NULL;
    }

    addr = calloc(1, sizeof(wingo_addr_t));
    if (addr == NULL) {
        return NULL;
    }

    sin6 = (struct sockaddr_in6 *)&addr->ss;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);

    rc = inet_pton(AF_INET6, ip, &sin6->sin6_addr);
    if (rc != 1) {
        free(addr);
        return NULL;
    }

    addr->ss_len = sizeof(struct sockaddr_in6);
    addr->ip_str_valid = false;

    return addr;
}

/*
 * Copy an address.
 */
wingo_addr_t *wingo_addr_copy(const wingo_addr_t *addr)
{
    wingo_addr_t *copy;

    if (addr == NULL) {
        return NULL;
    }

    copy = calloc(1, sizeof(wingo_addr_t));
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, addr, sizeof(wingo_addr_t));

    return copy;
}

/*
 * Free an address.
 */
void wingo_addr_free(wingo_addr_t *addr)
{
    if (addr == NULL) {
        return;
    }

    free(addr);
}

/*
 * Get address family.
 */
wingo_addr_family_t wingo_addr_family(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return WINGO_ADDR_UNSPEC;
    }

    if (addr->ss.ss_family == AF_INET) {
        return WINGO_ADDR_IPV4;
    }
    if (addr->ss.ss_family == AF_INET6) {
        return WINGO_ADDR_IPV6;
    }

    return WINGO_ADDR_UNSPEC;
}

/*
 * Get address port.
 */
wingo_u16 wingo_addr_port(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return 0;
    }

    return sockaddr_port((const struct sockaddr *)&addr->ss);
}

/*
 * Set address port.
 */
void wingo_addr_set_port(wingo_addr_t *addr, wingo_u16 port)
{
    if (addr == NULL) {
        return;
    }

    sockaddr_set_port((struct sockaddr *)&addr->ss, port);
}

/*
 * Get address IP as string.
 */
wingo_error_t wingo_addr_ip_str(const wingo_addr_t *addr,
                                 char *buf, wingo_size size)
{
    if (addr == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Use cached string if valid */
    if (addr->ip_str_valid) {
        if (strlen(addr->ip_str) >= size) {
            return WINGO_ERR_OVERFLOW;
        }
        strcpy(buf, addr->ip_str);
        return WINGO_SUCCESS;
    }

    return sockaddr_ip_str((const struct sockaddr *)&addr->ss, buf, size);
}

/*
 * Get address as string (IP:port).
 */
wingo_error_t wingo_addr_str(const wingo_addr_t *addr,
                              char *buf, wingo_size size)
{
    char ip[WINGO_ADDR_STR_MAX];
    wingo_error_t rc;
    int n;

    if (addr == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = sockaddr_ip_str((const struct sockaddr *)&addr->ss, ip, sizeof(ip));
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    if (addr->ss.ss_family == AF_INET6) {
        /* IPv6: [ip]:port */
        n = snprintf(buf, size, "[%s]:%u", ip, wingo_addr_port(addr));
    } else {
        /* IPv4: ip:port */
        n = snprintf(buf, size, "%s:%u", ip, wingo_addr_port(addr));
    }

    if (n < 0 || (wingo_size)n >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    return WINGO_SUCCESS;
}

/*
 * Compare two addresses.
 */
int wingo_addr_cmp(const wingo_addr_t *a, const wingo_addr_t *b)
{
    if (a == NULL && b == NULL) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }

    /* Different family */
    if (a->ss.ss_family != b->ss.ss_family) {
        return (int)a->ss.ss_family - (int)b->ss.ss_family;
    }

    if (a->ss.ss_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in *)&a->ss;
        struct sockaddr_in *sb = (struct sockaddr_in *)&b->ss;
        int rc = memcmp(&sa->sin_addr, &sb->sin_addr, sizeof(sa->sin_addr));
        if (rc != 0) return rc;
        return (int)ntohs(sa->sin_port) - (int)ntohs(sb->sin_port);
    }

    if (a->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&a->ss;
        struct sockaddr_in6 *sb = (struct sockaddr_in6 *)&b->ss;
        int rc = memcmp(&sa->sin6_addr, &sb->sin6_addr, sizeof(sa->sin6_addr));
        if (rc != 0) return rc;
        return (int)ntohs(sa->sin6_port) - (int)ntohs(sb->sin6_port);
    }

    return 0;
}

/*
 * Check if address is IPv4.
 */
bool wingo_addr_is_ipv4(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }
    return addr->ss.ss_family == AF_INET;
}

/*
 * Check if address is IPv6.
 */
bool wingo_addr_is_ipv6(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }
    return addr->ss.ss_family == AF_INET6;
}

/*
 * Check if address is IPv4-mapped IPv6.
 */
bool wingo_addr_is_ipv4_mapped(const wingo_addr_t *addr)
{
    struct sockaddr_in6 *sin6;

    if (addr == NULL || addr->ss.ss_family != AF_INET6) {
        return false;
    }

    sin6 = (struct sockaddr_in6 *)&addr->ss;
    return IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr);
}

/*
 * Check if address is loopback.
 */
bool wingo_addr_is_loopback(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    if (addr->ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr->ss;
        return (ntohl(sin->sin_addr.s_addr) >> 24) == 127;
    }

    if (addr->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr->ss;
        return IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr);
    }

    return false;
}

/*
 * Check if address is multicast.
 */
bool wingo_addr_is_multicast(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    if (addr->ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr->ss;
        return IN_MULTICAST(ntohl(sin->sin_addr.s_addr));
    }

    if (addr->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr->ss;
        return IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr);
    }

    return false;
}

/*
 * Check if address is unspecified (0.0.0.0 or ::).
 */
bool wingo_addr_is_unspecified(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    if (addr->ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr->ss;
        return sin->sin_addr.s_addr == htonl(INADDR_ANY);
    }

    if (addr->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr->ss;
        return IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr);
    }

    return false;
}

/*
 * Check if address is martian (invalid).
 */
bool wingo_addr_is_martian(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return true;
    }

    if (addr->ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr->ss;
        return ipv4_is_martian(&sin->sin_addr);
    }

    if (addr->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr->ss;

        /* IPv4-mapped → check as IPv4 */
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            struct in_addr v4;
            memcpy(&v4, &sin6->sin6_addr.s6_addr[12], 4);
            return ipv4_is_martian(&v4);
        }

        return ipv6_is_martian(&sin6->sin6_addr);
    }

    return true;
}

/* ============================================================================
 * SOCKET CREATION
 * ============================================================================ */

/*
 * Create a new socket.
 */
wingo_sock_t *wingo_sock_new(wingo_sock_kind_t kind,
                              wingo_addr_family_t family)
{
    wingo_sock_t *sock;
    int posix_kind;
    int posix_family;
    int fd;
    wingo_error_t rc;

    if (!sock_kind_valid(kind)) {
        return NULL;
    }

    posix_kind = sock_kind_to_posix(kind);
    if (posix_kind < 0) {
        return NULL;
    }

    posix_family = addr_family_to_posix(family);

    /*
     * For UNSPEC, default to IPv4 (Bowie prefers IPv4).
     */
    if (posix_family == AF_UNSPEC) {
        posix_family = AF_INET;
    }

    fd = socket(posix_family, posix_kind, 0);
    if (fd < 0) {
        WINGO_LOG_DEBUG("socket() failed: %s", strerror(errno));
        return NULL;
    }

    /* Set close-on-exec */
    rc = sock_set_cloexec(fd);
    if (rc != WINGO_SUCCESS) {
        close(fd);
        return NULL;
    }

    sock = calloc(1, sizeof(wingo_sock_t));
    if (sock == NULL) {
        close(fd);
        return NULL;
    }

    sock->fd = fd;
    sock->kind = kind;
    sock->state = WINGO_SOCK_STATE_OPEN;
    sock->local = NULL;
    sock->remote = NULL;
    sock->last_error = WINGO_SUCCESS;
    sock->blocking = true;
    sock->timeout_ms = 0;

    return sock;
}

/*
 * Create a UDP socket.
 */
wingo_sock_t *wingo_sock_new_udp(wingo_addr_family_t family)
{
    return wingo_sock_new(WINGO_SOCK_KIND_UDP, family);
}

/*
 * Create a TCP socket.
 */
wingo_sock_t *wingo_sock_new_tcp(wingo_addr_family_t family)
{
    return wingo_sock_new(WINGO_SOCK_KIND_TCP, family);
}

/*
 * Close a socket.
 *
 * This closes the fd but does NOT free the struct.
 * Use wingo_sock_free() to free the struct.
 */
void wingo_sock_close(wingo_sock_t *sock)
{
    if (sock == NULL) {
        return;
    }

    if (sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
    }

    if (sock->local != NULL) {
        wingo_addr_free(sock->local);
        sock->local = NULL;
    }

    if (sock->remote != NULL) {
        wingo_addr_free(sock->remote);
        sock->remote = NULL;
    }

    sock->state = WINGO_SOCK_STATE_CLOSED;
}

/*
 * Free a socket.
 */
void wingo_sock_free(wingo_sock_t *sock)
{
    if (sock == NULL) {
        return;
    }

    wingo_sock_close(sock);
    free(sock);
}

/* ============================================================================
 * SOCKET BIND
 * ============================================================================ */

/*
 * Bind socket to an address.
 */
wingo_error_t wingo_sock_bind(wingo_sock_t *sock, const wingo_addr_t *addr)
{
    int rc;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (sock->state != WINGO_SOCK_STATE_OPEN) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = bind(sock->fd,
              (const struct sockaddr *)&addr->ss,
              addr->ss_len);

    if (rc < 0) {
        sock->last_error = errno_to_error(errno);
        WINGO_LOG_DEBUG("bind() failed: %s", strerror(errno));
        return sock->last_error;
    }

    /* Save local address */
    sock->local = wingo_addr_copy(addr);
    sock->state = WINGO_SOCK_STATE_BOUND;

    return WINGO_SUCCESS;
}

/*
 * Bind socket to a port.
 */
wingo_error_t wingo_sock_bind_port(wingo_sock_t *sock,
                                    wingo_u16 port,
                                    wingo_addr_family_t family)
{
    wingo_addr_t *addr;
    wingo_error_t rc;

    if (sock == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (family == WINGO_ADDR_IPV6) {
        addr = wingo_addr_new_ipv6("::", port);
    } else {
        addr = wingo_addr_new_ipv4("0.0.0.0", port);
    }

    if (addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_sock_bind(sock, addr);
    wingo_addr_free(addr);

    return rc;
}

/*
 * Bind socket to any address.
 */
wingo_error_t wingo_sock_bind_any(wingo_sock_t *sock, wingo_u16 port)
{
    return wingo_sock_bind_port(sock, port, WINGO_ADDR_IPV4);
}

/* ============================================================================
 * SOCKET CONNECT
 * ============================================================================ */

/*
 * Connect socket to an address (TCP).
 *
 * For UDP, this sets the default destination.
 */
wingo_error_t wingo_sock_connect(wingo_sock_t *sock, const wingo_addr_t *addr)
{
    int rc;

    if (sock == NULL || sock->fd < 0 || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (sock->state != WINGO_SOCK_STATE_OPEN &&
        sock->state != WINGO_SOCK_STATE_BOUND) {
        return WINGO_ERR_INVALID_STATE;
    }

    rc = connect(sock->fd,
                 (const struct sockaddr *)&addr->ss,
                 addr->ss_len);

    if (rc < 0) {
        sock->last_error = errno_to_error(errno);
        WINGO_LOG_DEBUG("connect() failed: %s", strerror(errno));
        return sock->last_error;
    }

    /* Save remote address */
    if (sock->remote != NULL) {
        wingo_addr_free(sock->remote);
    }
    sock->remote = wingo_addr_copy(addr);
    sock->state = WINGO_SOCK_STATE_CONNECT;

    return WINGO_SUCCESS;
}

/*
 * Connect socket with timeout (TCP).
 *
 * This uses non-blocking connect + poll.
 */
wingo_error_t wingo_sock_connect_timeout(wingo_sock_t *sock,
                                          const wingo_addr_t *addr,
                                          wingo_i64 timeout_ms)
{
    wingo_error_t rc;
    int err;
    socklen_t err_len;
    bool was_blocking;

    if (sock == NULL || sock->fd < 0 || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (sock->kind != WINGO_SOCK_KIND_TCP) {
        /* For UDP, timeout connect doesn't make sense */
        return wingo_sock_connect(sock, addr);
    }

    was_blocking = sock->blocking;

    /* Set non-blocking */
    rc = sock_set_nonblocking(sock->fd, true);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Try connect */
    rc = connect(sock->fd,
                 (const struct sockaddr *)&addr->ss,
                 addr->ss_len);

    if (rc < 0) {
        if (errno != EINPROGRESS) {
            sock->last_error = errno_to_error(errno);
            if (was_blocking) {
                sock_set_nonblocking(sock->fd, false);
            }
            return sock->last_error;
        }

        /* Wait for connect to complete */
        rc = sock_wait(sock->fd, POLLOUT, timeout_ms);
        if (rc != WINGO_SUCCESS) {
            if (was_blocking) {
                sock_set_nonblocking(sock->fd, false);
            }
            return rc;
        }

        /* Check connect result */
        err = 0;
        err_len = sizeof(err);
        if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0) {
            if (was_blocking) {
                sock_set_nonblocking(sock->fd, false);
            }
            return errno_to_error(errno);
        }

        if (err != 0) {
            if (was_blocking) {
                sock_set_nonblocking(sock->fd, false);
            }
            return errno_to_error(err);
        }
    }

    /* Restore blocking mode */
    if (was_blocking) {
        sock_set_nonblocking(sock->fd, false);
    }

    /* Save remote address */
    if (sock->remote != NULL) {
        wingo_addr_free(sock->remote);
    }
    sock->remote = wingo_addr_copy(addr);
    sock->state = WINGO_SOCK_STATE_CONNECT;

    return WINGO_SUCCESS;
}

/*
 * Disconnect socket.
 */
wingo_error_t wingo_sock_disconnect(wingo_sock_t *sock)
{
    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (sock->state != WINGO_SOCK_STATE_CONNECT) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (sock->remote != NULL) {
        wingo_addr_free(sock->remote);
        sock->remote = NULL;
    }

    sock->state = WINGO_SOCK_STATE_OPEN;

    return WINGO_SUCCESS;
}
/* ============================================================================
 * SOCKET LISTEN (TCP)
 * ============================================================================ */

/*
 * Listen for incoming connections (TCP).
 */
wingo_error_t wingo_sock_listen(wingo_sock_t *sock, int backlog)
{
    int rc;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (sock->kind != WINGO_SOCK_KIND_TCP) {
        return WINGO_ERR_NOT_SUPPORTED;
    }

    if (sock->state != WINGO_SOCK_STATE_BOUND) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (backlog <= 0) {
        backlog = SOMAXCONN;
    }

    rc = listen(sock->fd, backlog);
    if (rc < 0) {
        sock->last_error = errno_to_error(errno);
        WINGO_LOG_DEBUG("listen() failed: %s", strerror(errno));
        return sock->last_error;
    }

    sock->state = WINGO_SOCK_STATE_LISTEN;

    return WINGO_SUCCESS;
}

/*
 * Accept an incoming connection (TCP).
 *
 * If addr is non-NULL, the remote address is stored there.
 * The caller owns the returned socket and must free it
 * with wingo_sock_free().
 */
wingo_sock_t *wingo_sock_accept(wingo_sock_t *sock, wingo_addr_t **addr)
{
    wingo_sock_t *client;
    struct sockaddr_storage ss;
    socklen_t ss_len;
    int fd;
    int rc;

    if (sock == NULL || sock->fd < 0) {
        return NULL;
    }

    if (sock->kind != WINGO_SOCK_KIND_TCP) {
        return NULL;
    }

    if (sock->state != WINGO_SOCK_STATE_LISTEN) {
        return NULL;
    }

    memset(&ss, 0, sizeof(ss));
    ss_len = sizeof(ss);

    fd = accept(sock->fd, (struct sockaddr *)&ss, &ss_len);
    if (fd < 0) {
        sock->last_error = errno_to_error(errno);

        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            WINGO_LOG_DEBUG("accept() failed: %s", strerror(errno));
        }

        return NULL;
    }

    /* Set close-on-exec */
    rc = sock_set_cloexec(fd);
    if (rc != WINGO_SUCCESS) {
        close(fd);
        return NULL;
    }

    /* Allocate socket struct */
    client = calloc(1, sizeof(wingo_sock_t));
    if (client == NULL) {
        close(fd);
        return NULL;
    }

    client->fd = fd;
    client->kind = WINGO_SOCK_KIND_TCP;
    client->state = WINGO_SOCK_STATE_CONNECT;
    client->local = NULL;
    client->remote = NULL;
    client->last_error = WINGO_SUCCESS;
    client->blocking = sock->blocking;
    client->timeout_ms = sock->timeout_ms;

    /* Non-blocking if parent is non-blocking */
    if (!sock->blocking) {
        sock_set_nonblocking(fd, true);
    }

    /* Save remote address */
    if (addr != NULL) {
        *addr = calloc(1, sizeof(wingo_addr_t));
        if (*addr != NULL) {
            memcpy(&(*addr)->ss, &ss, ss_len);
            (*addr)->ss_len = ss_len;
            (*addr)->ip_str_valid = false;
        }
    }

    /* Save remote address in client too */
    client->remote = calloc(1, sizeof(wingo_addr_t));
    if (client->remote != NULL) {
        memcpy(&client->remote->ss, &ss, ss_len);
        client->remote->ss_len = ss_len;
        client->remote->ip_str_valid = false;
    }

    return client;
}

/* ============================================================================
 * SOCKET SEND
 * ============================================================================ */

/*
 * Send data over socket.
 *
 * For TCP: sends data on connected socket.
 * For UDP: sends to connected address.
 *
 * Returns number of bytes sent, or -1 on error.
 */
wingo_ssize wingo_sock_send(wingo_sock_t *sock,
                             const void *data,
                             wingo_size len)
{
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || data == NULL) {
        return -1;
    }

    if (sock->state != WINGO_SOCK_STATE_CONNECT &&
        sock->state != WINGO_SOCK_STATE_OPEN) {
        return -1;
    }

    n = send(sock->fd, data, len, 0);
    if (n < 0) {
        sock->last_error = errno_to_error(errno);
        return -1;
    }

    return (wingo_ssize)n;
}

/*
 * Send data to address (UDP).
 *
 * Returns number of bytes sent, or -1 on error.
 */
wingo_ssize wingo_sock_sendto(wingo_sock_t *sock,
                               const void *data,
                               wingo_size len,
                               const wingo_addr_t *addr)
{
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || data == NULL || addr == NULL) {
        return -1;
    }

    if (sock->kind != WINGO_SOCK_KIND_UDP) {
        return -1;
    }

    n = sendto(sock->fd,
               data, len, 0,
               (const struct sockaddr *)&addr->ss,
               addr->ss_len);

    if (n < 0) {
        sock->last_error = errno_to_error(errno);
        return -1;
    }

    return (wingo_ssize)n;
}

/*
 * Send all data over socket.
 *
 * This loops until all data is sent.
 * For non-blocking sockets, this uses poll() to wait.
 */
wingo_error_t wingo_sock_sendall(wingo_sock_t *sock,
                                  const void *data,
                                  wingo_size len)
{
    const wingo_u8 *ptr = (const wingo_u8 *)data;
    wingo_size remaining = len;
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || data == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    while (remaining > 0) {
        n = send(sock->fd, ptr, remaining, 0);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Wait for writable */
                wingo_error_t rc;

                if (sock->timeout_ms > 0) {
                    rc = sock_wait(sock->fd, POLLOUT, sock->timeout_ms);
                } else {
                    rc = sock_wait(sock->fd, POLLOUT, -1);
                }

                if (rc != WINGO_SUCCESS) {
                    sock->last_error = rc;
                    return rc;
                }

                continue;
            }

            sock->last_error = errno_to_error(errno);
            return sock->last_error;
        }

        if (n == 0) {
            break;
        }

        ptr += n;
        remaining -= (wingo_size)n;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SOCKET RECEIVE
 * ============================================================================ */

/*
 * Receive data from socket.
 *
 * Returns number of bytes received, 0 on close, -1 on error.
 */
wingo_ssize wingo_sock_recv(wingo_sock_t *sock, void *buf, wingo_size len)
{
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || buf == NULL) {
        return -1;
    }

    n = recv(sock->fd, buf, len, 0);
    if (n < 0) {
        sock->last_error = errno_to_error(errno);
        return -1;
    }

    return (wingo_ssize)n;
}

/*
 * Receive data from address (UDP).
 *
 * If addr is non-NULL, the source address is stored there.
 * The caller owns the returned address and must free it.
 */
wingo_ssize wingo_sock_recvfrom(wingo_sock_t *sock,
                                 void *buf,
                                 wingo_size len,
                                 wingo_addr_t **addr)
{
    struct sockaddr_storage ss;
    socklen_t ss_len;
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || buf == NULL) {
        return -1;
    }

    memset(&ss, 0, sizeof(ss));
    ss_len = sizeof(ss);

    n = recvfrom(sock->fd, buf, len, 0,
                 (struct sockaddr *)&ss, &ss_len);

    if (n < 0) {
        sock->last_error = errno_to_error(errno);
        return -1;
    }

    /* Save source address */
    if (addr != NULL) {
        *addr = calloc(1, sizeof(wingo_addr_t));
        if (*addr != NULL) {
            memcpy(&(*addr)->ss, &ss, ss_len);
            (*addr)->ss_len = ss_len;
            (*addr)->ip_str_valid = false;
        }
    }

    return (wingo_ssize)n;
}

/*
 * Receive data with timeout.
 *
 * Returns:
 *   > 0  — number of bytes received
 *   0    — timeout
 *   -1   — error
 */
wingo_ssize wingo_sock_recv_timeout(wingo_sock_t *sock,
                                     void *buf,
                                     wingo_size len,
                                     wingo_i64 timeout_ms)
{
    wingo_error_t rc;
    ssize_t n;

    if (sock == NULL || sock->fd < 0 || buf == NULL) {
        return -1;
    }

    /* Wait for readable */
    rc = sock_wait(sock->fd, POLLIN, timeout_ms);
    if (rc == WINGO_ERR_TIMEOUT) {
        return 0;
    }
    if (rc != WINGO_SUCCESS) {
        sock->last_error = rc;
        return -1;
    }

    n = recv(sock->fd, buf, len, 0);
    if (n < 0) {
        sock->last_error = errno_to_error(errno);
        return -1;
    }

    return (wingo_ssize)n;
}

/* ============================================================================
 * SOCKET OPTIONS
 * ============================================================================ */

/*
 * Set socket blocking mode.
 */
wingo_error_t wingo_sock_set_blocking(wingo_sock_t *sock, bool blocking)
{
    wingo_error_t rc;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = sock_set_nonblocking(sock->fd, !blocking);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    sock->blocking = blocking;

    return WINGO_SUCCESS;
}

/*
 * Set socket reuse address.
 */
wingo_error_t wingo_sock_set_reuseaddr(wingo_sock_t *sock, bool reuse)
{
    int opt = reuse ? 1 : 0;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
}

/*
 * Set socket reuse port.
 */
wingo_error_t wingo_sock_set_reuseport(wingo_sock_t *sock, bool reuse)
{
#ifdef SO_REUSEPORT
    int opt = reuse ? 1 : 0;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT,
                   &opt, sizeof(opt)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
#else
    WINGO_UNUSED(sock);
    WINGO_UNUSED(reuse);
    return WINGO_ERR_NOT_SUPPORTED;
#endif
}

/*
 * Set socket keepalive.
 */
wingo_error_t wingo_sock_set_keepalive(wingo_sock_t *sock, bool keepalive)
{
    int opt = keepalive ? 1 : 0;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE,
                   &opt, sizeof(opt)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
}

/*
 * Set socket send buffer size.
 */
wingo_error_t wingo_sock_set_sndbuf(wingo_sock_t *sock, int size)
{
    if (sock == NULL || sock->fd < 0 || size < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF,
                   &size, sizeof(size)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
}

/*
 * Set socket receive buffer size.
 */
wingo_error_t wingo_sock_set_rcvbuf(wingo_sock_t *sock, int size)
{
    if (sock == NULL || sock->fd < 0 || size < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
                   &size, sizeof(size)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
}

/*
 * Set socket timeout.
 *
 * This applies to send/recv operations.
 * A timeout of 0 means no timeout (blocking forever).
 */
wingo_error_t wingo_sock_set_timeout(wingo_sock_t *sock, wingo_i64 timeout_ms)
{
    struct timeval tv;

    if (sock == NULL || sock->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (timeout_ms < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    sock->timeout_ms = timeout_ms;

    /* Set SO_RCVTIMEO and SO_SNDTIMEO */
    if (timeout_ms > 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO,
                   &tv, sizeof(tv)) < 0) {
        sock->last_error = errno_to_error(errno);
        return sock->last_error;
    }

    return WINGO_SUCCESS;
}
/* ============================================================================
 * SOCKET QUERY
 * ============================================================================ */

/*
 * Get socket kind.
 */
wingo_sock_kind_t wingo_sock_kind(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return WINGO_SOCK_KIND_UDP;
    }

    return sock->kind;
}

/*
 * Get socket state.
 */
wingo_sock_state_t wingo_sock_state(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return WINGO_SOCK_STATE_CLOSED;
    }

    return sock->state;
}

/*
 * Get socket file descriptor.
 */
int wingo_sock_fd(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return -1;
    }

    return sock->fd;
}

/*
 * Get local address.
 *
 * Returns a new address (caller owns it).
 * Returns NULL if not bound.
 */
wingo_addr_t *wingo_sock_local_addr(const wingo_sock_t *sock)
{
    struct sockaddr_storage ss;
    socklen_t ss_len;
    wingo_addr_t *addr;

    if (sock == NULL || sock->fd < 0) {
        return NULL;
    }

    /* If we cached it, return a copy */
    if (sock->local != NULL) {
        return wingo_addr_copy(sock->local);
    }

    /* Otherwise query the kernel */
    memset(&ss, 0, sizeof(ss));
    ss_len = sizeof(ss);

    if (getsockname(sock->fd, (struct sockaddr *)&ss, &ss_len) < 0) {
        return NULL;
    }

    addr = calloc(1, sizeof(wingo_addr_t));
    if (addr == NULL) {
        return NULL;
    }

    memcpy(&addr->ss, &ss, ss_len);
    addr->ss_len = ss_len;
    addr->ip_str_valid = false;

    return addr;
}

/*
 * Get remote address.
 *
 * Returns a new address (caller owns it).
 * Returns NULL if not connected.
 */
wingo_addr_t *wingo_sock_remote_addr(const wingo_sock_t *sock)
{
    struct sockaddr_storage ss;
    socklen_t ss_len;
    wingo_addr_t *addr;

    if (sock == NULL || sock->fd < 0) {
        return NULL;
    }

    /* If we cached it, return a copy */
    if (sock->remote != NULL) {
        return wingo_addr_copy(sock->remote);
    }

    /* Otherwise query the kernel */
    memset(&ss, 0, sizeof(ss));
    ss_len = sizeof(ss);

    if (getpeername(sock->fd, (struct sockaddr *)&ss, &ss_len) < 0) {
        return NULL;
    }

    addr = calloc(1, sizeof(wingo_addr_t));
    if (addr == NULL) {
        return NULL;
    }

    memcpy(&addr->ss, &ss, ss_len);
    addr->ss_len = ss_len;
    addr->ip_str_valid = false;

    return addr;
}

/*
 * Check if socket is open.
 */
bool wingo_sock_is_open(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return false;
    }

    return sock->fd >= 0 && sock->state != WINGO_SOCK_STATE_CLOSED;
}

/*
 * Check if socket is connected.
 */
bool wingo_sock_is_connected(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return false;
    }

    return sock->state == WINGO_SOCK_STATE_CONNECT;
}

/* ============================================================================
 * SOCKET ERROR
 * ============================================================================ */

/*
 * Get last socket error.
 */
wingo_error_t wingo_sock_last_error(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return sock->last_error;
}

/*
 * Clear socket error.
 */
void wingo_sock_clear_error(wingo_sock_t *sock)
{
    if (sock == NULL) {
        return;
    }

    sock->last_error = WINGO_SUCCESS;
}

/*
 * Check if socket has error.
 */
bool wingo_sock_has_error(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return true;
    }

    return sock->last_error != WINGO_SUCCESS;
}

/*
 * Get socket error string.
 */
wingo_error_t wingo_sock_error_str(const wingo_sock_t *sock,
                                    char *buf, wingo_size size)
{
    int n;

    if (sock == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    n = snprintf(buf, size, "%s", wingo_error_str(sock->last_error));

    if (n < 0 || (wingo_size)n >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SOCKET EPOLL INTEGRATION
 * ============================================================================ */

/*
 * Get socket for epoll registration.
 *
 * This is just the file descriptor, but we provide it
 * as a function for clarity and future-proofing.
 */
int wingo_sock_epoll_fd(const wingo_sock_t *sock)
{
    if (sock == NULL) {
        return -1;
    }

    return sock->fd;
}

/* ============================================================================
 * INTERNAL: GET SOCKADDR FOR SEND/RECV
 * ============================================================================ */

/*
 * Get raw sockaddr from wingo_addr_t.
 *
 * This is used internally by callers that need raw sockaddr.
 * It is exposed here for advanced use cases (e.g., epoll integration).
 *
 * @param addr      Address
 * @param out_ss    Output sockaddr_storage
 * @param out_len   Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_addr_to_sockaddr(const wingo_addr_t *addr,
                                      struct sockaddr_storage *out_ss,
                                      socklen_t *out_len)
{
    if (addr == NULL || out_ss == NULL || out_len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(out_ss, &addr->ss, addr->ss_len);
    *out_len = addr->ss_len;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
