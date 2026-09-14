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
  #include "wingo/platform/platform.h" 
#include "wingo/util/time.h"

#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#if WINGO_PLATFORM_LINUX
    #include <sys/stat.h>
#endif

/* ============================================================================
 * PLATFORM INFO
 * ============================================================================ */

wingo_platform_t wingo_platform_get(void)
{
#if WINGO_PLATFORM_ANDROID
    return WINGO_PLATFORM_ANDROID;
#elif WINGO_PLATFORM_LINUX
    return WINGO_PLATFORM_LINUX;
#else
    return WINGO_PLATFORM_UNKNOWN;
#endif
}

const char *wingo_platform_name(void)
{
    return WINGO_PLATFORM_NAME;
}

bool wingo_platform_is_linux(void)
{
#if WINGO_PLATFORM_LINUX
    return true;
#else
    return false;
#endif
}

bool wingo_platform_is_android(void)
{
#if WINGO_PLATFORM_ANDROID
    return true;
#else
    return false;
#endif
}

const char *wingo_platform_version(void)
{
#if WINGO_PLATFORM_LINUX
    static char version_buf[128] = {0};
    static bool initialized = false;

    if (!initialized) {
        struct utsname uts;
        if (uname(&uts) == 0) {
            snprintf(version_buf, sizeof(version_buf),
                     "%s %s", uts.sysname, uts.release);
        } else {
            snprintf(version_buf, sizeof(version_buf), "Linux (unknown)");
        }
        initialized = true;
    }

    return version_buf;
#elif WINGO_PLATFORM_ANDROID
    return "Android";
#else
    return "Unknown";
#endif
}

const char *wingo_platform_arch(void)
{
    static char arch_buf[64] = {0};
    static bool initialized = false;

    if (!initialized) {
#if defined(__x86_64__)
        snprintf(arch_buf, sizeof(arch_buf), "x86_64");
#elif defined(__i386__)
        snprintf(arch_buf, sizeof(arch_buf), "i386");
#elif defined(__aarch64__)
        snprintf(arch_buf, sizeof(arch_buf), "aarch64");
#elif defined(__arm__)
        snprintf(arch_buf, sizeof(arch_buf), "arm");
#elif defined(__riscv)
        snprintf(arch_buf, sizeof(arch_buf), "riscv");
#else
        snprintf(arch_buf, sizeof(arch_buf), "unknown");
#endif
        initialized = true;
    }

    return arch_buf;
}

/* ============================================================================
 * TUN CONFIGURATION
 * ============================================================================ */

void wingo_tun_config_default(wingo_tun_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

#if WINGO_PLATFORM_ANDROID
    config->name = "bowie0";
#else
    config->name = "tun0";
#endif

    config->mtu = 1400;
    config->ipv4_addr = "10.0.0.2";
    config->ipv4_netmask = "255.255.255.0";
    config->ipv6_addr = NULL;
    config->enable_ipv6 = false;
    config->set_default_route = false;
}

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

wingo_error_t wingo_platform_init(void)
{
    WINGO_LOG_DEBUG("Initializing platform: %s (%s)",
                    wingo_platform_name(),
                    wingo_platform_arch());

#if WINGO_PLATFORM_ANDROID
    /*
     * On Android, the TUN fd is set from Java via JNI.
     * Nothing to do here.
     */
#endif

    return WINGO_SUCCESS;
}

void wingo_platform_shutdown(void)
{
    WINGO_LOG_DEBUG("Shutting down platform");
}

/* ============================================================================
 * PLATFORM UTILITIES
 * ============================================================================ */

wingo_u64 wingo_platform_getpid(void)
{
    return (wingo_u64)getpid();
}

