
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
 * This header provides Linux-specific APIs that are not part of the
 * cross-platform abstraction. These functions are used internally by
 * the Linux platform implementation.
 *
 * Features:
 *   - TUN/TAP interface management
 *   - Interface configuration (IP, MTU, MAC)
 *   - Routing table
 *   - iptables/nftables (NAT, FORWARD)
 *   - PID file
 *   - Daemonization
 *   - Network information
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/platform/platform.h"

#if WINGO_PLATFORM_LINUX

/* ============================================================================
 * TUN INTERFACE
 * ============================================================================ */

/*
 * TUN flags.
 */

#define WINGO_TUN_FLAG_TUN      IFF_TUN      /* TUN (layer 3) */
#define WINGO_TUN_FLAG_TAP      IFF_TAP      /* TAP (layer 2) */
#define WINGO_TUN_FLAG_NO_PI    IFF_NO_PI    /* No packet info */
#define WINGO_TUN_FLAG_MULTI   IFF_MULTI_QUEUE /* Multi-queue */

/*
 * Open a TUN interface directly.
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
 * Set interface MAC address.
 *
 * @param ifname    Interface name
 * @param mac       MAC address (e.g., "aa:bb:cc:dd:ee:ff")
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_linux_if_set_mac(const char *ifname, const char *mac);

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
 * Get interface flags.
 *
 * @param ifname    Interface name
 * @return          Flags, or -1 on error
 */

int wingo_linux_if_get_flags(const char *ifname);

/*
 * Get interface MTU.
 *
 * @param ifname    Interface name
 * @return          MTU, or -1 on error
 */

int wingo_linux_if_get_mtu(const char *ifname);

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
 * Set default route.
 *
 * @param ifname    Interface name
 * @param gateway   Gateway address
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_linux_route_set_default(const char *ifname,
                                            const char *gateway);

/*
 * Get default interface.
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_linux_route_get_default_if(char *buf, wingo_size size);

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

/*
 * Clear all iptables rules added by Bowie.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_linux_clear_iptables(void);

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
    char    name[IFNAMSIZ];
    char    ipv4[16];
    char    ipv6[46];
    char    mac[18];
    int     mtu;
    int     flags;
    bool    is_up;
    bool    is_loopback;
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
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_PLATFORM_LINUX */

#endif /* WINGO_PLATFORM_LINUX_PLATFORM_H */
