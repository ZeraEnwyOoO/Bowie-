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

package com.bowie;

import android.util.Log;

/**
 * BowieNative — JNI Bridge to the native C engine.
 *
 * This class provides static methods that map to the native
 * functions in bowie_jni.c.
 *
 * Usage:
 *   BowieNative.init();
 *   BowieNative.start();
 *   BowieNative.startWithFd(fd);
 *   String status = BowieNative.getStatus();
 *   BowieNative.stop();
 */
public final class BowieNative {

    /* ========================================================================
     * CONSTANTS
     * ======================================================================== */

    private static final String TAG = "BowieNative";

    /*
     * Log levels (must match wingo_log_level_t in C).
     */
    public static final int LOG_TRACE  = 0;
    public static final int LOG_DEBUG  = 1;
    public static final int LOG_INFO   = 2;
    public static final int LOG_NOTICE = 3;
    public static final int LOG_WARN   = 4;
    public static final int LOG_ERROR  = 5;
    public static final int LOG_FATAL  = 6;
    public static final int LOG_NONE   = 7;

    /* ========================================================================
     * STATIC INITIALIZER
     * ======================================================================== */

    /*
     * Native library name.
     *
     * The actual file is libbowie.so, but we use "bowie" here.
     */
    private static final String LIBRARY_NAME = "bowie";

    /*
     * Flag: is the native library loaded?
     */
    private static boolean sLoaded = false;

    static {
        try {
            System.loadLibrary(LIBRARY_NAME);
            sLoaded = true;
            Log.i(TAG, "Native library loaded: " + LIBRARY_NAME);
        } catch (UnsatisfiedLinkError e) {
            sLoaded = false;
            Log.e(TAG, "Failed to load native library: " + LIBRARY_NAME, e);
        }
    }

    /* ========================================================================
     * CONSTRUCTOR (PRIVATE)
     * ======================================================================== */

    /*
     * Private constructor — this is a utility class with static methods.
     */
    private BowieNative() {
        throw new UnsupportedOperationException("Utility class");
    }

    /* ========================================================================
     * LIBRARY STATUS
     * ======================================================================== */

    /**
     * Check if the native library is loaded.
     *
     * @return true if loaded, false otherwise
     */
    public static boolean isLoaded() {
        return sLoaded;
    }

    /**
     * Ensure the native library is loaded.
     *
     * @throws IllegalStateException if not loaded
     */
    private static void ensureLoaded() {
        if (!sLoaded) {
            throw new IllegalStateException(
                "Native library not loaded: " + LIBRARY_NAME);
        }
    }

    /* ========================================================================
     * NATIVE METHODS
     * ======================================================================== */

    /**
     * Initialize the native library.
     *
     * This must be called before any other native method.
     *
     * Maps to: Java_com_bowie_BowieNative_init()
     */
    public static void init() {
        ensureLoaded();
        nativeInit();
    }

    /**
     * Start the engine.
     *
     * Creates and initializes the engine, but does not start
     * the main loop. Use startWithFd() to start with a TUN fd.
     *
     * Maps to: Java_com_bowie_BowieNative_start()
     *
     * @return true on success, false on failure
     */
    public static boolean start() {
        ensureLoaded();
        return nativeStart();
    }

    /**
     * Start the engine with a TUN file descriptor.
     *
     * This is called from BowieVpnService after VpnService.establish()
     * provides the TUN fd.
     *
     * Maps to: Java_com_bowie_BowieNative_startWithFd()
     *
     * @param fd TUN file descriptor
     * @return true on success, false on failure
     */
    public static boolean startWithFd(int fd) {
        ensureLoaded();
        return nativeStartWithFd(fd);
    }

    /**
     * Stop the engine.
     *
     * Maps to: Java_com_bowie_BowieNative_stop()
     */
    public static void stop() {
        ensureLoaded();
        nativeStop();
    }

    /**
     * Get engine status.
     *
     * Maps to: Java_com_bowie_BowieNative_getStatus()
     *
     * @return status string
     */
    public static String getStatus() {
        ensureLoaded();
        return nativeGetStatus();
    }

    /**
     * Get engine statistics.
     *
     * Maps to: Java_com_bowie_BowieNative_getStats()
     *
     * @return statistics string
     */
    public static String getStats() {
        ensureLoaded();
        return nativeGetStats();
    }

    /**
     * Set log level.
     *
     * Maps to: Java_com_bowie_BowieNative_setLogLevel()
     *
     * @param level log level (use LOG_* constants)
     */
    public static void setLogLevel(int level) {
        ensureLoaded();
        nativeSetLogLevel(level);
    }

    /**
     * Check if engine is running.
     *
     * Maps to: Java_com_bowie_BowieNative_isRunning()
     *
     * @return true if running, false otherwise
     */
    public static boolean isRunning() {
        ensureLoaded();
        return nativeIsRunning();
    }

    /**
     * Get platform info.
     *
     * Maps to: Java_com_bowie_BowieNative_getPlatformInfo()
     *
     * @return platform info string
     */
    public static String getPlatformInfo() {
        ensureLoaded();
        return nativeGetPlatformInfo();
    }

    /**
     * Get native library version.
     *
     * Maps to: Java_com_bowie_BowieNative_getVersion()
     *
     * @return version string
     */
    public static String getVersion() {
        ensureLoaded();
        return nativeGetVersion();
    }

    /* ========================================================================
     * NATIVE DECLARATIONS
     * ======================================================================== */

    /*
     * These are implemented in bowie_jni.c.
     */

    private static native void nativeInit();
    private static native boolean nativeStart();
    private static native boolean nativeStartWithFd(int fd);
    private static native void nativeStop();
    private static native String nativeGetStatus();
    private static native String nativeGetStats();
    private static native void nativeSetLogLevel(int level);
    private static native boolean nativeIsRunning();
    private static native String nativeGetPlatformInfo();
    private static native String nativeGetVersion();
}
