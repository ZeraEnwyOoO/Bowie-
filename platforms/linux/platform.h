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

#ifndef WINGO_PLATFORM_LINUX_PLATFORM_H
#define WINGO_PLATFORM_LINUX_PLATFORM_H

/*
 * ============================================================================
 * LINUX PLATFORM API
 * ============================================================================
 *
 * This header provides Linux-specific APIs.
 *
 * It includes:
 *   - TUN interface management
 *   - Interface configuration (IP, MTU, MAC)
 *   - Routing
 *   - IP forwarding
 *   - iptables (NAT, FORWARD)
 *   - PID file
 *   - Daemonization
 *   - Network information
 *
 * NOTE: This header only works on Linux.
 *       It is included by platform.c and by main.c.
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/platform/platform.h"

#if WINGO_PLATFORM_LINUX

#include <net/if.h>
#include <linux/if_tun.h>

/* ============================================================================
 * TUN INTERFACE STRUCTURE
 * ============================================================================ */

/*
 * TUN interface (Linux-specific).
 *
 * This is the concrete layout of the opaque wingo_tun_t.
 *
 * It is defined here (instead of in platform.c) so that main.c
 * and other files can access the fields directly.
 */
struct wingo_tun {
    int         fd;                     /* /dev/net/tun fd */
    char        name[IFNAMSIZ];         /* Interface name */
    int         mtu;                    /* MTU */
    bool        configured;             /* IP configured? */
    bool        persistent;             /* Persistent TUN? */
};

/* ============================================================================
 * TUN FLAGS
 * ============================================================================ */

/*
 * TUN flags (from <linux/if_tun.h>).
 */
#define WINGO_TUN_FLAG_TUN      IFF_TUN
#define WINGO_TUN_FLAG_TAP      IFF_TAP
#define WINGO_TUN_FLAG_NO_PI    IFF_NO_PI
#define WINGO_TUN_FLAG_MULTI    IFF_MULTI_QUEUE

/* ============================================================================
 * LOW-LEVEL TUN API
 * ============================================================================ */

/*
 * Open a TUN interface directly with custom flags.
 *
 * This is the low-level function. Use wingo_tun_open() from platform.h
 * for the cross-platform API.
 *
 * @param name      Interface name (in/out, must be at least IFNAMSIZ bytes)
 * @param flags     TUN flags
 * @return          TUN handle, or NULL on error
 */
wingo_tun_t *wingo_linux_tun_open(const char *name, int flags);

/*
 * Set TUN interface as persistent.
 *
 * A persistent TUN interface remains after the process exits.
 *
 * @param tun       TUN handle
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_tun_set_persistent(wingo_tun_t *tun);

/* ============================================================================
 * INTERFACE CONFIGURATION
 * ============================================================================ */

/*
 * Set interface IP address.
 *
 * @param ifname    Interface name
 * @param addr      IP address (e.g., "10.0.0.2")
 * @param netmask   Netmask (e.g., "255.255.255.0")
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_set_ipv4(const char *ifname,
                                      const char *addr,
                                      const char *netmask);

/*
 * Set interface MTU.
 *
 * @param ifname    Interface name
 * @param mtu       MTU value
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_set_mtu(const char *ifname, int mtu);

/*
 * Bring interface up.
 *
 * @param ifname    Interface name
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_up(const char *ifname);

/*
 * Bring interface down.
 *
 * @param ifname    Interface name
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_down(const char *ifname);

/* ============================================================================
 * INTERFACE QUERY
 * ============================================================================ */

/*
 * Get interface IP address.
 *
 * @param ifname    Interface name
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_get_ipv4(const char *ifname,
                                      char *buf, wingo_size size);

/*
 * Get interface MAC address.
 *
 * @param ifname    Interface name
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_if_get_mac(const char *ifname,
                                     char *buf, wingo_size size);

/*
 * Check if interface exists.
 *
 * @param ifname    Interface name
 * @return          true if exists, false otherwise
 */
bool wingo_linux_if_exists(const char *ifname);

/*
 * Check if interface is up.
 *
 * @param ifname    Interface name
 * @return          true if up, false otherwise
 */
bool wingo_linux_if_is_up(const char *ifname);

