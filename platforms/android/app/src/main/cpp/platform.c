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
 * This file provides Android-specific implementations of the
 * platform abstraction API.
 *
 * Key differences from Linux:
 *   - TUN fd comes from VpnService (via JNI), not /dev/net/tun
 *   - No root required
 *   - No iptables
 *   - Interface config via Java
 *   - App sandbox paths
 *
 * NOTE: This file only compiles on Android.
 */

#include "wingo/platform/platform.h"
#include "platforms/android/platform.h"

#if WINGO_PLATFORM_ANDROID

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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <android/log.h>

/* ============================================================================
 * LOGGING
 * ============================================================================ */

#define LOG_TAG "BowiePlatform"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Android TUN interface.
 *
 * On Android, TUN is provided by VpnService. The fd is passed
 * from Java via JNI. We don't create the interface ourselves.
 */
struct wingo_tun {
    int         fd;                     /* fd from VpnService */
    char        name[64];               /* Interface name */
    int         mtu;                    /* MTU */
    bool        configured;             /* Is configured? */
    bool        owns_fd;                /* Should we close fd? */
};

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

/*
 * Global TUN fd from VpnService.
 *
 * Set via wingo_tun_set_android_fd().
 */
static int g_android_tun_fd = -1;

/* ============================================================================
 * PLATFORM INFO (ANDROID)
 * ============================================================================ */

/*
 * Get Android API level.
 *
 * NOTE: This is set from Java via JNI.
 *       For now, we use a simple approach: read from system property.
 */
static int get_android_api_level(void)
{
    char value[PROP_VALUE_MAX];
    int api = 0;

    /*
     * NOTE: __system_property_get() is in <sys/system_properties.h>.
     *       We don't include it here to avoid dependency issues.
     *
     *       Instead, we parse /system/build.prop (if readable).
     */

    FILE *f = fopen("/system/build.prop", "r");
    if (f != NULL) {
        char line[256];

        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "ro.build.version.sdk=", 21) == 0) {
                api = atoi(line + 21);
                break;
            }
        }

        fclose(f);
    }

    WINGO_UNUSED(value);

    return api;
}

/*
 * Get Android device model.
 */
static const char *get_android_model(void)
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

/* ============================================================================
 * TUN LIFECYCLE (ANDROID)
 * ============================================================================ */

/*
 * Open a TUN interface on Android.
 *
 * On Android, we don't create the TUN interface ourselves.
 * Instead, the VpnService (Java) creates it and passes us the fd.
 *
 * We expect wingo_tun_set_android_fd() to have been called first.
 *
 * @param config    Configuration (ignored for name/mtu — set by VpnService)
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

    tun = calloc(1, sizeof(wingo_tun_t));
    if (tun == NULL) {
        LOGE("Failed to allocate TUN handle");
        return NULL;
    }

    tun->fd = g_android_tun_fd;
    tun->mtu = 1400;
    tun->configured = true;
    tun->owns_fd = false;   /* Java owns the fd */

    strncpy(tun->name, "bowie0", sizeof(tun->name) - 1);
    tun->name[sizeof(tun->name) - 1] = '\0';

    LOGI("Android TUN opened (fd=%d)", tun->fd);

    return tun;
}

/*
 * Close a TUN interface.
 *
 * NOTE: We don't close the fd because Java owns it.
 *       Java will close it when VpnService is destroyed.
 */
void wingo_tun_close(wingo_tun_t *tun)
{
    if (tun == NULL) {
        return;
    }

    if (tun->owns_fd && tun->fd >= 0) {
        close(tun->fd);
    }

    LOGI("Android TUN closed");

    free(tun);
}

/* ============================================================================
 * TUN I/O (ANDROID)
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
 * TUN QUERY (ANDROID)
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
 * ANDROID-SPECIFIC API
 * ============================================================================ */

/*
 * Set TUN fd from Android VpnService.
 *
 * Called from JNI (bowie_jni.c) after VpnService.establish().
 *
 * @param fd        File descriptor from VpnService
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

/* ============================================================================
 * PLATFORM UTILITIES (ANDROID)
 * ============================================================================ */

/*
 * Android paths.
 *
 * The app sandbox is:
 *   /data/data/com.bowie/
 *
 * We use:
 *   /data/data/com.bowie/files          — data
 *   /data/data/com.bowie/files/config   — config
 *   /data/data/com.bowie/cache          — runtime
 */

wingo_error_t wingo_platform_getcwd(char *buf, wingo_size size)
{
    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (getcwd(buf, size) == NULL) {
        /* Android may not have a useful cwd */
        if (size >= 25) {
            strcpy(buf, "/data/data/com.bowie");
        } else {
            return WINGO_ERR_OVERFLOW;
        }
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_platform_gethome(char *buf, wingo_size size)
{
    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (strlen("/data/data/com.bowie/files") >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    strcpy(buf, "/data/data/com.bowie/files");

    return WINGO_SUCCESS;
}

/* ============================================================================
 * ANDROID INFO
 * ============================================================================ */

/*
 * Get Android API level.
 */
int wingo_android_api_level(void)
{
    return get_android_api_level();
}

/*
 * Get Android device model.
 */
const char *wingo_android_model(void)
{
    return get_android_model();
}

/*
 * Check if running on Android.
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
 * This is called after JNI_OnLoad.
 *
 * NOTE: The actual TUN fd is set later via VpnService.
 */
wingo_error_t wingo_platform_init(void)
{
    LOGI("Initializing Android platform");
    LOGI("  API level: %d", get_android_api_level());
    LOGI("  Model:     %s", get_android_model());

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
