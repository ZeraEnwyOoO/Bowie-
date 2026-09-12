 
 
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

#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

static const char *const log_level_names[] = {
    "TRACE", "DEBUG", "INFO", "NOTICE",
    "WARN", "ERROR", "FATAL", "NONE"
};

static const char *const log_level_colors[] = {
    WINGO_COLOR_DIM, WINGO_COLOR_CYAN, WINGO_COLOR_GREEN,
    WINGO_COLOR_BLUE, WINGO_COLOR_YELLOW, WINGO_COLOR_RED,
    WINGO_COLOR_RED WINGO_COLOR_BOLD, WINGO_COLOR_RESET
};

static const int log_level_syslog[] = {
    LOG_DEBUG, LOG_DEBUG, LOG_INFO, LOG_NOTICE,
    LOG_WARNING, LOG_ERR, LOG_CRIT, LOG_EMERG
};

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
    bool                initialized;
    wingo_log_level_t   level;
    wingo_u32           targets;
    wingo_u32           flags;
    FILE               *file;
    char                file_path[WINGO_MAX_PATH];
    wingo_size          max_file_size;
    int                 max_files;
    bool                rotate;
    bool                flush;
    bool                use_syslog;
} log_state = {
    .initialized   = false,
    .level         = WINGO_LOG_INFO,
    .targets       = WINGO_LOG_TARGET_STDERR,
    .flags         = WINGO_LOG_FLAG_DEFAULT,
    .file          = NULL,
    .file_path     = {0},
    .max_file_size = WINGO_LOG_MAX_FILE_SIZE,
    .max_files     = WINGO_LOG_MAX_FILES,
    .rotate        = true,
    .flush         = true,
    .use_syslog    = false,
};

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

static void log_timestamp(char *buf, wingo_size size)
{
    wingo_timespec_t ts;
    struct tm tm;
    time_t sec;

    if (buf == NULL || size == 0) return;

    if (wingo_time_realtime(&ts) != WINGO_SUCCESS) {
        buf[0] = '\0';
        return;
    }

    sec = (time_t)ts.sec;
    if (localtime_r(&sec, &tm) == NULL) {
        buf[0] = '\0';
        return;
    }

    snprintf(buf, size,
             "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             (int)(ts.nsec / 1000000));
}

static void log_thread_id(char *buf, wingo_size size)
{
    unsigned long tid;
    if (buf == NULL || size == 0) return;
    tid = (unsigned long)pthread_self();
    snprintf(buf, size, "%lu", tid);
}

static bool log_needs_rotation(void)
{
    struct stat st;
    if (log_state.file == NULL || log_state.file_path[0] == '\0') return false;
    if (stat(log_state.file_path, &st) != 0) return false;
    return (wingo_size)st.st_size >= log_state.max_file_size;
}

/*
 * Safe snprintf wrapper.
 *
 * Returns WINGO_SUCCESS if the string fits, WINGO_ERR_OVERFLOW otherwise.
 *
 * This eliminates -Wformat-truncation warnings by explicitly checking
 * the return value of snprintf.
 */
static wingo_error_t safe_snprintf(char *buf, wingo_size size,
                                   const char *fmt, ...)
{
    va_list args;
    int written;

    if (buf == NULL || size == 0 || fmt == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    va_start(args, fmt);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);

    if (written < 0) {
        return WINGO_ERR_GENERIC;
    }

    if ((wingo_size)written >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    return WINGO_SUCCESS;
}

/*
 * Safe string copy with guaranteed null-termination.
 *
 * This eliminates -Wstringop-truncation warnings by:
 *   1. Computing the actual length with strlen()
 *   2. Using memcpy() instead of strncpy()
 *   3. Explicitly null-terminating the destination
 *
 * GCC's -Wstringop-truncation warning is triggered specifically by
 * strncpy() when it cannot prove the source is shorter than the
 * destination. Using memcpy() with an explicit length avoids this
 * entirely while being equally safe.
 */
static void safe_strcpy(char *dst, wingo_size dst_size, const char *src)
{
    wingo_size len;

    if (dst == NULL || dst_size == 0) return;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);

    /* Clamp to destination size - 1 (leave room for null terminator) */
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    /* Copy the bytes */
    if (len > 0) {
        memcpy(dst, src, len);
    }

    /* Explicit null terminator */
    dst[len] = '\0';
}

static wingo_error_t log_rotate_files(void)
{
    char old_path[WINGO_MAX_PATH];
    char new_path[WINGO_MAX_PATH];
    wingo_error_t rc;
    int i;

    if (!log_state.rotate || log_state.file_path[0] == '\0') {
        return WINGO_SUCCESS;
    }

    if (log_state.file != NULL) {
        fclose(log_state.file);
        log_state.file = NULL;
    }

    /* Delete oldest file */
    rc = safe_snprintf(old_path, sizeof(old_path), "%s.%d",
                       log_state.file_path, log_state.max_files);
    if (rc == WINGO_SUCCESS) {
        unlink(old_path);
    }

    /* Shift files: N-1 -> N, N-2 -> N-1, ..., 1 -> 2 */
    for (i = log_state.max_files - 1; i >= 1; i--) {
        rc = safe_snprintf(old_path, sizeof(old_path), "%s.%d",
                           log_state.file_path, i);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }

        rc = safe_snprintf(new_path, sizeof(new_path), "%s.%d",
                           log_state.file_path, i + 1);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }

        if (rename(old_path, new_path) != 0 && errno != ENOENT) {
            fprintf(stderr, "Warning: failed to rotate %s -> %s: %s\n",
                    old_path, new_path, strerror(errno));
        }
    }

    /* Rename current log to .1 */
    rc = safe_snprintf(new_path, sizeof(new_path), "%s.1",
                       log_state.file_path);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    if (rename(log_state.file_path, new_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "Warning: failed to rename %s -> %s: %s\n",
                log_state.file_path, new_path, strerror(errno));
    }

    /* Reopen log file */
    log_state.file = fopen(log_state.file_path, "a");
    if (log_state.file == NULL) {
        return WINGO_ERR_FILE_OPEN;
    }

    return WINGO_SUCCESS;
}

