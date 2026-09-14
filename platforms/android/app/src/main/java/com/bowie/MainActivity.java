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

import android.app.Activity;
import android.content.Intent;
import android.net.VpnService;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * MainActivity — Bowie Terminal UI.
 *
 * This is a simple terminal-style UI for Bowie.
 *
 * Features:
 *   - Output area (scrollable log)
 *   - Input area (command input)
 *   - Start/Stop VPN buttons
 *   - Status display
 *
 * Commands:
 *   start       — Start VPN
 *   stop        — Stop VPN
 *   status      — Show engine status
 *   stats       — Show statistics
 *   platform    — Show platform info
 *   version     — Show version
 *   clear       — Clear output
 *   help        — Show help
 */
public class MainActivity extends Activity {

    /* ========================================================================
     * CONSTANTS
     * ======================================================================== */

    private static final String TAG = "BowieMain";

    /*
     * VPN permission request code.
     */
    private static final int VPN_REQUEST_CODE = 0x0B0E;

    /*
     * Status update interval (ms).
     */
    private static final long STATUS_UPDATE_INTERVAL = 2000;

    /* ========================================================================
     * UI COMPONENTS
     * ======================================================================== */

    private ScrollView mScrollView;
    private TextView mOutputView;
    private EditText mInputView;
    private Button mSendButton;
    private Button mStartButton;
    private Button mStopButton;

    /* ========================================================================
     * STATE
     * ======================================================================== */

    /*
     * Handler for UI updates.
     */
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    /*
     * Flag: is VPN running?
     */
    private boolean mVpnRunning = false;

    /*
     * Status update runnable.
     */
    private final Runnable mStatusUpdater = new Runnable() {
        @Override
        public void run() {
            updateStatus();
            if (mVpnRunning) {
                mHandler.postDelayed(this, STATUS_UPDATE_INTERVAL);
            }
        }
    };

