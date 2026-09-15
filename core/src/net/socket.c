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
    if (addr == NULL || addr->ss.ss_family != AF_INET6) {
        return false;
    }

    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr->ss;
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
