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

package org.lineageos.settings.device;

import android.os.Bundle;
import android.support.v7.preference.Preference;
import android.support.v14.preference.PreferenceFragment;
import android.support.v14.preference.SwitchPreference;
import android.text.TextUtils;

import org.lineageos.settings.device.actions.Constants;
import org.lineageos.settings.device.FileUtils;

import java.io.File;

public abstract class ActionsFragment extends PreferenceFragment {

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(getPreferenceScreenResId());

        SwitchPreference pref = (SwitchPreference) findPreference(getPreferenceKey());
        if (pref != null) {
            pref.setEnabled(isAvailable());

            Boolean isChecked = isChecked();
            if (isChecked != null) {
                pref.setChecked(isChecked.booleanValue());
            }

            pref.setOnPreferenceChangeListener(new Preference.OnPreferenceChangeListener() {
                public boolean onPreferenceChange(Preference preference, Object newValue) {
                    return setChecked((boolean) newValue);
                }
            });
        }
    }

    protected abstract int getPreferenceScreenResId();

    protected abstract String getPreferenceKey();

    protected boolean isAvailable() {
        return true;
    }

    protected Boolean isChecked() {
        return null;
    }

    protected boolean setChecked(boolean isChecked) {
        return true;
    }
}
