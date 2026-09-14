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

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.net.VpnService;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.IOException;

/**
 * BowieVpnService — Android VpnService for Bowie.
 *
 * This service:
 *   - Creates the TUN interface via VpnService.Builder
 *   - Configures IP address, MTU, routes
 *   - Passes the TUN fd to the native engine
 *   - Shows a persistent notification
 *   - Handles service lifecycle
 *
 * The native engine (via JNI) reads/writes the TUN fd.
 *
 * Usage (from MainActivity):
 *   Intent intent = new Intent(this, BowieVpnService.class);
 *   intent.setAction(BowieVpnService.ACTION_START);
 *   startService(intent);
 */
public class BowieVpnService extends VpnService {

    /* ========================================================================
     * CONSTANTS
     * ======================================================================== */

    private static final String TAG = "BowieVpnService";

    /*
     * Notification.
     */
    private static final String CHANNEL_ID = "bowie_vpn";
    private static final String CHANNEL_NAME = "Bowie VPN";
    private static final int NOTIFICATION_ID = 1;

    /*
     * Intent actions.
     */
    public static final String ACTION_START = "com.bowie.action.START";
    public static final String ACTION_STOP = "com.bowie.action.STOP";

    /*
     * VPN configuration.
     */
    private static final String VPN_SESSION_NAME = "Bowie";
    private static final String VPN_ADDRESS = "10.0.0.2";
    private static final int VPN_PREFIX_LENGTH = 32;
    private static final String VPN_ROUTE = "0.0.0.0";
    private static final int VPN_ROUTE_PREFIX = 0;
    private static final int VPN_MTU = 1400;

    /*
     * DNS servers.
     */
    private static final String VPN_DNS1 = "1.1.1.1";
    private static final String VPN_DNS2 = "8.8.8.8";

    /* ========================================================================
     * STATE
     * ======================================================================== */

    /*
     * TUN file descriptor.
     */
    private ParcelFileDescriptor mTunFd = null;

    /*
     * Flag: is the service running?
     */
    private boolean mRunning = false;

    /* ========================================================================
     * LIFECYCLE
     * ======================================================================== */

    @Override
    public void onCreate() {
        super.onCreate();

        Log.i(TAG, "Service created");

        /* Create notification channel */
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = (intent != null) ? intent.getAction() : null;

        Log.i(TAG, "onStartCommand: action=" + action);

        if (ACTION_START.equals(action)) {
            startVpn();
            return START_STICKY;
        }

        if (ACTION_STOP.equals(action)) {
            stopVpn();
            stopSelf();
            return START_NOT_STICKY;
        }

        /* Default: start VPN */
        startVpn();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "Service destroyed");

        stopVpn();

        super.onDestroy();
    }

    /* ========================================================================
     * VPN CONTROL
     * ======================================================================== */

    /**
     * Start the VPN.
     */
    private void startVpn() {
        if (mRunning) {
            Log.w(TAG, "VPN already running");
            return;
        }

        Log.i(TAG, "Starting VPN...");

        /* Build VPN interface */
        Builder builder = new Builder();

        /* Session name */
        builder.setSession(VPN_SESSION_NAME);

        /* IP address */
        builder.addAddress(VPN_ADDRESS, VPN_PREFIX_LENGTH);

        /* Routes */
        builder.addRoute(VPN_ROUTE, VPN_ROUTE_PREFIX);

        /* DNS servers */
        builder.addDnsServer(VPN_DNS1);
        builder.addDnsServer(VPN_DNS2);

        /* MTU */
        builder.setMtu(VPN_MTU);

        /* Blocking mode (API 29+) */
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            builder.setBlocking(true);
        }

        /* Establish TUN */
        try {
            mTunFd = builder.establish();
        } catch (Exception e) {
            Log.e(TAG, "Failed to establish VPN", e);
            mTunFd = null;
            return;
        }

        if (mTunFd == null) {
            Log.e(TAG, "Failed to establish VPN: fd is null");
            return;
        }

        int fd = mTunFd.getFd();
        Log.i(TAG, "VPN established: fd=" + fd);

        /* Initialize native engine */
        if (!BowieNative.isLoaded()) {
            Log.e(TAG, "Native library not loaded");
            closeTun();
            return;
        }

        /* Init native */
        BowieNative.init();

        /* Start engine with TUN fd */
        boolean ok = BowieNative.startWithFd(fd);
        if (!ok) {
            Log.e(TAG, "Failed to start native engine");
            closeTun();
            return;
        }

        Log.i(TAG, "Native engine started with fd=" + fd);

        mRunning = true;

        /* Show notification */
        showNotification();
    }

    /**
     * Stop the VPN.
     */
    private void stopVpn() {
        if (!mRunning && mTunFd == null) {
            Log.d(TAG, "VPN not running");
            return;
        }

        Log.i(TAG, "Stopping VPN...");

        /* Stop native engine */
        if (BowieNative.isLoaded()) {
            BowieNative.stop();
        }

        /* Close TUN */
        closeTun();

        mRunning = false;

        /* Cancel notification */
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.cancel(NOTIFICATION_ID);
        }

        Log.i(TAG, "VPN stopped");
    }

    /**
     * Close TUN fd.
     */
    private void closeTun() {
        if (mTunFd != null) {
            try {
                mTunFd.close();
            } catch (IOException e) {
                Log.e(TAG, "Failed to close TUN", e);
            }
            mTunFd = null;
        }
    }

    /* ========================================================================
     * NOTIFICATION
     * ======================================================================== */

    /**
     * Create notification channel (API 26+).
     */
    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Bowie VPN status");

            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(channel);
            }
        }
    }

    /**
     * Show persistent notification.
     */
    private void showNotification() {
        /* Intent to open MainActivity */
        Intent intent = new Intent(this, MainActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                        Intent.FLAG_ACTIVITY_CLEAR_TOP);

        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        }

        PendingIntent pendingIntent = PendingIntent.getActivity(
            this, 0, intent, pendingFlags
        );

        /* Build notification */
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        builder.setContentTitle("Bowie VPN")
               .setContentText("P2P Internet Sharing is active")
               .setSmallIcon(android.R.drawable.ic_dialog_info)
               .setContentIntent(pendingIntent)
               .setOngoing(true);

        /* Show as foreground service */
        startForeground(NOTIFICATION_ID, builder.build());
    }

    /* ========================================================================
     * PUBLIC API
     * ======================================================================== */

    /**
     * Check if VPN is running.
     *
     * @return true if running, false otherwise
     */
    public boolean isRunning() {
        return mRunning;
    }

    /**
     * Get TUN file descriptor.
     *
     * @return fd, or -1 if not running
     */
    public int getTunFd() {
        return (mTunFd != null) ? mTunFd.getFd() : -1;
    }
}