static void log_write_to_file(FILE *f, const char *line)
{
    if (f == NULL || line == NULL) return;
    fputs(line, f);
    if (log_state.flush) fflush(f);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

wingo_error_t wingo_log_init(const wingo_log_config_t *config)
{
    pthread_mutex_lock(&log_mutex);

    if (log_state.initialized) {
        pthread_mutex_unlock(&log_mutex);
        return WINGO_SUCCESS;
    }

    if (config != NULL) {
        log_state.level         = config->level;
        log_state.targets       = config->targets;
        log_state.flags         = config->flags;
        log_state.max_file_size = config->max_file_size > 0
                                ? config->max_file_size
                                : WINGO_LOG_MAX_FILE_SIZE;
        log_state.max_files     = config->max_files > 0
                                ? config->max_files
                                : WINGO_LOG_MAX_FILES;
        log_state.rotate        = config->rotate;
        log_state.flush         = config->flush;

        if (config->file_path[0] != '\0') {
            safe_strcpy(log_state.file_path,
                        sizeof(log_state.file_path),
                        config->file_path);
        }
    }

    if ((log_state.targets & WINGO_LOG_TARGET_FILE) &&
        log_state.file_path[0] != '\0') {
        log_state.file = fopen(log_state.file_path, "a");
        if (log_state.file == NULL) {
            pthread_mutex_unlock(&log_mutex);
            return WINGO_ERR_FILE_OPEN;
        }
    }

    if (log_state.targets & WINGO_LOG_TARGET_SYSLOG) {
        openlog("bowie", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        log_state.use_syslog = true;
    }

    log_state.initialized = true;
    pthread_mutex_unlock(&log_mutex);
    return WINGO_SUCCESS;
}

void wingo_log_shutdown(void)
{
    pthread_mutex_lock(&log_mutex);

    if (!log_state.initialized) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    if (log_state.file != NULL) {
        fflush(log_state.file);
        fclose(log_state.file);
        log_state.file = NULL;
    }

    if (log_state.use_syslog) {
        closelog();
        log_state.use_syslog = false;
    }

    log_state.initialized = false;
    pthread_mutex_unlock(&log_mutex);
}

void wingo_log_set_level(wingo_log_level_t level)
{
    pthread_mutex_lock(&log_mutex);
    log_state.level = level;
    pthread_mutex_unlock(&log_mutex);
}

wingo_log_level_t wingo_log_get_level(void)
{
    wingo_log_level_t level;
    pthread_mutex_lock(&log_mutex);
    level = log_state.level;
    pthread_mutex_unlock(&log_mutex);
    return level;
}

void wingo_log_set_targets(wingo_u32 targets)
{
    pthread_mutex_lock(&log_mutex);
    log_state.targets = targets;
    pthread_mutex_unlock(&log_mutex);
}

wingo_u32 wingo_log_get_targets(void)
{
    wingo_u32 targets;
    pthread_mutex_lock(&log_mutex);
    targets = log_state.targets;
    pthread_mutex_unlock(&log_mutex);
    return targets;
}

void wingo_log_set_flags(wingo_u32 flags)
{
    pthread_mutex_lock(&log_mutex);
    log_state.flags = flags;
    pthread_mutex_unlock(&log_mutex);
}

wingo_u32 wingo_log_get_flags(void)
{
    wingo_u32 flags;
    pthread_mutex_lock(&log_mutex);
    flags = log_state.flags;
    pthread_mutex_unlock(&log_mutex);
    return flags;
}

wingo_error_t wingo_log_set_file(const char *path)
{
    FILE *new_file = NULL;

    if (path == NULL) return WINGO_ERR_INVALID_ARG;

    pthread_mutex_lock(&log_mutex);

    new_file = fopen(path, "a");
    if (new_file == NULL) {
        pthread_mutex_unlock(&log_mutex);
        return WINGO_ERR_FILE_OPEN;
    }

    if (log_state.file != NULL) {
        fclose(log_state.file);
    }

    log_state.file = new_file;
    safe_strcpy(log_state.file_path, sizeof(log_state.file_path), path);

    pthread_mutex_unlock(&log_mutex);
    return WINGO_SUCCESS;
}

bool wingo_log_is_enabled(wingo_log_level_t level)
{
    bool enabled;
    pthread_mutex_lock(&log_mutex);
    enabled = (level >= log_state.level) && (log_state.level != WINGO_LOG_NONE);
    pthread_mutex_unlock(&log_mutex);
    return enabled;
}

void wingo_log_vwrite(wingo_log_level_t level,
                      const char *file, int line, const char *func,
                      const char *fmt, va_list args)
{
    char timestamp[64];
    char tid[32];
    char message[4096];
    char line_buf[8192];
    const char *level_name;
    const char *level_color;
    const char *file_name;
    const char *slash;
    int pos;
    int n;

    if (level < log_state.level || log_state.level == WINGO_LOG_NONE) {
        return;
    }

    level_name  = (level < WINGO_ARRAY_SIZE(log_level_names))
                ? log_level_names[level] : "UNKNOWN";
    level_color = (level < WINGO_ARRAY_SIZE(log_level_colors))
                ? log_level_colors[level] : "";

    slash = (file != NULL) ? strrchr(file, '/') : NULL;
    file_name = (slash != NULL) ? slash + 1 : (file != NULL ? file : "?");

    vsnprintf(message, sizeof(message), fmt, args);

    if (log_state.flags & WINGO_LOG_FLAG_TIMESTAMP) {
        log_timestamp(timestamp, sizeof(timestamp));
    } else {
        timestamp[0] = '\0';
    }

    if (log_state.flags & WINGO_LOG_FLAG_THREAD) {
        log_thread_id(tid, sizeof(tid));
    } else {
        tid[0] = '\0';
    }

    pos = 0;

    if (timestamp[0] != '\0') {
        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "[%s] ", timestamp);
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
    }

    if (tid[0] != '\0') {
        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "[%s] ", tid);
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
    }

    if (log_state.flags & WINGO_LOG_FLAG_COLOR) {
        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "%s%-6s%s ",
                     level_color, level_name, WINGO_COLOR_RESET);
    } else {
        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "%-6s ", level_name);
    }
    if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

    if (log_state.flags & WINGO_LOG_FLAG_FILE) {
        if (log_state.flags & WINGO_LOG_FLAG_LINE) {
            n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                         "[%s:%d] ", file_name, line);
        } else {
            n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                         "[%s] ", file_name);
        }
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
    }

    if (log_state.flags & WINGO_LOG_FLAG_FUNC) {
        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "%s(): ", func != NULL ? func : "?");
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
    }

    n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                 "%s\n", message);
    if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

    pthread_mutex_lock(&log_mutex);

    if (log_state.targets & WINGO_LOG_TARGET_STDOUT) {
        log_write_to_file(stdout, line_buf);
    }

    if (log_state.targets & WINGO_LOG_TARGET_STDERR) {
        log_write_to_file(stderr, line_buf);
    }

    if ((log_state.targets & WINGO_LOG_TARGET_FILE) && log_state.file != NULL) {
        if (log_state.rotate && log_needs_rotation()) {
            log_rotate_files();
        }
        log_write_to_file(log_state.file, line_buf);
    }

    if (log_state.use_syslog && (log_state.targets & WINGO_LOG_TARGET_SYSLOG)) {
        int priority = (level < WINGO_ARRAY_SIZE(log_level_syslog))
                     ? log_level_syslog[level] : LOG_INFO;
        syslog(priority, "%s", message);
    }

    pthread_mutex_unlock(&log_mutex);
}