wingo_error_t wingo_platform_getcwd(char *buf, wingo_size size)
{
    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (getcwd(buf, size) == NULL) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_platform_gethome(char *buf, wingo_size size)
{
    const char *home;

    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Try $HOME first */
    home = getenv("HOME");

#if WINGO_PLATFORM_ANDROID
    /*
     * On Android, $HOME is usually not set.
     * Use /data/data/com.bowie/files as home.
     */
    if (home == NULL) {
        home = "/data/data/com.bowie/files";
    }
#endif

    /* Fall back to getpwuid */
    if (home == NULL) {
        struct passwd *pw = getpwuid(getuid());
        if (pw != NULL) {
            home = pw->pw_dir;
        }
    }

    if (home == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    if (strlen(home) >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    strcpy(buf, home);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_platform_getconfigdir(char *buf, wingo_size size)
{
    char home[WINGO_MAX_PATH];
    wingo_error_t rc;

    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

#if WINGO_PLATFORM_ANDROID
    /* Android: /data/data/com.bowie/files/config */
    snprintf(buf, size, "/data/data/com.bowie/files/config");
    return WINGO_SUCCESS;
#else
    /* Linux: $XDG_CONFIG_HOME/bowie or ~/.config/bowie */

    const char *xdg_config = getenv("XDG_CONFIG_HOME");

    if (xdg_config != NULL && xdg_config[0] != '\0') {
        if (strlen(xdg_config) + strlen("/bowie") >= size) {
            return WINGO_ERR_OVERFLOW;
        }
        snprintf(buf, size, "%s/bowie", xdg_config);
        return WINGO_SUCCESS;
    }

    rc = wingo_platform_gethome(home, sizeof(home));
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    if (strlen(home) + strlen("/.config/bowie") >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    snprintf(buf, size, "%s/.config/bowie", home);

    return WINGO_SUCCESS;
#endif
}

wingo_error_t wingo_platform_getdatadir(char *buf, wingo_size size)
{
    char home[WINGO_MAX_PATH];
    wingo_error_t rc;

    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

#if WINGO_PLATFORM_ANDROID
    /* Android: /data/data/com.bowie/files */
    snprintf(buf, size, "/data/data/com.bowie/files");
    return WINGO_SUCCESS;
#else
    /* Linux: $XDG_DATA_HOME/bowie or ~/.local/share/bowie */

    const char *xdg_data = getenv("XDG_DATA_HOME");

    if (xdg_data != NULL && xdg_data[0] != '\0') {
        if (strlen(xdg_data) + strlen("/bowie") >= size) {
            return WINGO_ERR_OVERFLOW;
        }
        snprintf(buf, size, "%s/bowie", xdg_data);
        return WINGO_SUCCESS;
    }

    rc = wingo_platform_gethome(home, sizeof(home));
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    if (strlen(home) + strlen("/.local/share/bowie") >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    snprintf(buf, size, "%s/.local/share/bowie", home);

    return WINGO_SUCCESS;
#endif
}

wingo_error_t wingo_platform_getruntimedir(char *buf, wingo_size size)
{
    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

#if WINGO_PLATFORM_ANDROID
    /* Android: /data/data/com.bowie/cache */
    snprintf(buf, size, "/data/data/com.bowie/cache");
    return WINGO_SUCCESS;
#else
    /* Linux: $XDG_RUNTIME_DIR/bowie or /tmp/bowie */

    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");

    if (xdg_runtime != NULL && xdg_runtime[0] != '\0') {
        if (strlen(xdg_runtime) + strlen("/bowie") >= size) {
            return WINGO_ERR_OVERFLOW;
        }
        snprintf(buf, size, "%s/bowie", xdg_runtime);
        return WINGO_SUCCESS;
    }

    /* Fall back to /tmp/bowie */
    if (strlen("/tmp/bowie") >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    snprintf(buf, size, "/tmp/bowie");

    return WINGO_SUCCESS;
#endif
}

/* ============================================================================
 * PLATFORM PERMISSIONS
 * ============================================================================ */

bool wingo_platform_is_root(void)
{
#if WINGO_PLATFORM_ANDROID
    /* Android apps are never root */
    return false;
#else
    return geteuid() == 0;
#endif
}

bool wingo_platform_has_tun(void)
{
#if WINGO_PLATFORM_ANDROID
    /*
     * On Android, TUN is provided by VpnService.
     * We can't check from C without JNI.
     * Assume yes.
     */
    return true;
#else
    /* Linux: check /dev/net/tun exists */
    struct stat st;

    if (stat("/dev/net/tun", &st) != 0) {
        return false;
    }

    return S_ISCHR(st.st_mode);
#endif
}

/* ============================================================================
 * PLATFORM INFO PRINT
 * ============================================================================ */

void wingo_platform_print(FILE *f)
{
    char cwd[WINGO_MAX_PATH] = {0};
    char config_dir[WINGO_MAX_PATH] = {0};
    char data_dir[WINGO_MAX_PATH] = {0};
    char runtime_dir[WINGO_MAX_PATH] = {0};

    if (f == NULL) {
        f = stderr;
    }

    wingo_platform_getcwd(cwd, sizeof(cwd));
    wingo_platform_getconfigdir(config_dir, sizeof(config_dir));
    wingo_platform_getdatadir(data_dir, sizeof(data_dir));
    wingo_platform_getruntimedir(runtime_dir, sizeof(runtime_dir));

    fprintf(f, "Platform Information:\n");
    fprintf(f, "  Platform:     %s\n", wingo_platform_name());
    fprintf(f, "  Version:      %s\n", wingo_platform_version());
    fprintf(f, "  Arch:         %s\n", wingo_platform_arch());
    fprintf(f, "  PID:          %llu\n",
            (unsigned long long)wingo_platform_getpid());
    fprintf(f, "  Root:         %s\n",
            wingo_platform_is_root() ? "yes" : "no");
    fprintf(f, "  TUN available:%s\n",
            wingo_platform_has_tun() ? "yes" : "no");
    fprintf(f, "  CWD:          %s\n", cwd);
    fprintf(f, "  Config dir:   %s\n", config_dir);
    fprintf(f, "  Data dir:     %s\n", data_dir);
    fprintf(f, "  Runtime dir:  %s\n", runtime_dir);
}
