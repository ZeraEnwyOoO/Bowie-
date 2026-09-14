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
 * This file provides Linux-specific implementations of:
 *   - TUN/TAP interface management (/dev/net/tun)
 *   - Interface configuration (IP, MTU, MAC)
 *   - Routing (ioctl)
 *   - IP forwarding
 *   - iptables (NAT, FORWARD)
 *   - PID file
 *   - Daemonization
 *   - Network information
 *
 * NOTE: This file only compiles on Linux.
 */

#include "wingo/platform/platform.h"
#include "platforms/linux/platform.h"

#if WINGO_PLATFORM_LINUX

#include "wingo/error.h"
#include "wingo/log.h"
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
 *
 * The actual struct wingo_tun_t is opaque (defined in platform.h).
 * Here we define the concrete layout.
 */
struct wingo_tun {
    int         fd;                     /* /dev/net/tun fd */
    char        name[IFNAMSIZ];         /* Interface name */
    int         mtu;                    /* MTU */
    bool        configured;             /* IP configured? */
    bool        persistent;             /* Persistent TUN? */
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Open /dev/net/tun and create the TUN interface.
 */
static int tun_alloc(const char *name, int flags)
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

    /* Set non-blocking */
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
    }

    /* Set close-on-exec */
    {
        int fl = fcntl(fd, F_GETFD, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
        }
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
 * Execute a shell command.
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
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
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
    tun->persistent = false;
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
            return 0;
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
            return 0;
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
    return tun ? tun->fd : -1;
}

const char *wingo_tun_get_name(const wingo_tun_t *tun)
{
    return tun ? tun->name : NULL;
}

int wingo_tun_get_mtu(const wingo_tun_t *tun)
{
    return tun ? tun->mtu : 0;
}

/* ============================================================================
 * LINUX-SPECIFIC TUN API
 * ============================================================================ */

wingo_tun_t *wingo_linux_tun_open(const char *name, int flags)
{
    wingo_tun_t *tun;
    char ifname[IFNAMSIZ];
    int fd;

    if (name != NULL) {
        strncpy(ifname, name, IFNAMSIZ - 1);
        ifname[IFNAMSIZ - 1] = '\0';
    } else {
        ifname[0] = '\0';
    }

    fd = tun_alloc(ifname, flags);
    if (fd < 0) {
        return NULL;
    }

    tun = calloc(1, sizeof(wingo_tun_t));
    if (tun == NULL) {
        close(fd);
        return NULL;
    }

    tun->fd = fd;
    tun->mtu = WINGO_MTU;
    tun->configured = false;
    tun->persistent = false;
    strncpy(tun->name, ifname, IFNAMSIZ - 1);
    tun->name[IFNAMSIZ - 1] = '\0';

    return tun;
}

wingo_error_t wingo_linux_tun_set_persistent(wingo_tun_t *tun)
{
    int err;

    if (tun == NULL || tun->fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    err = ioctl(tun->fd, TUNSETPERSIST, 1);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETPERSIST failed: %s", strerror(errno));
        return WINGO_ERR_PERMISSION;
    }

    tun->persistent = true;

    WINGO_LOG_INFO("TUN %s marked as persistent", tun->name);
    return WINGO_SUCCESS;
}

/* ============================================================================
 * LINUX NETWORK CONFIGURATION
 * ============================================================================ */

