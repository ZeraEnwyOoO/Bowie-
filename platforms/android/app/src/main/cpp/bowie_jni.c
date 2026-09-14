
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
 * Bowie JNI Bridge
 *
 * This file provides the bridge between the Java/Kotlin side of the
 * Android app and the native C engine.
 *
 * It handles:
 *   - Engine lifecycle (init, start, stop)
 *   - TUN fd from VpnService
 *   - Status queries
 *   - Log forwarding to Android logcat
 *
 * The Java side calls these functions via JNI.
 */

#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"
#include "wingo/core/engine.h"
#include "wingo/platform/platform.h"
#include "wingo/util/time.h"

/* ============================================================================
 * LOGGING
 * ============================================================================ */

#define LOG_TAG "BowieJNI"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

/*
 * Global engine handle.
 *
 * Only one engine per process (Android app).
 */
static wingo_engine_t *g_engine = NULL;

/*
 * TUN file descriptor from VpnService.
 *
 * Set via BowieNative.startWithFd().
 * Used by the engine for tunnel I/O.
 */
static int g_tun_fd = -1;

/*
 * Mutex to protect global state.
 */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * JavaVM reference.
 *
 * Stored on JNI_OnLoad so we can attach threads later.
 */
static JavaVM *g_jvm = NULL;

/*
 * Flag: is the native library initialized?
 */
static bool g_initialized = false;

/* ============================================================================
 * JNI LIFECYCLE
 * ============================================================================ */

/*
 * Called when the native library is loaded.
 *
 * This is the first function called by the JVM.
 * We use it to store the JavaVM reference.
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    jint rc;

    WINGO_UNUSED(reserved);

    LOGI("JNI_OnLoad called");

    /* Store JavaVM */
    g_jvm = vm;

    /* Get JNIEnv */
    rc = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    if (rc != JNI_OK) {
        LOGE("Failed to get JNIEnv");
        return JNI_ERR;
    }

    LOGI("Native library loaded");

    return JNI_VERSION_1_6;
}

/*
 * Called when the native library is unloaded.
 */
JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM *vm, void *reserved)
{
    WINGO_UNUSED(vm);
    WINGO_UNUSED(reserved);

    LOGI("JNI_OnUnload called");

    /* Stop engine if running */
    if (g_engine != NULL) {
        wingo_engine_stop(g_engine);
        wingo_engine_free(g_engine);
        g_engine = NULL;
    }

    /* Shutdown log */
    wingo_log_shutdown();

    g_jvm = NULL;
    g_initialized = false;
}

/* ============================================================================
 * ANDROID LOG CALLBACK
 * ============================================================================ */

/*
 * Custom log callback that forwards to Android logcat.
 *
 * We use this instead of the default file/stdout logging because
 * Android doesn't have stdout/stderr in the usual sense.
 */
static void android_log_callback(wingo_log_level_t level,
                                 const char *message)
{
    switch (level) {
    case WINGO_LOG_TRACE:
    case WINGO_LOG_DEBUG:
        LOGD("%s", message);
        break;
    case WINGO_LOG_INFO:
    case WINGO_LOG_NOTICE:
        LOGI("%s", message);
        break;
    case WINGO_LOG_WARN:
        LOGW("%s", message);
        break;
    case WINGO_LOG_ERROR:
    case WINGO_LOG_FATAL:
        LOGE("%s", message);
        break;
    default:
        LOGI("%s", message);
        break;
    }
}

/* ============================================================================
 * NATIVE METHODS
 * ============================================================================ */

/*
 * Initialize the native library.
 *
 * Called from Java: BowieNative.init()
 *
 * This is a lightweight initialization. The engine is not created yet.
 */
