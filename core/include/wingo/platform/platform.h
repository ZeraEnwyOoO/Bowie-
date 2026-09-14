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
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * PLATFORM DETECTION (COMPILE-TIME)
 * ============================================================================ */

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

wingo_platform_t wingo_platform_get(void);
const char *wingo_platform_name(void);
bool wingo_platform_is_linux(void);
bool wingo_platform_is_android(void);
const char *wingo_platform_version(void);
const char *wingo_platform_arch(void);

/* ============================================================================
 * TUN INTERFACE (OPAQUE)
 * ============================================================================ */

typedef struct wingo_tun wingo_tun_t;

/* ============================================================================
 * TUN CONFIGURATION
 * ============================================================================ */

typedef struct {
    const char *name;
    int         mtu;
    const char *ipv4_addr;
    const char *ipv4_netmask;
    const char *ipv6_addr;
    bool        enable_ipv6;
    bool        set_default_route;
} wingo_tun_config_t;

void wingo_tun_config_default(wingo_tun_config_t *config);

/* ============================================================================
 * TUN LIFECYCLE
 * ============================================================================ */

wingo_tun_t *wingo_tun_open(const wingo_tun_config_t *config);
void wingo_tun_close(wingo_tun_t *tun);

/* ============================================================================
 * TUN I/O
 * ============================================================================ */

int wingo_tun_read(wingo_tun_t *tun, void *buf, wingo_size len);
int wingo_tun_write(wingo_tun_t *tun, const void *buf, wingo_size len);

/* ============================================================================
 * TUN QUERY
 * ============================================================================ */

int wingo_tun_get_fd(const wingo_tun_t *tun);
const char *wingo_tun_get_name(const wingo_tun_t *tun);
int wingo_tun_get_mtu(const wingo_tun_t *tun);

/* ============================================================================
 * ANDROID-SPECIFIC API
 * ============================================================================ */

#if WINGO_IS_ANDROID

wingo_error_t wingo_tun_set_android_fd(int fd);

#endif /* WINGO_IS_ANDROID */

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

wingo_error_t wingo_platform_init(void);
void wingo_platform_shutdown(void);

/* ============================================================================
 * PLATFORM UTILITIES
 * ============================================================================ */

wingo_u64 wingo_platform_getpid(void);
wingo_error_t wingo_platform_getcwd(char *buf, wingo_size size);
wingo_error_t wingo_platform_gethome(char *buf, wingo_size size);
wingo_error_t wingo_platform_getconfigdir(char *buf, wingo_size size);
wingo_error_t wingo_platform_getdatadir(char *buf, wingo_size size);
wingo_error_t wingo_platform_getruntimedir(char *buf, wingo_size size);

/* ============================================================================
 * PLATFORM PERMISSIONS
 * ============================================================================ */

bool wingo_platform_is_root(void);
bool wingo_platform_has_tun(void);

/* ============================================================================
 * PLATFORM INFO PRINT
 * ============================================================================ */

void wingo_platform_print(FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_PLATFORM_PLATFORM_H */
