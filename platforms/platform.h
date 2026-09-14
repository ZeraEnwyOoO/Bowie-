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

#ifndef WINGO_PLATFORM_PLATFORM_H
#define WINGO_PLATFORM_PLATFORM_H

/*
 * ============================================================================
 * WINGO PLATFORM ABSTRACTION
 * ============================================================================
 *
 * This header provides a cross-platform abstraction layer. It allows
 * the core engine to run on multiple platforms (Linux, Android) without
 * platform-specific code in the core.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    CORE ENGINE                              │
 *   │                                                             │
 *   │   (Platform-independent code)                               │
 *   │                                                             │
 *   └──────────────────────────┬──────────────────────────────────┘
 *                              │
 *                              │ Platform API
 *                              │
 *   ┌──────────────────────────▼──────────────────────────────────┐
 *   │                  PLATFORM LAYER                             │
 *   │                                                             │
 *   │   ┌─────────────┐              ┌─────────────┐             │
 *   │   │   Linux     │              │   Android   │             │
 *   │   │             │              │             │             │
 *   │   │  TUN/TAP    │              │  VpnService │             │
 *   │   │  /dev/net   │              │  JNI Bridge │             │
 *   │   │             │              │             │             │
 *   │   └─────────────┘              └─────────────┘             │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * PLATFORM DETECTION (COMPILE-TIME)
 * ============================================================================ */

/*
 * Platform detection at compile time.
 *
 * These macros are defined based on the compiler's predefined macros.
 */

#if defined(__ANDROID__)
    #define WINGO_PLATFORM_ANDROID   1
    #define WINGO_PLATFORM_LINUX     0
    #define WINGO_PLATFORM_NAME      "Android"
#elif defined(__linux__)
    #define WINGO_PLATFORM_ANDROID   0
    #define WINGO_PLATFORM_LINUX     1
    #define WINGO_PLATFORM_NAME      "Linux"
#else
    #error "Unsupported platform. Bowie requires Linux or Android."
#endif

/* ============================================================================
 * PLATFORM TYPES
 * ============================================================================ */

/*
 * Platform type.
 */

typedef enum {
    WINGO_PLATFORM_UNKNOWN = 0,
    WINGO_PLATFORM_LINUX   = 1,
    WINGO_PLATFORM_ANDROID = 2,
} wingo_platform_t;

/* ============================================================================
 * PLATFORM INFO
 * ============================================================================ */

/*
 * Get current platform.
 *
 * @return          Platform type
 */

wingo_platform_t wingo_platform_get(void);

/*
 * Get platform name.
 *
 * @return          Static string (never NULL)
 */

const char *wingo_platform_name(void);

/*
 * Check if running on Linux.
 *
 * @return          true if Linux, false otherwise
 */

bool wingo_platform_is_linux(void);

/*
 * Check if running on Android.
 *
 * @return          true if Android, false otherwise
 */

bool wingo_platform_is_android(void);

/*
 * Get platform version string.
 *
 * @return          Static string (never NULL)
 */

const char *wingo_platform_version(void);

/*
 * Get platform architecture.
 *
 * @return          Static string (never NULL)
 */

const char *wingo_platform_arch(void);

/* ============================================================================
 * TUN INTERFACE (OPAQUE)
 * ============================================================================ */

/*
 * TUN interface handle.
 *
 * This is an opaque type. The actual implementation is platform-specific:
 *   - Linux: /dev/net/tun
 *   - Android: VpnService
 */

typedef struct wingo_tun wingo_tun_t;

/* ============================================================================
 * TUN CONFIGURATION
 * ============================================================================ */

/*
 * TUN configuration.
 */

typedef struct {
    /*
     * Interface name.
     * NULL = "tun0" (Linux) or "bowie0" (Android)
     */
    const char *name;

    /*
     * MTU (Maximum Transmission Unit).
     * 0 = default (1400)
     */
    int mtu;

    /*
     * IPv4 address (e.g., "10.0.0.2").
     * NULL = "10.0.0.2"
     */
    const char *ipv4_addr;

    /*
     * IPv4 netmask (e.g., "255.255.255.0").
     * NULL = "255.255.255.0"
     */
    const char *ipv4_netmask;

    /*
     * IPv6 address (e.g., "fd00::2").
     * NULL = no IPv6
     */
    const char *ipv6_addr;

    /*
     * Enable IPv6.
     */
    bool enable_ipv6;

    /*
     * Set as default route.
     */
    bool set_default_route;

} wingo_tun_config_t;

/*
 * Get default TUN configuration.
 *
 * @param config    Output configuration
 */

