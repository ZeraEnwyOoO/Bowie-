
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
 * Android Platform Implementation
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
 *   - Interface configuration (IP, MTU, routes)
 *   - VpnService setup
 *   - Notification
 *   - Permissions
 *
 * So this file is MINIMAL. Most Android logic is in Java.
 *
 * NOTE: This file only compiles on Android.
 */

#include "wingo/platform/platform.h"
#include "platforms/android/platform.h"

#if WINGO_PLATFORM_ANDROID

#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <android/log.h>

/* ============================================================================
 * LOGGING
 * ============================================================================ */

#define LOG_TAG "BowiePlatform"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

/*
 * Global TUN fd from VpnService.
 *
 * Set via wingo_tun_set_android_fd() from JNI.
 */
static int g_android_tun_fd = -1;

/* ============================================================================
 * TUN LIFECYCLE (ANDROID)
 * ============================================================================ */

/*
 * Open a TUN interface on Android.
 *
 * The fd must already be set via wingo_tun_set_android_fd().
 * We don't create the interface — VpnService does.
 *
 * @param config    Configuration (ignored — set by Java)
 * @return          TUN handle, or NULL on error
 */
wingo_tun_t *wingo_tun_open(const wingo_tun_config_t *config)
{
    wingo_tun_t *tun;

    WINGO_UNUSED(config);

    if (g_android_tun_fd < 0) {
        LOGE("No TUN fd set. Call wingo_tun_set_android_fd() first.");
        return NULL;
    }

    /* Allocate handle (struct is defined in platform.h) */
    tun = calloc(1, sizeof(wingo_tun_t));
    if (tun == NULL) {
        LOGE("Failed to allocate TUN handle");
        return NULL;
    }

    /*
     * Fill in the handle.
     *
     * The struct layout is in platform.h.
     */
    tun->fd = g_android_tun_fd;
    tun->mtu = 1400;
    tun->configured = true;
    tun->owns_fd = false;   /* Java owns the fd */

    strncpy(tun->name, "bowie0", sizeof(tun->name) - 1);
    tun->name[sizeof(tun->name) - 1] = '\0';

    LOGI("TUN opened (fd=%d)", tun->fd);

    return tun;
}

/*
 * Close a TUN interface on Android.
 *
 * NOTE: We do NOT close the fd. Java owns it.
 *       Java will close it when VpnService is destroyed.
 *
 * @param tun       TUN handle (NULL is safe)
 */
void wingo_tun_close(wingo_tun_t *tun)
{
    if (tun == NULL) {
        return;
    }

    if (tun->owns_fd && tun->fd >= 0) {
        close(tun->fd);
    }

    LOGI("TUN closed");

    free(tun);
}

/* ============================================================================
 * TUN I/O (ANDROID)
 * ============================================================================ */

/*
 * Read a packet from the TUN interface.
 *
 * @param tun       TUN handle
 * @param buf       Output buffer
 * @param len       Buffer size
 * @return          Number of bytes read, or -1 on error
 */
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

/*
 * Write a packet to the TUN interface.
 *
 * @param tun       TUN handle
 * @param buf       Packet data
 * @param len       Packet length
 * @return          Number of bytes written, or -1 on error
 */
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
 * TUN QUERY (ANDROID)
 * ============================================================================ */

/*
 * Get TUN file descriptor.
 */
int wingo_tun_get_fd(const wingo_tun_t *tun)
{
    return tun ? tun->fd : -1;
}

/*
 * Get TUN interface name.
 */
const char *wingo_tun_get_name(const wingo_tun_t *tun)
{
    return tun ? tun->name : NULL;
}

/*
 * Get TUN MTU.
 */
int wingo_tun_get_mtu(const wingo_tun_t *tun)
{
    return tun ? tun->mtu : 0;
}

/* ============================================================================
 * ANDROID-SPECIFIC API
 * ============================================================================ */

/*
 * Set TUN fd from VpnService.
 *
 * Called from JNI after VpnService.establish().
 *
 * @param fd        File descriptor
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_tun_set_android_fd(int fd)
{
    if (fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    g_android_tun_fd = fd;

    LOGI("TUN fd set: %d", fd);

    return WINGO_SUCCESS;
}

/*
 * Get Android API level.
 *
 * Reads from /system/build.prop.
 *
 * @return          API level, or 0 on error
 */
int wingo_android_api_level(void)
{
    FILE *f;
    char line[256];
    int api = 0;

    f = fopen("/system/build.prop", "r");
    if (f == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "ro.build.version.sdk=", 21) == 0) {
            api = atoi(line + 21);
            break;
        }
    }

    fclose(f);
    return api;
}

/*
 * Get Android device model.
 *
 * Reads from /system/build.prop.
 *
 * @return          Model string (never NULL)
 */
const char *wingo_android_model(void)
{
    static char model[128] = {0};
    static bool initialized = false;

    if (!initialized) {
        FILE *f = fopen("/system/build.prop", "r");

        if (f != NULL) {
            char line[256];

            while (fgets(line, sizeof(line), f) != NULL) {
                if (strncmp(line, "ro.product.model=", 17) == 0) {
                    strncpy(model, line + 17, sizeof(model) - 1);

                    /* Strip newline */
                    char *nl = strchr(model, '\n');
                    if (nl != NULL) {
                        *nl = '\0';
                    }
                    break;
                }
            }

            fclose(f);
        }

        if (model[0] == '\0') {
            strcpy(model, "Android");
        }

        initialized = true;
    }

    return model;
}

/*
 * Check if running on Android.
 *
 * @return          true (always, on Android)
 */
bool wingo_android_is_android(void)
{
    return true;
}

/* ============================================================================
 * PLATFORM INIT (ANDROID)
 * ============================================================================ */

/*
 * Initialize Android platform.
 *
 * Called from JNI_OnLoad.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_platform_init(void)
{
    LOGI("Initializing Android platform");
    LOGI("  API level: %d", wingo_android_api_level());
    LOGI("  Model:     %s", wingo_android_model());

    return WINGO_SUCCESS;
}

/*
 * Shutdown Android platform.
 */
void wingo_platform_shutdown(void)
{
    LOGI("Shutting down Android platform");

    g_android_tun_fd = -1;
}

#endif /* WINGO_PLATFORM_ANDROID */