JNIEXPORT void JNICALL
Java_com_bowie_BowieNative_init(JNIEnv *env, jclass clazz)
{
    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    if (g_initialized) {
        LOGW("Already initialized");
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    LOGI("Initializing native library");

    /*
     * Initialize logging.
     *
     * We configure it to forward to Android logcat.
     *
     * NOTE: The log callback API is in Phase 2 (log.h).
     *       We use wingo_log_init() with default config,
     *       and the default output goes to stderr, which
     *       Android redirects to logcat automatically.
     */
    wingo_log_config_t log_config;

    memset(&log_config, 0, sizeof(log_config));
    log_config.level = WINGO_LOG_INFO;
    log_config.targets = WINGO_LOG_TARGET_STDERR;
    log_config.flags = WINGO_LOG_FLAG_TIMESTAMP | WINGO_LOG_FLAG_LEVEL;
    log_config.flush = true;

    wingo_log_init(&log_config);

    /* Initialize platform */
    wingo_platform_init();

    g_initialized = true;

    LOGI("Native library initialized");

    pthread_mutex_unlock(&g_mutex);
}

/*
 * Start the engine.
 *
 * Called from Java: BowieNative.start()
 */
JNIEXPORT jboolean JNICALL
Java_com_bowie_BowieNative_start(JNIEnv *env, jclass clazz)
{
    wingo_engine_config_t config;
    wingo_error_t rc;

    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    if (g_engine != NULL) {
        LOGW("Engine already running");
        pthread_mutex_unlock(&g_mutex);
        return JNI_TRUE;
    }

    LOGI("Creating engine");

    /* Create engine config */
    wingo_engine_config_default(&config);
    config.name = "bowie-android";
    config.log_level = WINGO_LOG_INFO;
    config.log_color = false;   /* No colors on Android */
    config.threads = 2;         /* Fewer threads on mobile */
    config.max_peers = 32;      /* Fewer peers on mobile */
    config.max_connections = 64;
    config.tick_ms = 10;
    config.idle_ms = 1000;

    /* Create engine */
    g_engine = wingo_engine_new(&config);
    if (g_engine == NULL) {
        LOGE("Failed to create engine");
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    /* Initialize engine */
    rc = wingo_engine_init(g_engine);
    if (rc != WINGO_SUCCESS) {
        LOGE("Failed to init engine: %s", wingo_error_str(rc));
        wingo_engine_free(g_engine);
        g_engine = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    /*
     * NOTE: We don't call wingo_engine_run() here because it blocks.
     *       The engine is started via startWithFd() from the VpnService.
     *
     *       In Phase 3, the engine can be initialized but not run
     *       until the TUN fd is available.
     */

    LOGI("Engine initialized");

    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

/*
 * Start the engine with a TUN file descriptor.
 *
 * Called from Java: BowieNative.startWithFd(fd)
 *
 * This is called from BowieVpnService after VpnService.establish()
 * provides the TUN fd.
 */
JNIEXPORT jboolean JNICALL
Java_com_bowie_BowieNative_startWithFd(JNIEnv *env, jclass clazz, jint fd)
{
    wingo_error_t rc;

    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    LOGI("Starting with TUN fd: %d", fd);

    g_tun_fd = fd;

    /*
     * NOTE: In Phase 7 (Tunnel), we'll pass this fd to the engine.
     *
     *       For now (Phase 3), we just store it.
     *
     *       When Phase 7 is done, we'll do:
     *           wingo_engine_set_tun_fd(g_engine, fd);
     */

    /* If engine not created yet, create it */
    if (g_engine == NULL) {
        wingo_engine_config_t config;

        wingo_engine_config_default(&config);
        config.name = "bowie-android";
        config.log_level = WINGO_LOG_INFO;
        config.log_color = false;
        config.threads = 2;
        config.max_peers = 32;
        config.max_connections = 64;

        g_engine = wingo_engine_new(&config);
        if (g_engine == NULL) {
            LOGE("Failed to create engine");
            pthread_mutex_unlock(&g_mutex);
            return JNI_FALSE;
        }

        rc = wingo_engine_init(g_engine);
        if (rc != WINGO_SUCCESS) {
            LOGE("Failed to init engine: %s", wingo_error_str(rc));
            wingo_engine_free(g_engine);
            g_engine = NULL;
            pthread_mutex_unlock(&g_mutex);
            return JNI_FALSE;
        }
    }

    /*
     * NOTE: In Phase 7, we'll call wingo_engine_run() in a separate thread.
     *
     *       For now, we just log that the fd is set.
     */

    LOGI("TUN fd set: %d", fd);

    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

/*
 * Stop the engine.
 *
 * Called from Java: BowieNative.stop()
 */
JNIEXPORT void JNICALL
Java_com_bowie_BowieNative_stop(JNIEnv *env, jclass clazz)
{
    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    LOGI("Stopping engine");

    if (g_engine != NULL) {
        wingo_engine_stop(g_engine);
        wingo_engine_free(g_engine);
        g_engine = NULL;
    }

    g_tun_fd = -1;

    LOGI("Engine stopped");

    pthread_mutex_unlock(&g_mutex);
}

/*
 * Get engine status.
 *
 * Called from Java: BowieNative.getStatus()
 *
 * Returns a string describing the engine state.
 */
JNIEXPORT jstring JNICALL
Java_com_bowie_BowieNative_getStatus(JNIEnv *env, jclass clazz)
{
    char buf[512];

    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    if (g_engine == NULL) {
        snprintf(buf, sizeof(buf),
                 "State: not running\n"
                 "TUN fd: %d",
                 g_tun_fd);
    } else {
        wingo_engine_state_t state = wingo_engine_get_state(g_engine);
        const char *state_name = wingo_engine_state_name(state);
        wingo_i64 uptime = wingo_engine_get_uptime(g_engine);

        char id_hex[WINGO_ID_HEX_SIZE];
        wingo_id id;
        wingo_engine_get_id(g_engine, &id);
        wingo_id_to_hex(&id, id_hex);

        snprintf(buf, sizeof(buf),
                 "State: %s\n"
                 "Uptime: %llds\n"
                 "Node ID: %s\n"
                 "TUN fd: %d",
                 state_name,
                 (long long)uptime,
                 id_hex,
                 g_tun_fd);
    }

    pthread_mutex_unlock(&g_mutex);

    return (*env)->NewStringUTF(env, buf);
}

/*
 * Get engine statistics.
 *
 * Called from Java: BowieNative.getStats()
 */
JNIEXPORT jstring JNICALL
Java_com_bowie_BowieNative_getStats(JNIEnv *env, jclass clazz)
{
    char buf[512];

    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    if (g_engine == NULL) {
        snprintf(buf, sizeof(buf), "Engine not running");
    } else {
        wingo_u64 events, ticks, errors;

        wingo_engine_get_stats(g_engine, &events, &ticks, &errors);

        snprintf(buf, sizeof(buf),
                 "Events: %llu\n"
                 "Ticks:  %llu\n"
                 "Errors: %llu",
                 (unsigned long long)events,
                 (unsigned long long)ticks,
                 (unsigned long long)errors);
    }

    pthread_mutex_unlock(&g_mutex);

    return (*env)->NewStringUTF(env, buf);
}

/*
 * Set log level.
 *
 * Called from Java: BowieNative.setLogLevel(level)
 */
JNIEXPORT void JNICALL
Java_com_bowie_BowieNative_setLogLevel(JNIEnv *env, jclass clazz, jint level)
{
    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    wingo_log_set_level((wingo_log_level_t)level);

    LOGI("Log level set to %d", level);
}

/*
 * Check if engine is running.
 *
 * Called from Java: BowieNative.isRunning()
 */
JNIEXPORT jboolean JNICALL
Java_com_bowie_BowieNative_isRunning(JNIEnv *env, jclass clazz)
{
    jboolean running;

    WINGO_UNUSED(env);
    WINGO_UNUSED(clazz);

    pthread_mutex_lock(&g_mutex);

    if (g_engine != NULL) {
        running = wingo_engine_is_running(g_engine) ? JNI_TRUE : JNI_FALSE;
    } else {
        running = JNI_FALSE;
    }

    pthread_mutex_unlock(&g_mutex);

    return running;
}

/*
 * Get platform info.
 *
 * Called from Java: BowieNative.getPlatformInfo()
 */
JNIEXPORT jstring JNICALL
Java_com_bowie_BowieNative_getPlatformInfo(JNIEnv *env, jclass clazz)
{
    char buf[512];

    WINGO_UNUSED(clazz);

    snprintf(buf, sizeof(buf),
             "Platform: %s\n"
             "Version:  %s\n"
             "Arch:     %s",
             wingo_platform_name(),
             wingo_platform_version(),
             wingo_platform_arch());

    return (*env)->NewStringUTF(env, buf);
}

/*
 * Get native library version.
 *
 * Called from Java: BowieNative.getVersion()
 */
JNIEXPORT jstring JNICALL
Java_com_bowie_BowieNative_getVersion(JNIEnv *env, jclass clazz)
{
    WINGO_UNUSED(clazz);

    return (*env)->NewStringUTF(env, WINGO_VERSION_STRING);
}
