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
 * Linux Platform Implementation
 *
 * This file provides Linux-specific implementations of the
 * platform abstraction API.
 *
 * It handles:
 *   - TUN/TAP interface (/dev/net/tun)
 *   - Network interface configuration (via ioctl)
 *   - Routing (via netlink or ioctl)
 *   - iptables/nftables (for NAT/gateway)
 *   - PID file management
 *   - Daemonization
 *
 * NOTE: This file only compiles on Linux.
 */

#include "wingo/platform/platform.h"

#if WINGO_PLATFORM_LINUX

#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/util/buffer.h"
#include "wingo/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <linux/if.h>
#include <linux/route.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * TUN interface (Linux-specific).
 */
struct wingo_tun {
    int         fd;                     /* /dev/net/tun fd */
    char        name[IFNAMSIZ];         /* Interface name */
    int         mtu;                    /* MTU */
    bool        configured;             /* IP configured? */
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Open /dev/net/tun and create the TUN interface.
 */
static int tun_alloc(char *name, int flags)
{
    struct ifreq ifr;
    int fd;
    int err;

    /* Open TUN device */
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        WINGO_LOG_ERROR("Failed to open /dev/net/tun: %s", strerror(errno));
        return -1;
    }

    /* Configure TUN */
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;

    if (name != NULL && name[0] != '\0') {
        strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    }

    err = ioctl(fd, TUNSETIFF, &ifr);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETIFF failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /* Return actual name */
    if (name != NULL) {
        strncpy(name, ifr.ifr_name, IFNAMSIZ - 1);
        name[IFNAMSIZ - 1] = '\0';
    }

    return fd;
}

/*
 * Set interface MTU.
 */