/* ============================================================================
 * IP FORWARDING
 * ============================================================================ */

/*
 * Enable IPv4 forwarding.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_enable_ip_forward(void);

/*
 * Disable IPv4 forwarding.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_disable_ip_forward(void);

/*
 * Check if IPv4 forwarding is enabled.
 *
 * @return          true if enabled, false otherwise
 */
bool wingo_linux_is_ip_forward_enabled(void);

/* ============================================================================
 * IPTABLES (NAT)
 * ============================================================================ */

/*
 * Add MASQUERADE rule.
 *
 * @param out_if    Output interface
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_add_masquerade(const char *out_if);

/*
 * Remove MASQUERADE rule.
 *
 * @param out_if    Output interface
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_remove_masquerade(const char *out_if);

/*
 * Add FORWARD rules.
 *
 * @param in_if     Input interface
 * @param out_if    Output interface
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_add_forward_rules(const char *in_if,
                                            const char *out_if);

/*
 * Remove FORWARD rules.
 *
 * @param in_if     Input interface
 * @param out_if    Output interface
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_remove_forward_rules(const char *in_if,
                                               const char *out_if);

/* ============================================================================
 * PID FILE
 * ============================================================================ */

/*
 * Write PID file.
 *
 * @param path      Path to PID file
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_write_pidfile(const char *path);

/*
 * Remove PID file.
 *
 * @param path      Path to PID file
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_remove_pidfile(const char *path);

/*
 * Read PID from file.
 *
 * @param path      Path to PID file
 * @return          PID, or -1 on error
 */
wingo_i64 wingo_linux_read_pidfile(const char *path);

/*
 * Check if process is running.
 *
 * @param pid       Process ID
 * @return          true if running, false otherwise
 */
bool wingo_linux_is_process_running(wingo_i64 pid);

/* ============================================================================
 * DAEMONIZATION
 * ============================================================================ */

/*
 * Daemonize the process.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_daemonize(void);

/* ============================================================================
 * NETWORK INFORMATION
 * ============================================================================ */

/*
 * Network interface information.
 */
typedef struct {
    char    name[IFNAMSIZ];     /* Interface name */
    char    ipv4[16];           /* IPv4 address */
    char    ipv6[46];           /* IPv6 address */
    char    mac[18];            /* MAC address */
    int     mtu;                /* MTU */
    int     flags;              /* Interface flags */
    bool    is_up;              /* Is interface up? */
    bool    is_loopback;        /* Is loopback? */
} wingo_linux_if_info_t;

/*
 * List network interfaces.
 *
 * @param out       Output array
 * @param max       Maximum number of interfaces
 * @return          Number of interfaces written, or -1 on error
 */
int wingo_linux_list_interfaces(wingo_linux_if_info_t *out, int max);

/*
 * Get interface information.
 *
 * @param ifname    Interface name
 * @param out       Output information
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_get_interface_info(const char *ifname,
                                             wingo_linux_if_info_t *out);

/*
 * Get hostname.
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_get_hostname(char *buf, wingo_size size);

/*
 * Get system uptime.
 *
 * @return          Uptime in seconds, or -1 on error
 */
wingo_i64 wingo_linux_get_uptime(void);

/*
 * Get system load average.
 *
 * @param load      Output array of 3 doubles
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_get_loadavg(double load[3]);

/* ============================================================================
 * ROUTING
 * ============================================================================ */

/*
 * Add a route.
 *
 * @param ifname    Interface name
 * @param dest      Destination network (e.g., "10.0.0.0")
 * @param netmask   Netmask (e.g., "255.255.255.0")
 * @param gateway   Gateway (NULL for direct route)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_route_add(const char *ifname,
                                    const char *dest,
                                    const char *netmask,
                                    const char *gateway);

/*
 * Remove a route.
 *
 * @param ifname    Interface name
 * @param dest      Destination network
 * @param netmask   Netmask
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_route_remove(const char *ifname,
                                       const char *dest,
                                       const char *netmask);

/*
 * Get default interface.
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_linux_route_get_default_if(char *buf, wingo_size size);

#endif /* WINGO_PLATFORM_LINUX */

#endif /* WINGO_PLATFORM_LINUX_PLATFORM_H */
