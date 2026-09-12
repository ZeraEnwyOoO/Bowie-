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

#ifndef WINGO_LOG_H
#define WINGO_LOG_H

#include "wingo/common.h"
#include "wingo/util/buffer.h"

/* ============================================================================
 * LOG LEVELS
 * ============================================================================ */

typedef enum {
    WINGO_LOG_TRACE     = 0,
    WINGO_LOG_DEBUG     = 1,
    WINGO_LOG_INFO      = 2,
    WINGO_LOG_NOTICE    = 3,
    WINGO_LOG_WARN      = 4,
    WINGO_LOG_ERROR     = 5,
    WINGO_LOG_FATAL     = 6,
    WINGO_LOG_NONE      = 7,
} wingo_log_level_t;

/* ============================================================================
 * LOG OUTPUT TARGETS
 * ============================================================================ */

#define WINGO_LOG_TARGET_NONE       0x00
#define WINGO_LOG_TARGET_STDOUT     0x01
#define WINGO_LOG_TARGET_STDERR     0x02
#define WINGO_LOG_TARGET_FILE       0x04
#define WINGO_LOG_TARGET_SYSLOG     0x08
#define WINGO_LOG_TARGET_ALL        0xFF

/* ============================================================================
 * LOG FLAGS
 * ============================================================================ */

#define WINGO_LOG_FLAG_NONE         0x0000
#define WINGO_LOG_FLAG_COLOR        0x0001
#define WINGO_LOG_FLAG_TIMESTAMP    0x0002
#define WINGO_LOG_FLAG_LEVEL        0x0004
#define WINGO_LOG_FLAG_FILE         0x0008
#define WINGO_LOG_FLAG_LINE         0x0010
#define WINGO_LOG_FLAG_FUNC         0x0020
#define WINGO_LOG_FLAG_THREAD       0x0040
#define WINGO_LOG_FLAG_PID          0x0080
#define WINGO_LOG_FLAG_ASYNC        0x0100
#define WINGO_LOG_FLAG_FLUSH        0x0200
#define WINGO_LOG_FLAG_DEFAULT      (WINGO_LOG_FLAG_COLOR | \
                                     WINGO_LOG_FLAG_TIMESTAMP | \
                                     WINGO_LOG_FLAG_LEVEL)

/* ============================================================================
 * LOG CONFIGURATION
 * ============================================================================ */

#define WINGO_LOG_MAX_FILE_SIZE     (10 * 1024 * 1024)
#define WINGO_LOG_MAX_FILES         5

typedef struct {
    wingo_log_level_t   level;
    wingo_u32           targets;
    wingo_u32           flags;
    char                file_path[WINGO_MAX_PATH];
    wingo_size          max_file_size;
    int                 max_files;
    bool                rotate;
    bool                flush;
} wingo_log_config_t;

/* ============================================================================
 * LOG FUNCTIONS
 * ============================================================================ */

wingo_error_t wingo_log_init(const wingo_log_config_t *config);
void wingo_log_shutdown(void);

void wingo_log_set_level(wingo_log_level_t level);
wingo_log_level_t wingo_log_get_level(void);

void wingo_log_set_targets(wingo_u32 targets);
wingo_u32 wingo_log_get_targets(void);

void wingo_log_set_flags(wingo_u32 flags);
wingo_u32 wingo_log_get_flags(void);

wingo_error_t wingo_log_set_file(const char *path);

bool wingo_log_is_enabled(wingo_log_level_t level);

void wingo_log_write(wingo_log_level_t level,
                     const char *file,
                     int line,
                     const char *func,
                     const char *fmt, ...)
    WINGO_ATTR_FORMAT(5, 6);

void wingo_log_vwrite(wingo_log_level_t level,
                      const char *file,
                      int line,
                      const char *func,
                      const char *fmt,
                      va_list args);

void wingo_log_flush(void);
wingo_error_t wingo_log_rotate(void);

/* ============================================================================
 * LOG MACROS
 * ============================================================================ */

#define WINGO_LOG_TRACE(...) \
    wingo_log_write(WINGO_LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_DEBUG(...) \
    wingo_log_write(WINGO_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_INFO(...) \
    wingo_log_write(WINGO_LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_NOTICE(...) \
    wingo_log_write(WINGO_LOG_NOTICE, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_WARN(...) \
    wingo_log_write(WINGO_LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_ERROR(...) \
    wingo_log_write(WINGO_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define WINGO_LOG_FATAL(...) \
    do { \
        wingo_log_write(WINGO_LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__); \
        wingo_log_flush(); \
        abort(); \
    } while (0)

#define WINGO_LOG_TRACE_IF(cond, ...) \
    do { if ((cond)) WINGO_LOG_TRACE(__VA_ARGS__); } while (0)

#define WINGO_LOG_DEBUG_IF(cond, ...) \
    do { if ((cond)) WINGO_LOG_DEBUG(__VA_ARGS__); } while (0)

#define WINGO_LOG_INFO_IF(cond, ...) \
    do { if ((cond)) WINGO_LOG_INFO(__VA_ARGS__); } while (0)

#define WINGO_LOG_WARN_IF(cond, ...) \
    do { if ((cond)) WINGO_LOG_WARN(__VA_ARGS__); } while (0)

#define WINGO_LOG_ERROR_IF(cond, ...) \
    do { if ((cond)) WINGO_LOG_ERROR(__VA_ARGS__); } while (0)

#define WINGO_LOG_ERR_CTX(ctx) \
    do { \
        if ((ctx) != NULL) { \
            WINGO_LOG_ERROR("%s: %s (at %s:%d in %s)", \
                wingo_error_name((ctx)->code), \
                (ctx)->message, \
                (ctx)->file, \
                (ctx)->line, \
                (ctx)->func); \
        } \
    } while (0)

/* ============================================================================
 * LOG HEX DUMP
 * ============================================================================ */

void wingo_log_hex(wingo_log_level_t level,
                   const char *file,
                   int line,
                   const char *func,
                   const void *data,
                   wingo_size len);

#define WINGO_LOG_HEX(level, data, len) \
    wingo_log_hex(level, __FILE__, __LINE__, __func__, data, len)

void wingo_log_buf(wingo_log_level_t level,
                   const char *file,
                   int line,
                   const char *func,
                   const wingo_buf_t *buf);

#define WINGO_LOG_BUF(level, buf) \
    wingo_log_buf(level, __FILE__, __LINE__, __func__, buf)

/* ============================================================================
 * COLOR CODES
 * ============================================================================ */

#define WINGO_COLOR_RESET       "\033[0m"
#define WINGO_COLOR_BLACK       "\033[30m"
#define WINGO_COLOR_RED         "\033[31m"
#define WINGO_COLOR_GREEN       "\033[32m"
#define WINGO_COLOR_YELLOW      "\033[33m"
#define WINGO_COLOR_BLUE        "\033[34m"
#define WINGO_COLOR_MAGENTA     "\033[35m"
#define WINGO_COLOR_CYAN        "\033[36m"
#define WINGO_COLOR_WHITE       "\033[37m"

#define WINGO_COLOR_BOLD        "\033[1m"
#define WINGO_COLOR_DIM         "\033[2m"
#define WINGO_COLOR_UNDERLINE   "\033[4m"
#define WINGO_COLOR_BLINK       "\033[5m"
#define WINGO_COLOR_REVERSE     "\033[7m"

#endif /* WINGO_LOG_H */
