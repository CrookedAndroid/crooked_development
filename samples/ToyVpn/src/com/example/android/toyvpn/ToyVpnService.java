/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.example.android.toyvpn;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.VpnService;
import android.os.Handler;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.util.ArrayMap;
import android.util.Log;
import android.widget.Toast;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;

public class ToyVpnService extends VpnService implements Handler.Callback,
        ToyVpnConnection.Listener {
    private static final String TAG = ToyVpnService.class.getSimpleName();

    public static final String ACTION_CONNECT = "com.example.android.toyvpn.START";
    public static final String ACTION_DISCONNECT = "com.example.android.toyvpn.STOP";

    private Handler mHandler;

    private Map<Integer, Thread> mThreads = new ArrayMap<>();
    private int mNextConnectionId = 1;

    private AtomicReference<ParcelFileDescriptor> mTunnelInterface = new AtomicReference<>();
    private PendingIntent mConfigureIntent;

    @Override
    public void onCreate() {
        // The handler is only used to show messages.
        if (mHandler == null) {
            mHandler = new Handler(this);
        }

        // Create the intent to "configure" the connection (just start ToyVpnClient).
        mConfigureIntent = PendingIntent.getActivity(this, 0, new Intent(this, ToyVpnClient.class),
                PendingIntent.FLAG_UPDATE_CURRENT);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (ACTION_DISCONNECT.equals(intent.getAction())) {
            disconnect();
        } else {
            connect();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        disconnect();
    }

    @Override
    public boolean handleMessage(Message message) {
        if (message != null) {
            Toast.makeText(this, message.what, Toast.LENGTH_SHORT).show();

            if (message.what != R.string.disconnected) {
                updateForegroundNotification(message.what);
            }
        }
        return true;
    }

    @Override
    public void onEstablish(int connectionId, ParcelFileDescriptor tunInterface) {
        if (tunInterface != null) {
            mHandler.sendEmptyMessage(R.string.connected);
        }
        final ParcelFileDescriptor oldInterface = mTunnelInterface.getAndSet(tunInterface);
        if (oldInterface != null) {
            try {
                Log.i(TAG, "Closing interface: " + oldInterface);
                oldInterface.close();
            } catch (IOException e){
                Log.e(TAG, "Closing interface failed", e);
            }
        }
    }

    @Override
    public synchronized void onDisconnect(int connectionId) {
        mThreads.remove(connectionId);
    }

    @Override
    public void onRevoke() {
        disconnect();
    }

    private synchronized void connect() {
        // Become a foreground service. Background services can be VPN services too, but they can
        // be killed by background check before getting a chance to receive onRevoke().
        updateForegroundNotification(R.string.connecting);
        mHandler.sendEmptyMessage(R.string.connecting);

        final SharedPreferences prefs = getSharedPreferences(ToyVpnClient.Prefs.NAME, MODE_PRIVATE);
        final ToyVpnConnection connection;
        try {
            // Extract information from the shared preferences.
            connection = new ToyVpnConnection(this, this, mNextConnectionId,
                    prefs.getString(ToyVpnClient.Prefs.SERVER_ADDRESS, ""),
                    Integer.parseInt(prefs.getString(ToyVpnClient.Prefs.SERVER_PORT, "")),
                    prefs.getString(ToyVpnClient.Prefs.SHARED_SECRET, "").getBytes());
        } catch (NumberFormatException e) {
            Log.e(TAG, "Bad port: " + prefs.getString(ToyVpnClient.Prefs.SERVER_PORT, null), e);
            return;
        }
        connection.setConfigureIntent(mConfigureIntent);

        final Thread thread = new Thread(connection, "ToyVpnThread");
        mThreads.put(mNextConnectionId, thread);
        thread.start();

        ++mNextConnectionId;
    }

    private synchronized void disconnect() {
        mHandler.sendEmptyMessage(R.string.disconnected);

        try {
            mThreads.values().forEach(x -> x.interrupt());
        } catch (Exception e) {
            Log.e(TAG, "Interrupting thread", e);
        }
        mThreads.clear();

        // Close the connection so ConnectivityManager unbinds, taking the app out of foreground.
        onEstablish(-1, null);
        stopForeground(true);
    }

    private void updateForegroundNotification(final int message) {
        startForeground(1, new Notification.Builder(this)
                .setSmallIcon(R.drawable.ic_vpn)
                .setContentText(getString(message))
                .setContentIntent(mConfigureIntent)
                .build());
    }
}
