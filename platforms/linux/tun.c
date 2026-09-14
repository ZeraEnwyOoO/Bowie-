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
 * Linux TUN/TAP Implementation
 *
 * This file provides the low-level TUN/TAP interface for Linux.
 *
 * It uses /dev/net/tun and ioctl() to create and configure
 * virtual network interfaces.
 *
 * The main functions are:
 *   - wingo_linux_tun_open()      — Open TUN with custom flags
 *   - wingo_linux_tun_set_persistent() — Make TUN persistent
 *
 * The cross-platform API (wingo_tun_open, wingo_tun_read, etc.)
 * is implemented in platforms/linux/platform.c.
 */

#include "wingo/platform/platform.h"
#include "platforms/linux/platform.h"

#if WINGO_PLATFORM_LINUX

#include "wingo/error.h"
#include "wingo/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Linux TUN handle (extends the base wingo_tun_t).
 *
 * The base wingo_tun_t is defined in platforms/linux/platform.c.
 * This struct is used for low-level operations.
 */
typedef struct {
    int         fd;
    char        name[IFNAMSIZ];
    int         flags;
    bool        persistent;
} linux_tun_t;

/* ============================================================================
 * LOW-LEVEL TUN OPEN
 * ============================================================================ */

/*
 * Allocate a TUN interface with custom flags.
 *
 * @param name      Interface name (in/out)
 * @param flags     TUN flags (IFF_TUN, IFF_TAP, IFF_NO_PI, etc.)
 * @return          File descriptor, or -1 on error
 */
static int tun_alloc(const char *name, int flags)
{
    struct ifreq ifr;
    int fd;
    int err;

    /* Open /dev/net/tun */
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
        WINGO_LOG_ERROR("TUNSETIFF failed for %s: %s",
                        name ? name : "(auto)", strerror(errno));
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
 * Make TUN persistent.
 *
 * A persistent TUN interface remains after the process exits.
 *
 * @param fd        TUN fd
 * @return          0 on success, -1 on error
 */
static int tun_set_persistent(int fd)
{
    int err;

    err = ioctl(fd, TUNSETPERSIST, 1);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETPERSIST failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Remove persistent flag from TUN.
 *
 * @param fd        TUN fd
 * @return          0 on success, -1 on error
 */
static int tun_unset_persistent(int fd)
{
    int err;

    err = ioctl(fd, TUNSETPERSIST, 0);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETPERSIST (0) failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Set TUN owner.
 *
 * @param fd        TUN fd
 * @param uid       User ID
 * @return          0 on success, -1 on error
 */
static int tun_set_owner(int fd, uid_t uid)
{
    int err;

    err = ioctl(fd, TUNSETOWNER, uid);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETOWNER failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Set TUN group.
 *
 * @param fd        TUN fd
 * @param gid       Group ID
 * @return          0 on success, -1 on error
 */
static int tun_set_group(int fd, gid_t gid)
{
    int err;

    err = ioctl(fd, TUNSETGROUP, gid);
    if (err < 0) {
        WINGO_LOG_ERROR("TUNSETGROUP failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

wingo_tun_t *wingo_linux_tun_open(const char *name, int flags)
{
    char ifname[IFNAMSIZ];
    int fd;
    wingo_tun_t *tun;

    /* Copy name */
    if (name != NULL) {
        strncpy(ifname, name, IFNAMSIZ - 1);
        ifname[IFNAMSIZ - 1] = '\0';
    } else {
        ifname[0] = '\0';
    }

    /* Ensure IFF_TUN or IFF_TAP is set */
    if ((flags & (IFF_TUN | IFF_TAP)) == 0) {
        flags |= IFF_TUN;
    }

    /* Allocate TUN */
    fd = tun_alloc(ifname, flags);
    if (fd < 0) {
        return NULL;
    }

    /* Allocate handle */
    tun = calloc(1, sizeof(wingo_tun_t));
    if (tun == NULL) {
        close(fd);
        return NULL;
    }

    /*
     * The wingo_tun_t struct is opaque (defined in platform.c),
     * but we need to fill it here. We use a cast to the internal
     * structure. This is a bit hacky, but works because both
     * files are in the same platform.
     *
     * A cleaner approach would be to expose a constructor from
     * platform.c, but for simplicity we'll do it here.
     */
    {
        /* Internal layout of wingo_tun_t */
        struct internal_tun {
            int  fd;
            char name[IFNAMSIZ];
            int  mtu;
            bool configured;
        } *internal = (struct internal_tun *)tun;

        internal->fd = fd;
        internal->mtu = 1400;
        internal->configured = false;
        strncpy(internal->name, ifname, IFNAMSIZ - 1);
        internal->name[IFNAMSIZ - 1] = '\0';
    }

    WINGO_LOG_DEBUG("TUN %s opened (fd=%d, flags=0x%x)",
                    ifname, fd, flags);

    return tun;
}

wingo_error_t wingo_linux_tun_set_persistent(wingo_tun_t *tun)
{
    int fd;

    if (tun == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Extract fd from opaque struct */
    {
        struct internal_tun {
            int  fd;
            char name[IFNAMSIZ];
            int  mtu;
            bool configured;
        } *internal = (struct internal_tun *)tun;

        fd = internal->fd;
    }

    if (fd < 0) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (tun_set_persistent(fd) < 0) {
        return WINGO_ERR_PERMISSION;
    }

    WINGO_LOG_INFO("TUN marked as persistent");
    return WINGO_SUCCESS;
}

/* ============================================================================
 * INTERNAL API (used by platform.c)
 * ============================================================================ */

/*
 * Internal: allocate TUN and return fd.
 *
 * This is used by platform.c's wingo_tun_open().
 *
 * @param name      Interface name (in/out)
 * @param flags     TUN flags
 * @return          fd, or -1 on error
 */
int wingo_linux_tun_alloc(const char *name, int flags)
{
    char ifname[IFNAMSIZ];

    if (name != NULL) {
        strncpy(ifname, name, IFNAMSIZ - 1);
        ifname[IFNAMSIZ - 1] = '\0';
    } else {
        ifname[0] = '\0';
    }

    return tun_alloc(ifname, flags);
}

/*
 * Internal: set TUN owner.
 */
int wingo_linux_tun_set_owner(int fd, uid_t uid)
{
    return tun_set_owner(fd, uid);
}

/*
 * Internal: set TUN group.
 */
int wingo_linux_tun_set_group(int fd, gid_t gid)
{
    return tun_set_group(fd, gid);
}

/*
 * Internal: unset persistent flag.
 */
int wingo_linux_tun_unset_persistent(int fd)
{
    return tun_unset_persistent(fd);
}

#endif /* WINGO_PLATFORM_LINUX */