    /* ========================================================================
     * LIFECYCLE
     * ======================================================================== */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "MainActivity created");

        /* Build UI programmatically */
        buildUI();

        /* Initialize native */
        if (BowieNative.isLoaded()) {
            BowieNative.init();
            BowieNative.setLogLevel(BowieNative.LOG_INFO);

            appendOutput("Bowie Terminal v" + BowieNative.getVersion());
            appendOutput("Platform: " + BowieNative.getPlatformInfo());
            appendOutput("");
            appendOutput("Type 'help' for commands.");
            appendOutput("");
        } else {
            appendOutput("ERROR: Native library not loaded!");
            appendOutput("The app cannot function without libbowie.so");
            appendOutput("");
        }
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "MainActivity destroyed");

        /* Stop status updates */
        mHandler.removeCallbacks(mStatusUpdater);

        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();

        /* Resume status updates if VPN is running */
        if (mVpnRunning) {
            mHandler.post(mStatusUpdater);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        /* Stop status updates */
        mHandler.removeCallbacks(mStatusUpdater);
    }

    /* ========================================================================
     * UI BUILDING
     * ======================================================================== */

    /**
     * Build the UI programmatically.
     *
     * We don't use XML layouts because this is a simple terminal UI.
     */
    private void buildUI() {
        /* Root layout */
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF000000);  /* Black */
        root.setPadding(16, 16, 16, 16);

        /* Title */
        TextView title = new TextView(this);
        title.setText("Bowie Terminal");
        title.setTextColor(0xFF00FF00);  /* Green */
        title.setTextSize(18);
        title.setPadding(0, 0, 0, 16);
        root.addView(title);

        /* Output area */
        mScrollView = new ScrollView(this);
        mScrollView.setLayoutParams(new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f
        ));

        mOutputView = new TextView(this);
        mOutputView.setTextColor(0xFF00FF00);  /* Green */
        mOutputView.setTextSize(12);
        mOutputView.setTypeface(android.graphics.Typeface.MONOSPACE);
        mOutputView.setMovementMethod(new ScrollingMovementMethod());
        mOutputView.setTextIsSelectable(true);

        mScrollView.addView(mOutputView);
        root.addView(mScrollView);

        /* Button row */
        LinearLayout buttonRow = new LinearLayout(this);
        buttonRow.setOrientation(LinearLayout.HORIZONTAL);
        buttonRow.setPadding(0, 16, 0, 16);

        /* Start button */
        mStartButton = new Button(this);
        mStartButton.setText("START");
        mStartButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                onStartClicked();
            }
        });
        buttonRow.addView(mStartButton);

        /* Stop button */
        mStopButton = new Button(this);
        mStopButton.setText("STOP");
        mStopButton.setEnabled(false);
        mStopButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                onStopClicked();
            }
        });
        buttonRow.addView(mStopButton);

        root.addView(buttonRow);

        /* Input row */
        LinearLayout inputRow = new LinearLayout(this);
        inputRow.setOrientation(LinearLayout.HORIZONTAL);

        mInputView = new EditText(this);
        mInputView.setHint("Command...");
        mInputView.setHintTextColor(0xFF888888);
        mInputView.setTextColor(0xFF00FF00);
        mInputView.setTypeface(android.graphics.Typeface.MONOSPACE);
        mInputView.setImeOptions(EditorInfo.IME_ACTION_SEND);
        mInputView.setSingleLine(true);
        mInputView.setLayoutParams(new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f
        ));
        mInputView.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                onSendClicked();
                return true;
            }
            return false;
        });
        inputRow.addView(mInputView);

        mSendButton = new Button(this);
        mSendButton.setText("Send");
        mSendButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                onSendClicked();
            }
        });
        inputRow.addView(mSendButton);

        root.addView(inputRow);

        setContentView(root);
    }

    /* ========================================================================
     * UI EVENTS
     * ======================================================================== */

    /**
     * Handle "START" button.
     */
    private void onStartClicked() {
        appendOutput("> start");

        /* Request VPN permission */
        Intent intent = VpnService.prepare(this);
        if (intent != null) {
            /* Permission needed */
            startActivityForResult(intent, VPN_REQUEST_CODE);
        } else {
            /* Already have permission */
            startVpnService();
        }
    }

    /**
     * Handle "STOP" button.
     */
    private void onStopClicked() {
        appendOutput("> stop");
        stopVpnService();
    }

    /**
     * Handle "Send" button.
     */
    private void onSendClicked() {
        String cmd = mInputView.getText().toString().trim();
        if (cmd.isEmpty()) {
            return;
        }

        mInputView.setText("");

        appendOutput("> " + cmd);

        executeCommand(cmd);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == VPN_REQUEST_CODE) {
            if (resultCode == RESULT_OK) {
                appendOutput("VPN permission granted");
                startVpnService();
            } else {
                appendOutput("VPN permission denied");
                Toast.makeText(this, "VPN permission denied",
                               Toast.LENGTH_SHORT).show();
            }
        }
    }

    /* ========================================================================
     * VPN CONTROL
     * ======================================================================== */

    /**
     * Start the VPN service.
     */
    private void startVpnService() {
        if (mVpnRunning) {
            appendOutput("VPN already running");
            return;
        }

        appendOutput("Starting VPN...");

        Intent intent = new Intent(this, BowieVpnService.class);
        intent.setAction(BowieVpnService.ACTION_START);

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }

        mVpnRunning = true;
        updateButtons();

        /* Start status updates */
        mHandler.post(mStatusUpdater);

        appendOutput("VPN started");
    }

    /**
     * Stop the VPN service.
     */
    private void stopVpnService() {
        if (!mVpnRunning) {
            appendOutput("VPN not running");
            return;
        }

        appendOutput("Stopping VPN...");

        Intent intent = new Intent(this, BowieVpnService.class);
        intent.setAction(BowieVpnService.ACTION_STOP);
        startService(intent);

        mVpnRunning = false;
        updateButtons();

        /* Stop status updates */
        mHandler.removeCallbacks(mStatusUpdater);

        appendOutput("VPN stopped");
    }

    /**
     * Update button states.
     */
    private void updateButtons() {
        mStartButton.setEnabled(!mVpnRunning);
        mStopButton.setEnabled(mVpnRunning);
    }

    /* ========================================================================
     * COMMANDS
     * ======================================================================== */

    /**
     * Execute a terminal command.
     */
    private void executeCommand(String cmd) {
        String[] parts = cmd.split("\\s+", 2);
        String command = parts[0].toLowerCase(Locale.US);
        String args = (parts.length > 1) ? parts[1] : "";

        switch (command) {
        case "start":
            onStartClicked();
            break;

        case "stop":
            onStopClicked();
            break;

        case "status":
            cmdStatus();
            break;

        case "stats":
            cmdStats();
            break;

        case "platform":
            cmdPlatform();
            break;

        case "version":
            cmdVersion();
            break;

        case "clear":
            mOutputView.setText("");
            break;

        case "help":
            cmdHelp();
            break;

        default:
            appendOutput("Unknown command: " + command);
            appendOutput("Type 'help' for commands.");
            break;
        }
    }

    /**
     * Command: status
     */
    private void cmdStatus() {
        if (!BowieNative.isLoaded()) {
            appendOutput("Native library not loaded");
            return;
        }

        String status = BowieNative.getStatus();
        appendOutput(status);
    }

    /**
     * Command: stats
     */
    private void cmdStats() {
        if (!BowieNative.isLoaded()) {
            appendOutput("Native library not loaded");
            return;
        }

        String stats = BowieNative.getStats();
        appendOutput(stats);
    }

    /**
     * Command: platform
     */
    private void cmdPlatform() {
        if (!BowieNative.isLoaded()) {
            appendOutput("Native library not loaded");
            return;
        }

        String info = BowieNative.getPlatformInfo();
        appendOutput(info);
    }

    /**
     * Command: version
     */
    private void cmdVersion() {
        if (!BowieNative.isLoaded()) {
            appendOutput("Native library not loaded");
            return;
        }

        String version = BowieNative.getVersion();
        appendOutput("Bowie v" + version);
    }

    /**
     * Command: help
     */
    private void cmdHelp() {
        appendOutput("Commands:");
        appendOutput("  start      Start VPN");
        appendOutput("  stop       Stop VPN");
        appendOutput("  status     Show engine status");
        appendOutput("  stats      Show statistics");
        appendOutput("  platform   Show platform info");
        appendOutput("  version    Show version");
        appendOutput("  clear      Clear output");
        appendOutput("  help       Show this help");
    }

    /* ========================================================================
     * STATUS UPDATES
     * ======================================================================== */

    /**
     * Update status display.
     */
    private void updateStatus() {
        if (!BowieNative.isLoaded()) {
            return;
        }

        if (!BowieNative.isRunning()) {
            mVpnRunning = false;
            updateButtons();
            return;
        }

        /* Status is updated by commands only */
        /* We don't spam output with status */
    }

    /* ========================================================================
     * OUTPUT
     * ======================================================================== */

    /**
     * Append text to output area.
     *
     * @param text text to append
     */
    private void appendOutput(String text) {
        /* Add timestamp */
        SimpleDateFormat sdf = new SimpleDateFormat("HH:mm:ss", Locale.US);
        String timestamp = sdf.format(new Date());

        /* Append with newline */
        mOutputView.append("[" + timestamp + "] " + text + "\n");

        /* Scroll to bottom */
        mScrollView.post(new Runnable() {
            @Override
            public void run() {
                mScrollView.fullScroll(View.FOCUS_DOWN);
            }
        });
    }
}
