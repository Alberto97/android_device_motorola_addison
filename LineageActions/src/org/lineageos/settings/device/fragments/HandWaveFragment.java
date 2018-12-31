/*
 * Copyright (c) 2017 The LineageOS Project
 * Copyright (c) 2018 Alberto Pedron
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

package org.lineageos.settings.device.fragments;

import android.content.Context;
import android.os.Bundle;
import android.os.UserHandle;
import android.provider.Settings;
import android.support.v14.preference.SwitchPreference;

import com.android.internal.hardware.AmbientDisplayConfiguration;

import org.lineageos.settings.device.ActionsFragment;
import org.lineageos.settings.device.R;

public class HandWaveFragment extends ActionsFragment {

    private final String PREFERENCE_HAND_WAVE = "gesture_hand_wave";

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(R.xml.hand_wave_panel);

        AmbientDisplayConfiguration adConfig = new AmbientDisplayConfiguration(getActivity());
        boolean dozeEnabled = adConfig.pulseOnNotificationEnabled(UserHandle.USER_CURRENT);
        boolean aodEnabled = adConfig.alwaysOnEnabled(UserHandle.USER_CURRENT);
        boolean pickupEnabled = isPulseOnPickupEnabled(getActivity()) && adConfig.pulseOnPickupAvailable();

        boolean enable = (pickupEnabled || dozeEnabled) && !aodEnabled;

        SwitchPreference pref = (SwitchPreference) findPreference(PREFERENCE_HAND_WAVE);
        if (pref != null) {
            pref.setEnabled(enable);
        }
    }

    private boolean isPulseOnPickupEnabled(Context context) {
        return Settings.Secure.getIntForUser(context.getContentResolver(),
                Settings.Secure.DOZE_PULSE_ON_PICK_UP, 1, UserHandle.USER_CURRENT) != 0;
    }
}