static int set_mtu(const char *name, int mtu)
{
    struct ifreq ifr;
    int fd;
    int err;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    ifr.ifr_mtu = mtu;

    err = ioctl(fd, SIOCSIFMTU, &ifr);
    close(fd);

    if (err < 0) {
        WINGO_LOG_ERROR("SIOCSIFMTU failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Set interface IP address and netmask.
 */
static int set_ipv4(const char *name, const char *addr, const char *netmask)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int fd;
    int err;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    /* Set address */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, addr, &sin->sin_addr);

    err = ioctl(fd, SIOCSIFADDR, &ifr);
    if (err < 0) {
        WINGO_LOG_ERROR("SIOCSIFADDR failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /* Set netmask */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_netmask;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, netmask, &sin->sin_addr);

    err = ioctl(fd, SIOCSIFNETMASK, &ifr);
    if (err < 0) {
        WINGO_LOG_ERROR("SIOCSIFNETMASK failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/*
 * Bring interface up.
 */
static int set_up(const char *name)
{
    struct ifreq ifr;
    int fd;
    int err;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if (err < 0) {
        close(fd);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

    err = ioctl(fd, SIOCSIFFLAGS, &ifr);
    close(fd);

    if (err < 0) {
        WINGO_LOG_ERROR("SIOCSIFFLAGS failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Add route.
 */
static int add_route(const char *ifname, const char *dest, const char *netmask)
{
    struct rtentry route;
    struct sockaddr_in *addr;
    int fd;
    int err;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&route, 0, sizeof(route));

    /* Destination */
    addr = (struct sockaddr_in *)&route.rt_dst;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, dest, &addr->sin_addr);

    /* Netmask */
    addr = (struct sockaddr_in *)&route.rt_genmask;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, netmask, &addr->sin_addr);

    /* Gateway (none — direct route) */
    route.rt_flags = RTF_UP | RTF_HOST;
    route.rt_dev = (char *)ifname;

    err = ioctl(fd, SIOCADDRT, &route);
    close(fd);

    if (err < 0) {
        WINGO_LOG_ERROR("SIOCADDRT failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* ============================================================================
 * TUN LIFECYCLE
 * ============================================================================ */

wingo_tun_t *wingo_tun_open(const wingo_tun_config_t *config)
{
    wingo_tun_config_t default_config;
    wingo_tun_t *tun;
    char name[IFNAMSIZ];
    int fd;
    int flags;

    /* Use default config if none provided */
    if (config == NULL) {
        wingo_tun_config_default(&default_config);
        config = &default_config;
    }

    /* Copy name */
    if (config->name != NULL) {
        strncpy(name, config->name, IFNAMSIZ - 1);
        name[IFNAMSIZ - 1] = '\0';
    } else {
        name[0] = '\0';
    }

    /* Allocate TUN */
    flags = IFF_TUN | IFF_NO_PI;
    fd = tun_alloc(name, flags);
    if (fd < 0) {
        return NULL;
    }

    WINGO_LOG_INFO("TUN interface %s opened (fd=%d)", name, fd);

    /* Set non-blocking */
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
    }

    /* Set MTU */
    if (config->mtu > 0) {
        if (set_mtu(name, config->mtu) < 0) {
            WINGO_LOG_WARN("Failed to set MTU on %s", name);
        }
    }

    /* Set IPv4 */
    if (config->ipv4_addr != NULL) {
        if (set_ipv4(name, config->ipv4_addr,
                     config->ipv4_netmask ? config->ipv4_netmask : "255.255.255.0") < 0) {
            WINGO_LOG_WARN("Failed to set IPv4 on %s", name);
        }
    }

    /* Bring up */
    if (set_up(name) < 0) {
        WINGO_LOG_WARN("Failed to bring up %s", name);
    }

    /* Allocate handle */
    tun = calloc(1, sizeof(wingo_tun_t));
    if (tun == NULL) {
        close(fd);
        return NULL;
    }

    tun->fd = fd;
    tun->mtu = config->mtu > 0 ? config->mtu : WINGO_MTU;
    tun->configured = true;
    strncpy(tun->name, name, IFNAMSIZ - 1);
    tun->name[IFNAMSIZ - 1] = '\0';

    WINGO_LOG_INFO("TUN %s configured (MTU=%d, IP=%s)",
                   tun->name, tun->mtu,
                   config->ipv4_addr ? config->ipv4_addr : "none");

    return tun;
}

void wingo_tun_close(wingo_tun_t *tun)
{
    if (tun == NULL) {
        return;
    }

    if (tun->fd >= 0) {
        close(tun->fd);
        tun->fd = -1;
    }

    WINGO_LOG_DEBUG("TUN %s closed", tun->name);

    free(tun);
}

/* ============================================================================
 * TUN I/O
 * ============================================================================ */

int wingo_tun_read(wingo_tun_t *tun, void *buf, wingo_size len)
{
    ssize_t n;

    if (tun == NULL || tun->fd < 0 || buf == NULL) {
        return -1;
    }

    n = read(tun->fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* No data available */
        }
        return -1;
    }

    return (int)n;
}

int wingo_tun_write(wingo_tun_t *tun, const void *buf, wingo_size len)
{
    ssize_t n;

    if (tun == NULL || tun->fd < 0 || buf == NULL) {
        return -1;
    }

    n = write(tun->fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* Would block */
        }
        return -1;
    }

    return (int)n;
}

/* ============================================================================
 * TUN QUERY
 * ============================================================================ */

int wingo_tun_get_fd(const wingo_tun_t *tun)
{
    if (tun == NULL) {
        return -1;
    }

    return tun->fd;
}

const char *wingo_tun_get_name(const wingo_tun_t *tun)
{
    if (tun == NULL) {
        return NULL;
    }

    return tun->name;
}

int wingo_tun_get_mtu(const wingo_tun_t *tun)
{
    if (tun == NULL) {
        return 0;
    }

    return tun->mtu;
}

/* ============================================================================
 * PLATFORM INITIALIZATION (LINUX)
 * ============================================================================ */

/*
 * These are already implemented in the core platform.c.
 * We don't need to re-implement them here.
 *
 * But if we need Linux-specific init, we'd add it here.
 */

/* ============================================================================
 * IPTABLES / NFTABLES
 * ============================================================================ */

/*
 * Execute a command and wait for it to finish.
 *
 * Returns the exit code, or -1 on error.
 */
static int run_command(const char *cmd)
{
    int status;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        /* Child */
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent: wait for child */
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

/*
 * Enable IP forwarding.
 */
wingo_error_t wingo_linux_enable_ip_forward(void)
{
    int rc = run_command("echo 1 > /proc/sys/net/ipv4/ip_forward");

    if (rc != 0) {
        WINGO_LOG_ERROR("Failed to enable IP forwarding");
        return WINGO_ERR_PERMISSION;
    }

    WINGO_LOG_INFO("IP forwarding enabled");
    return WINGO_SUCCESS;
}

/*
 * Disable IP forwarding.
 */
wingo_error_t wingo_linux_disable_ip_forward(void)
{
    int rc = run_command("echo 0 > /proc/sys/net/ipv4/ip_forward");

    if (rc != 0) {
        WINGO_LOG_ERROR("Failed to disable IP forwarding");
        return WINGO_ERR_PERMISSION;
    }

    WINGO_LOG_INFO("IP forwarding disabled");
    return WINGO_SUCCESS;
}

/*
 * Add MASQUERADE rule (NAT).
 */
wingo_error_t wingo_linux_add_masquerade(const char *out_if)
{
    char cmd[512];
    int rc;

    if (out_if == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    snprintf(cmd, sizeof(cmd),
             "iptables -t nat -A POSTROUTING -o %s -j MASQUERADE",
             out_if);

    rc = run_command(cmd);
    if (rc != 0) {
        WINGO_LOG_ERROR("Failed to add MASQUERADE rule for %s", out_if);
        return WINGO_ERR_PERMISSION;
    }

    WINGO_LOG_INFO("MASQUERADE rule added for %s", out_if);
    return WINGO_SUCCESS;
}

/*
 * Remove MASQUERADE rule.
 */
wingo_error_t wingo_linux_remove_masquerade(const char *out_if)
{
    char cmd[512];
    int rc;

    if (out_if == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    snprintf(cmd, sizeof(cmd),
             "iptables -t nat -D POSTROUTING -o %s -j MASQUERADE",
             out_if);

    rc = run_command(cmd);
    if (rc != 0) {
        WINGO_LOG_WARN("Failed to remove MASQUERADE rule for %s", out_if);
        return WINGO_ERR_NOT_FOUND;
    }

    WINGO_LOG_INFO("MASQUERADE rule removed for %s", out_if);
    return WINGO_SUCCESS;
}

/*
 * Add FORWARD rules.
 */
wingo_error_t wingo_linux_add_forward_rules(const char *in_if, const char *out_if)
{
    char cmd[512];
    int rc;

    if (in_if == NULL || out_if == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Allow forward from in_if to out_if */
    snprintf(cmd, sizeof(cmd),
             "iptables -A FORWARD -i %s -o %s -j ACCEPT",
             in_if, out_if);
    rc = run_command(cmd);
    if (rc != 0) {
        WINGO_LOG_ERROR("Failed to add FORWARD rule (%s -> %s)",
                        in_if, out_if);
        return WINGO_ERR_PERMISSION;
    }

    /* Allow forward from out_if to in_if (established) */
    snprintf(cmd, sizeof(cmd),
             "iptables -A FORWARD -i %s -o %s -m state --state RELATED,ESTABLISHED -j ACCEPT",
             out_if, in_if);
    rc = run_command(cmd);
    if (rc != 0) {
        WINGO_LOG_WARN("Failed to add FORWARD rule (%s -> %s)",
                       out_if, in_if);
    }

    WINGO_LOG_INFO("FORWARD rules added (%s <-> %s)", in_if, out_if);
    return WINGO_SUCCESS;
}

/*
 * Remove FORWARD rules.
 */
wingo_error_t wingo_linux_remove_forward_rules(const char *in_if, const char *out_if)
{
    char cmd[512];

    if (in_if == NULL || out_if == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    snprintf(cmd, sizeof(cmd),
             "iptables -D FORWARD -i %s -o %s -j ACCEPT",
             in_if, out_if);
    run_command(cmd);

    snprintf(cmd, sizeof(cmd),
             "iptables -D FORWARD -i %s -o %s -m state --state RELATED,ESTABLISHED -j ACCEPT",
             out_if, in_if);
    run_command(cmd);

    WINGO_LOG_INFO("FORWARD rules removed (%s <-> %s)", in_if, out_if);
    return WINGO_SUCCESS;
}

/* ============================================================================
 * PID FILE
 * ============================================================================ */

wingo_error_t wingo_linux_write_pidfile(const char *path)
{
    FILE *f;
    pid_t pid;

    if (path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pid = getpid();

    f = fopen(path, "w");
    if (f == NULL) {
        WINGO_LOG_ERROR("Failed to open PID file %s: %s",
                        path, strerror(errno));
        return WINGO_ERR_FILE_OPEN;
    }

    fprintf(f, "%d\n", pid);
    fclose(f);

    WINGO_LOG_DEBUG("PID file written: %s (%d)", path, pid);
    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_remove_pidfile(const char *path)
{
    if (path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (unlink(path) != 0) {
        if (errno != ENOENT) {
            WINGO_LOG_WARN("Failed to remove PID file %s: %s",
                           path, strerror(errno));
            return WINGO_ERR_FILE_OPEN;
        }
    }

    return WINGO_SUCCESS;
}

wingo_i64 wingo_linux_read_pidfile(const char *path)
{
    FILE *f;
    long pid;

    if (path == NULL) {
        return -1;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }

    if (fscanf(f, "%ld", &pid) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return (wingo_i64)pid;
}

/* ============================================================================
 * NETWORK INTERFACE INFO
 * ============================================================================ */

/*
 * Get default gateway interface.
 */
wingo_error_t wingo_linux_get_default_if(char *buf, wingo_size size)
{
    FILE *f;
    char line[256];
    char iface[64];
    unsigned long dest, gw, flags;
    int refcnt, use, metric, mask, mtu, window, irtt;

    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen("/proc/net/route", "r");
    if (f == NULL) {
        return WINGO_ERR_FILE_OPEN;
    }

    /* Skip header */
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return WINGO_ERR_FILE_READ;
    }

    /* Find default route (destination 0.0.0.0) */
    while (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "%63s %lx %lx %lx %d %d %d %x %d %d %d",
                   iface, &dest, &gw, &flags, &refcnt, &use,
                   &metric, &mask, &mtu, &window, &irtt) >= 1) {
            if (dest == 0) {
                strncpy(buf, iface, size - 1);
                buf[size - 1] = '\0';
                fclose(f);
                return WINGO_SUCCESS;
            }
        }
    }

    fclose(f);
    return WINGO_ERR_NOT_FOUND;
}

/*
 * Get interface IP address.
 */
wingo_error_t wingo_linux_get_if_ip(const char *ifname, char *buf, wingo_size size)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int fd;
    int err;

    if (ifname == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WINGO_ERR_NET_SOCKET;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    if (err < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    if (inet_ntop(AF_INET, &sin->sin_addr, buf, size) == NULL) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/*
 * Get interface MAC address.
 */
wingo_error_t wingo_linux_get_if_mac(const char *ifname, char *buf, wingo_size size)
{
    struct ifreq ifr;
    unsigned char *mac;
    int fd;
    int err;

    if (ifname == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WINGO_ERR_NET_SOCKET;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFHWADDR, &ifr);
    close(fd);

    if (err < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
    snprintf(buf, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return WINGO_SUCCESS;
}

/*
 * Check if interface exists.
 */
bool wingo_linux_if_exists(const char *ifname)
{
    struct ifreq ifr;
    int fd;
    int err;

    if (ifname == NULL) {
        return false;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFFLAGS, &ifr);
    close(fd);

    return err == 0;
}

/*
 * Check if interface is up.
 */
bool wingo_linux_if_is_up(const char *ifname)
{
    struct ifreq ifr;
    int fd;
    int err;

    if (ifname == NULL) {
        return false;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFFLAGS, &ifr);
    close(fd);

    if (err < 0) {
        return false;
    }

    return (ifr.ifr_flags & IFF_UP) != 0;
}

#endif /* WINGO_PLATFORM_LINUX */
