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

#ifndef WINGO_PLATFORM_ANDROID_PLATFORM_H
#define WINGO_PLATFORM_ANDROID_PLATFORM_H

/*
 * ============================================================================
 * ANDROID PLATFORM API
 * ============================================================================
 *
 * This header provides Android-specific APIs.
 *
 * IMPORTANT: Android is different from Linux!
 *
 * What we CAN do in C:
 *   - Read/write TUN fd (provided by VpnService)
 *   - Platform detection
 *   - App sandbox paths
 *   - Read /system/build.prop
 *
 * What we CANNOT do in C (must be done in Java):
 *   - TUN creation (VpnService)
 *   - IP configuration
 *   - MTU configuration
 *   - Routing
 *   - iptables (not available on Android)
 *
 * So this header is MINIMAL compared to Linux.
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/platform/platform.h"

#if WINGO_PLATFORM_ANDROID

/* ============================================================================
 * TUN INTERFACE STRUCTURE
 * ============================================================================ */

/*
 * TUN interface (Android-specific).
 *
 * This is the concrete layout of the opaque wingo_tun_t.
 *
 * On Android, the fd is provided by Java (VpnService).
 * We don't create or configure the interface ourselves.
 */
struct wingo_tun {
    int         fd;                     /* fd from VpnService */
    char        name[64];               /* Interface name */
    int         mtu;                    /* MTU */
    bool        configured;             /* Is configured? */
    bool        owns_fd;                /* Should we close fd? */
};

/* ============================================================================
 * ANDROID-SPECIFIC API
 * ============================================================================ */

/*
 * Set TUN fd from VpnService.
 *
 * Called from JNI (bowie_jni.c) after VpnService.establish().
 *
 * @param fd        File descriptor from VpnService
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_tun_set_android_fd(int fd);

/*
 * Get Android API level.
 *
 * Reads from /system/build.prop.
 *
 * @return          API level, or 0 on error
 */
int wingo_android_api_level(void);

/*
 * Get Android device model.
 *
 * Reads from /system/build.prop.
 *
 * @return          Model string (never NULL)
 */
const char *wingo_android_model(void);

/*
 * Check if running on Android.
 *
 * @return          true (always, on Android)
 */
bool wingo_android_is_android(void);

#endif /* WINGO_PLATFORM_ANDROID */

#endif /* WINGO_PLATFORM_ANDROID_PLATFORM_H */
