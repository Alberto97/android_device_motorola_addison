/*
 * Copyright (c) 2015 The CyanogenMod Project
 * Copyright (c) 2017 The LineageOS Project
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

package org.lineageos.settings.device.actions;

import android.content.Context;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.support.annotation.NonNull;

import org.lineageos.settings.device.SensorAction;

public class TorchAction implements SensorAction {
    private static final String TAG = "LineageActions";

    private CameraManager mCameraManager;
    private final Vibrator mVibrator;
    private String mRearCameraId;
    private static boolean mTorchEnabled;

    public TorchAction(Context mContext) {
        mCameraManager = (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
        mVibrator = (Vibrator) mContext.getSystemService(Context.VIBRATOR_SERVICE);
        try {
            for (final String cameraId : mCameraManager.getCameraIdList()) {
                CameraCharacteristics characteristics =
                        mCameraManager.getCameraCharacteristics(cameraId);
                int cOrientation = characteristics.get(CameraCharacteristics.LENS_FACING);
                if (cOrientation == CameraCharacteristics.LENS_FACING_BACK) {
                    mRearCameraId = cameraId;
                    break;
                }
            }
        } catch (CameraAccessException e) {
            // Noop
        }
    }

    @Override
    public void action() {
        VibrationEffect vibrationEffect = VibrationEffect.createOneShot(250, VibrationEffect.DEFAULT_AMPLITUDE);
        mVibrator.vibrate(vibrationEffect);
        if (mRearCameraId != null) {
            try {
                mCameraManager.setTorchMode(mRearCameraId, !mTorchEnabled);
                mTorchEnabled = !mTorchEnabled;
            } catch (CameraAccessException e) {
                // Noop
            }
        }
    }

    private class MyTorchCallback extends CameraManager.TorchCallback {

        @Override
        public void onTorchModeChanged(@NonNull String cameraId, boolean enabled) {
            if (!cameraId.equals(mRearCameraId))
                return;
            mTorchEnabled = enabled;
        }

        @Override
        public void onTorchModeUnavailable(@NonNull String cameraId) {
            if (!cameraId.equals(mRearCameraId))
                return;
            mTorchEnabled = false;
        }
    }
}