void wingo_tun_config_default(wingo_tun_config_t *config);

/* ============================================================================
 * TUN LIFECYCLE
 * ============================================================================ */

/*
 * Open a TUN interface.
 *
 * On Linux: opens /dev/net/tun and configures the interface.
 * On Android: uses VpnService (must be initialized from Java first).
 *
 * @param config    Configuration (NULL for defaults)
 * @return          TUN handle, or NULL on error
 */

wingo_tun_t *wingo_tun_open(const wingo_tun_config_t *config);

/*
 * Close a TUN interface.
 *
 * @param tun       TUN handle (NULL is safe)
 */

void wingo_tun_close(wingo_tun_t *tun);

/* ============================================================================
 * TUN I/O
 * ============================================================================ */

/*
 * Read a packet from the TUN interface.
 *
 * On Linux: reads from the TUN fd.
 * On Android: reads from the VpnService fd.
 *
 * @param tun       TUN handle
 * @param buf       Output buffer
 * @param len       Buffer size
 * @return          Number of bytes read, or -1 on error
 */

int wingo_tun_read(wingo_tun_t *tun, void *buf, wingo_size len);

/*
 * Write a packet to the TUN interface.
 *
 * @param tun       TUN handle
 * @param buf       Packet data
 * @param len       Packet length
 * @return          Number of bytes written, or -1 on error
 */

int wingo_tun_write(wingo_tun_t *tun, const void *buf, wingo_size len);

/* ============================================================================
 * TUN QUERY
 * ============================================================================ */

/*
 * Get TUN file descriptor.
 *
 * @param tun       TUN handle
 * @return          File descriptor, or -1 on error
 */

int wingo_tun_get_fd(const wingo_tun_t *tun);

/*
 * Get TUN interface name.
 *
 * @param tun       TUN handle
 * @return          Interface name, or NULL on error
 */

const char *wingo_tun_get_name(const wingo_tun_t *tun);

/*
 * Get TUN MTU.
 *
 * @param tun       TUN handle
 * @return          MTU, or 0 on error
 */

int wingo_tun_get_mtu(const wingo_tun_t *tun);

/* ============================================================================
 * TUN ANDROID-SPECIFIC
 * ============================================================================ */

#if WINGO_PLATFORM_ANDROID

/*
 * Set the TUN file descriptor from Android VpnService.
 *
 * This must be called from JNI after VpnService.establish().
 *
 * @param fd        File descriptor from VpnService
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_tun_set_android_fd(int fd);

#endif /* WINGO_PLATFORM_ANDROID */

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

/*
 * Initialize platform subsystem.
 *
 * On Linux: no-op (TUN is available).
 * On Android: initializes JNI references.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_init(void);

/*
 * Shutdown platform subsystem.
 */

void wingo_platform_shutdown(void);

/* ============================================================================
 * PLATFORM UTILITIES
 * ============================================================================ */

/*
 * Get process ID.
 *
 * @return          Process ID
 */

wingo_u64 wingo_platform_getpid(void);

/*
 * Get current working directory.
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_getcwd(char *buf, wingo_size size);

/*
 * Get home directory.
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_gethome(char *buf, wingo_size size);

/*
 * Get config directory.
 *
 * Linux:   ~/.config/bowie
 * Android: /data/data/com.bowie/files/config
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_getconfigdir(char *buf, wingo_size size);

/*
 * Get data directory.
 *
 * Linux:   ~/.local/share/bowie
 * Android: /data/data/com.bowie/files
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_getdatadir(char *buf, wingo_size size);

/*
 * Get runtime directory.
 *
 * Linux:   /run/bowie
 * Android: /data/data/com.bowie/cache
 *
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_platform_getruntimedir(char *buf, wingo_size size);

/* ============================================================================
 * PLATFORM PERMISSIONS
 * ============================================================================ */

/*
 * Check if running as root.
 *
 * Linux:   geteuid() == 0
 * Android: always false (app is never root)
 *
 * @return          true if root, false otherwise
 */

bool wingo_platform_is_root(void);

/*
 * Check if TUN is available.
 *
 * Linux:   /dev/net/tun exists
 * Android: VpnService is available
 *
 * @return          true if TUN available, false otherwise
 */

bool wingo_platform_has_tun(void);

/* ============================================================================
 * PLATFORM INFO PRINT
 * ============================================================================ */

/*
 * Print platform information.
 *
 * @param f         Output file (NULL = stderr)
 */

void wingo_platform_print(FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_PLATFORM_PLATFORM_H */