void wingo_log_write(wingo_log_level_t level,
                     const char *file, int line, const char *func,
                     const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    wingo_log_vwrite(level, file, line, func, fmt, args);
    va_end(args);
}

void wingo_log_flush(void)
{
    pthread_mutex_lock(&log_mutex);
    if (log_state.file != NULL) fflush(log_state.file);
    fflush(stdout);
    fflush(stderr);
    pthread_mutex_unlock(&log_mutex);
}

wingo_error_t wingo_log_rotate(void)
{
    wingo_error_t rc;
    pthread_mutex_lock(&log_mutex);
    rc = log_rotate_files();
    pthread_mutex_unlock(&log_mutex);
    return rc;
}

void wingo_log_hex(wingo_log_level_t level,
                   const char *file, int line, const char *func,
                   const void *data, wingo_size len)
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    char line_buf[128];
    wingo_size i;
    wingo_size offset;

    if (data == NULL || len == 0) return;
    if (level < log_state.level || log_state.level == WINGO_LOG_NONE) return;

    for (offset = 0; offset < len; offset += 16) {
        int pos = 0;
        int n;

        n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                     "%04zx: ", (size_t)offset);
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

        for (i = 0; i < 16; i++) {
            if (offset + i < len) {
                n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                             "%02x ", bytes[offset + i]);
            } else {
                n = snprintf(line_buf + pos, sizeof(line_buf) - pos, "   ");
            }
            if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

            if (i == 7) {
                n = snprintf(line_buf + pos, sizeof(line_buf) - pos, " ");
                if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
            }
        }

        n = snprintf(line_buf + pos, sizeof(line_buf) - pos, " |");
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

        for (i = 0; i < 16 && offset + i < len; i++) {
            wingo_u8 c = bytes[offset + i];
            n = snprintf(line_buf + pos, sizeof(line_buf) - pos,
                         "%c", (c >= 32 && c <= 126) ? c : '.');
            if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;
        }

        n = snprintf(line_buf + pos, sizeof(line_buf) - pos, "|");
        if (n > 0 && (wingo_size)n < sizeof(line_buf) - pos) pos += n;

        wingo_log_write(level, file, line, func, "%s", line_buf);
    }
}

void wingo_log_buf(wingo_log_level_t level,
                   const char *file, int line, const char *func,
                   const wingo_buf_t *buf)
{
    if (buf == NULL) return;
    wingo_log_write(level, file, line, func,
                    "Buffer[cap=%zu, len=%zu, read=%zu]",
                    buf->cap, buf->len, buf->read);
    wingo_log_hex(level, file, line, func, buf->data, buf->len);
}