wingo_error_t wingo_linux_if_set_ipv4(const char *ifname,
                                      const char *addr,
                                      const char *netmask)
{
    if (ifname == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (set_ipv4(ifname, addr, netmask ? netmask : "255.255.255.0") < 0) {
        return WINGO_ERR_PERMISSION;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_if_set_mtu(const char *ifname, int mtu)
{
    if (ifname == NULL || mtu <= 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (set_mtu(ifname, mtu) < 0) {
        return WINGO_ERR_PERMISSION;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_if_up(const char *ifname)
{
    if (ifname == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (set_up(ifname) < 0) {
        return WINGO_ERR_PERMISSION;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_if_down(const char *ifname)
{
    struct ifreq ifr;
    int fd;
    int err;

    if (ifname == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WINGO_ERR_NET_SOCKET;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    err = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if (err < 0) {
        close(fd);
        return WINGO_ERR_NOT_FOUND;
    }

    ifr.ifr_flags &= ~IFF_UP;

    err = ioctl(fd, SIOCSIFFLAGS, &ifr);
    close(fd);

    if (err < 0) {
        return WINGO_ERR_PERMISSION;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * LINUX INTERFACE QUERY
 * ============================================================================ */

wingo_error_t wingo_linux_if_get_ipv4(const char *ifname,
                                      char *buf, wingo_size size)
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

wingo_error_t wingo_linux_if_get_mac(const char *ifname,
                                     char *buf, wingo_size size)
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

/* ============================================================================
 * IP FORWARDING
 * ============================================================================ */

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

bool wingo_linux_is_ip_forward_enabled(void)
{
    FILE *f;
    int value = 0;

    f = fopen("/proc/sys/net/ipv4/ip_forward", "r");
    if (f == NULL) {
        return false;
    }

    if (fscanf(f, "%d", &value) != 1) {
        value = 0;
    }

    fclose(f);
    return value == 1;
}

/* ============================================================================
 * IPTABLES (NAT)
 * ============================================================================ */

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

bool wingo_linux_is_process_running(wingo_i64 pid)
{
    if (pid <= 0) {
        return false;
    }

    if (kill((pid_t)pid, 0) == 0) {
        return true;
    }

    return errno == EPERM;
}

/* ============================================================================
 * DAEMONIZATION
 * ============================================================================ */

wingo_error_t wingo_linux_daemonize(void)
{
    pid_t pid;
    int fd;

    /* Fork #1 */
    pid = fork();
    if (pid < 0) {
        return WINGO_ERR_GENERIC;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Become session leader */
    if (setsid() < 0) {
        return WINGO_ERR_GENERIC;
    }

    /* Fork #2 */
    pid = fork();
    if (pid < 0) {
        return WINGO_ERR_GENERIC;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change working directory */
    if (chdir("/") < 0) {
        return WINGO_ERR_GENERIC;
    }

    /* Redirect stdio */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }

    umask(027);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * NETWORK INFORMATION
 * ============================================================================ */

int wingo_linux_list_interfaces(wingo_linux_if_info_t *out, int max)
{
    FILE *f;
    char line[256];
    int count = 0;

    if (out == NULL || max <= 0) {
        return -1;
    }

    f = fopen("/proc/net/dev", "r");
    if (f == NULL) {
        return -1;
    }

    /* Skip headers */
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return -1;
    }
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL && count < max) {
        char name[IFNAMSIZ];
        unsigned long rx_bytes, rx_packets;

        if (sscanf(line, " %15[^:]: %lu %lu",
                   name, &rx_bytes, &rx_packets) >= 1) {
            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, name, IFNAMSIZ - 1);
            out[count].is_up = wingo_linux_if_is_up(name);
            out[count].is_loopback = (strcmp(name, "lo") == 0);
            count++;
        }
    }

    fclose(f);
    return count;
}

wingo_error_t wingo_linux_get_interface_info(const char *ifname,
                                             wingo_linux_if_info_t *out)
{
    if (ifname == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->name, ifname, IFNAMSIZ - 1);

    wingo_linux_if_get_ipv4(ifname, out->ipv4, sizeof(out->ipv4));
    wingo_linux_if_get_mac(ifname, out->mac, sizeof(out->mac));

    out->is_up = wingo_linux_if_is_up(ifname);
    out->is_loopback = (strcmp(ifname, "lo") == 0);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_get_hostname(char *buf, wingo_size size)
{
    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (gethostname(buf, size) != 0) {
        return WINGO_ERR_GENERIC;
    }

    buf[size - 1] = '\0';

    return WINGO_SUCCESS;
}

wingo_i64 wingo_linux_get_uptime(void)
{
    FILE *f;
    double uptime;

    f = fopen("/proc/uptime", "r");
    if (f == NULL) {
        return -1;
    }

    if (fscanf(f, "%lf", &uptime) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return (wingo_i64)uptime;
}

wingo_error_t wingo_linux_get_loadavg(double load[3])
{
    FILE *f;

    if (load == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen("/proc/loadavg", "r");
    if (f == NULL) {
        return WINGO_ERR_FILE_OPEN;
    }

    if (fscanf(f, "%lf %lf %lf", &load[0], &load[1], &load[2]) != 3) {
        fclose(f);
        return WINGO_ERR_FILE_READ;
    }

    fclose(f);
    return WINGO_SUCCESS;
}

/* ============================================================================
 * ROUTING
 * ============================================================================ */

wingo_error_t wingo_linux_route_add(const char *ifname,
                                    const char *dest,
                                    const char *netmask,
                                    const char *gateway)
{
    struct rtentry route;
    struct sockaddr_in *addr;
    int fd;
    int err;

    if (ifname == NULL || dest == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WINGO_ERR_NET_SOCKET;
    }

    memset(&route, 0, sizeof(route));

    /* Destination */
    addr = (struct sockaddr_in *)&route.rt_dst;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, dest, &addr->sin_addr);

    /* Netmask */
    addr = (struct sockaddr_in *)&route.rt_genmask;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, netmask ? netmask : "255.255.255.255", &addr->sin_addr);

    /* Gateway */
    if (gateway != NULL) {
        addr = (struct sockaddr_in *)&route.rt_gateway;
        addr->sin_family = AF_INET;
        inet_pton(AF_INET, gateway, &addr->sin_addr);
        route.rt_flags = RTF_UP | RTF_GATEWAY;
    } else {
        route.rt_flags = RTF_UP | RTF_HOST;
    }

    route.rt_dev = (char *)ifname;

    err = ioctl(fd, SIOCADDRT, &route);
    close(fd);

    if (err < 0) {
        WINGO_LOG_ERROR("SIOCADDRT failed: %s", strerror(errno));
        return WINGO_ERR_PERMISSION;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_route_remove(const char *ifname,
                                       const char *dest,
                                       const char *netmask)
{
    struct rtentry route;
    struct sockaddr_in *addr;
    int fd;
    int err;

    if (ifname == NULL || dest == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WINGO_ERR_NET_SOCKET;
    }

    memset(&route, 0, sizeof(route));

    addr = (struct sockaddr_in *)&route.rt_dst;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, dest, &addr->sin_addr);

    addr = (struct sockaddr_in *)&route.rt_genmask;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, netmask ? netmask : "255.255.255.255", &addr->sin_addr);

    route.rt_dev = (char *)ifname;

    err = ioctl(fd, SIOCDELRT, &route);
    close(fd);

    if (err < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_linux_route_get_default_if(char *buf, wingo_size size)
{
    FILE *f;
    char line[256];

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

    /* Find default route */
    while (fgets(line, sizeof(line), f) != NULL) {
        char iface[64];
        unsigned long dest;

        if (sscanf(line, "%63s %lx", iface, &dest) >= 2) {
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

/* ============================================================================
 * PLATFORM INFO (LINUX)
 * ============================================================================ */

wingo_error_t wingo_platform_init(void)
{
    WINGO_LOG_DEBUG("Initializing Linux platform");

    /* Check TUN available */
    if (!wingo_platform_has_tun()) {
        WINGO_LOG_WARN("TUN not available at /dev/net/tun");
    }

    return WINGO_SUCCESS;
}

void wingo_platform_shutdown(void)
{
    WINGO_LOG_DEBUG("Shutting down Linux platform");
}

#endif /* WINGO_PLATFORM_LINUX */
