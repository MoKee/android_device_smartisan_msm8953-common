/*
 * Copyright (C) 2018 The MoKee Open Source Project
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

package org.mokee.settings.device.smartisan;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.ViewConfiguration;

import com.android.internal.os.DeviceKeyHandler;

import org.mokee.internal.util.FileUtils;

public class KeyHandler implements DeviceKeyHandler {

    private static final String TAG = "KeyHandler";

    private static final int KEY_BACK_BETTERLIFE = 0;
    private static final int KEY_BACK_GF3206 = 1;
    private static final int KEY_BACK_GF3208 = 2;
    private static final int KEY_BACK_IDEX = 3;
    private static final int KEY_HOME = 4;

    private final KeyInfo[] keys = new KeyInfo[] {
        new KeyInfo("back", "betterlife-blfp"),
        new KeyInfo("back", "gf3206"),
        new KeyInfo("back", "gf3208"),
        new KeyInfo("back", "ix_btp"),
        new KeyInfo("home", "qpnp_pon");
    };

    private final int singleTapTimeout = 150;
    private final int doubleTapTimeout = ViewConfiguration.getDoubleTapTimeout();

    private long lastTapMillis = 0;

    private Handler handler = new Handler(Looper.getMainLooper());

    public KeyHandler(Context context) {
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        boolean handled = false;
        handled = handleBackKeyEvent(event) || handled;
        handled = handleHomeKeyEvent(event) || handled;
        return handled ? null : event;
    }

    private boolean handleBackKeyEvent(KeyEvent event) {
        // The sensor reports fake DOWN and UP per taps
        if (event.getAction() != KeyEvent.ACTION_UP) {
            return false;
        }

        KeyInfo matchedKey;
        int matchedKeyIndex;

        if (keys[KEY_BACK_BETTERLIFE].match(event)) {
            matchedKey = keys[KEY_BACK_BETTERLIFE];
            matchedKeyIndex = KEY_BACK_BETTERLIFE;
        } else if (keys[KEY_BACK_GF3206].match(event)) {
            matchedKey = keys[KEY_BACK_GF3206];
            matchedKeyIndex = KEY_BACK_GF3206;
        } else if (keys[KEY_BACK_GF3208].match(event)) {
            matchedKey = keys[KEY_BACK_GF3208];
            matchedKeyIndex = KEY_BACK_GF3208;
        } else if (keys[KEY_BACK_IDEX].match(event)) {
            matchedKey = keys[KEY_BACK_IDEX];
            matchedKeyIndex = KEY_BACK_IDEX;
        } else {
            return false;
        }

        injectKey(keys[KEY_HOME].keyCode, KeyEvent.ACTION_UP, KeyEvent.FLAG_CANCELED);
        handler.removeCallbacksAndMessages("home_tap");

        final long now = SystemClock.uptimeMillis();
        if (now - lastTapMillis < doubleTapTimeout) {
            injectKey(KeyEvent.KEYCODE_APP_SWITCH);
        } else {
            injectKey(KeyEvent.KEYCODE_BACK);
        }

        lastTapMillis = now;
        return true;
    }

    private boolean handleHomeKeyEvent(KeyEvent event) {
        final KeyInfo keyHome = keys[KEY_HOME];

        if (!keyHome.match(event)) {
            return false;
        }

        switch (event.getAction()) {
            case KeyEvent.ACTION_DOWN:
                injectKey(keyHome.keyCode, KeyEvent.ACTION_DOWN, 0);
                break;
            case KeyEvent.ACTION_UP:
                handler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        injectKey(keyHome.keyCode, KeyEvent.ACTION_UP, 0);
                    }
                }, "home_tap", singleTapTimeout);
                break;
        }

        return true;
    }

    private String getDeviceName(KeyEvent event) {
        final int deviceId = event.getDeviceId();
        final InputDevice device = InputDevice.getDevice(deviceId);
        return device == null ? null : device.getName();
    }

    private void injectKey(int code) {
        injectKey(code, KeyEvent.ACTION_DOWN, 0);
        injectKey(code, KeyEvent.ACTION_UP, 0);
    }

    private void injectKey(int code, int action, int flags) {
        final long now = SystemClock.uptimeMillis();
        InputManager.getInstance().injectInputEvent(new KeyEvent(
                        now, now, action, code, 0, 0,
                        KeyCharacterMap.VIRTUAL_KEYBOARD,
                        0, flags,
                        InputDevice.SOURCE_KEYBOARD),
                InputManager.INJECT_INPUT_EVENT_MODE_ASYNC);
    }

    private class KeyInfo {

        final String deviceName;
        final int scanCode;
        int deviceId;
        int keyCode;

        KeyInfo(String file, String deviceName) {
            int scanCode;
            this.deviceName = deviceName;
            try {
                scanCode = Integer.parseInt(FileUtils.readOneLine(
                        "/proc/keypad/" + file));
            } catch (NumberFormatException ignored) {
                scanCode = 0;
            }
            this.scanCode = scanCode;
        }

        boolean match(KeyEvent event) {
            if (deviceId == 0) {
                final String deviceName = getDeviceName(event);
                if (this.deviceName.equals(deviceName)) {
                    deviceId = event.getDeviceId();
                } else {
                    return false;
                }
            } else {
                if (deviceId != event.getDeviceId()) {
                    return false;
                }
            }

            if (event.getScanCode() == scanCode) {
                keyCode = event.getKeyCode();
            } else {
                return false;
            }

            return true;
        }

    }

}
