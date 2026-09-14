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
 * This header provides a cross-platform abstraction layer.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    CORE ENGINE                              │
 *   │                                                             │
 *   │   #include "wingo/platform/platform.h"                      │
 *   │                                                             │
 *   └──────────────────────────┬──────────────────────────────────┘
 *                              │
 *                              │ Interface (this header)
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
//why diagram? bcus i fix that shit a lot of hour
#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * PLATFORM DETECTION (COMPILE-TIME)
 * ============================================================================ */

/*
 * Platform detection at compile time.
 *
 * NOTE: We use WINGO_IS_LINUX / WINGO_IS_ANDROID as macros
 *       to avoid conflict with the enum values below.
 */

#if defined(__ANDROID__)
    #define WINGO_IS_ANDROID    1
    #define WINGO_IS_LINUX      0
    #define WINGO_PLATFORM_NAME "Android"
#elif defined(__linux__)
    #define WINGO_IS_ANDROID    0
    #define WINGO_IS_LINUX      1
    #define WINGO_PLATFORM_NAME "Linux"
#else
    #error "Unsupported platform. Bowie requires Linux or Android."
#endif

/* ============================================================================
 * PLATFORM TYPES
 * ============================================================================ */

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
 */
wingo_platform_t wingo_platform_get(void);

/*
 * Get platform name.
 */
const char *wingo_platform_name(void);

/*
 * Check if running on Linux.
 */
bool wingo_platform_is_linux(void);

/*
 * Check if running on Android.
 */
bool wingo_platform_is_android(void);

/*
 * Get platform version string.
 */
const char *wingo_platform_version(void);

/*
 * Get platform architecture.
 */
const char *wingo_platform_arch(void);

/* ============================================================================
 * TUN INTERFACE (OPAQUE)
 * ============================================================================ */

/*
 * TUN interface handle.
 *
 * This is an opaque type. The actual implementation is platform-specific.
 */
typedef struct wingo_tun wingo_tun_t;

/* ============================================================================
 * TUN CONFIGURATION
 * ============================================================================ */

typedef struct {
    const char *name;           /* Interface name (NULL = default) */
    int         mtu;            /* MTU (0 = default) */
    const char *ipv4_addr;      /* IPv4 address (NULL = default) */
    const char *ipv4_netmask;   /* IPv4 netmask (NULL = default) */
    const char *ipv6_addr;      /* IPv6 address (NULL = none) */
    bool        enable_ipv6;    /* Enable IPv6 */
    bool        set_default_route; /* Set as default route */
} wingo_tun_config_t;

/*
 * Get default TUN configuration.
 */
void wingo_tun_config_default(wingo_tun_config_t *config);

/* ============================================================================
 * TUN LIFECYCLE
 * ============================================================================ */

/*
 * Open a TUN interface.
 */
wingo_tun_t *wingo_tun_open(const wingo_tun_config_t *config);

/*
 * Close a TUN interface.
 */
void wingo_tun_close(wingo_tun_t *tun);

/* ============================================================================
 * TUN I/O
 * ============================================================================ */

/*
 * Read a packet from the TUN interface.
 */
int wingo_tun_read(wingo_tun_t *tun, void *buf, wingo_size len);

/*
 * Write a packet to the TUN interface.
 */
int wingo_tun_write(wingo_tun_t *tun, const void *buf, wingo_size len);

/* ============================================================================
 * TUN QUERY
 * ============================================================================ */

/*
 * Get TUN file descriptor.
 */
int wingo_tun_get_fd(const wingo_tun_t *tun);

/*
 * Get TUN interface name.
 */
const char *wingo_tun_get_name(const wingo_tun_t *tun);

/*
 * Get TUN MTU.
 */
int wingo_tun_get_mtu(const wingo_tun_t *tun);

/* ============================================================================
 * ANDROID-SPECIFIC API
 * ============================================================================ */

#if WINGO_IS_ANDROID

/*
 * Set the TUN file descriptor from Android VpnService.
 *
 * Called from JNI after VpnService.establish().
 */
wingo_error_t wingo_tun_set_android_fd(int fd);

#endif /* WINGO_IS_ANDROID */

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

/*
 * Initialize platform subsystem.
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
 */
wingo_u64 wingo_platform_getpid(void);

/*
 * Get current working directory.
 */
wingo_error_t wingo_platform_getcwd(char *buf, wingo_size size);

/*
 * Get home directory.
 */
wingo_error_t wingo_platform_gethome(char *buf, wingo_size size);

/*
 * Get config directory.
 */
wingo_error_t wingo_platform_getconfigdir(char *buf, wingo_size size);

/*
 * Get data directory.
 */
wingo_error_t wingo_platform_getdatadir(char *buf, wingo_size size);

/*
 * Get runtime directory.
 */
wingo_error_t wingo_platform_getruntimedir(char *buf, wingo_size size);

/* ============================================================================
 * PLATFORM PERMISSIONS
 * ============================================================================ */

/*
 * Check if running as root.
 */
bool wingo_platform_is_root(void);

/*
 * Check if TUN is available.
 */
bool wingo_platform_has_tun(void);

/* ============================================================================
 * PLATFORM INFO PRINT
 * ============================================================================ */

/*
 * Print platform information.
 */
void wingo_platform_print(FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_PLATFORM_PLATFORM_H */
