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

import android.support.v7.preference.Preference;
import android.support.v14.preference.PreferenceFragment;
import android.support.v14.preference.SwitchPreference;
import android.text.TextUtils;

import org.lineageos.settings.device.actions.Constants;
import org.lineageos.settings.device.FileUtils;

import java.io.File;

public abstract class ActionsFragment extends PreferenceFragment {

    public void setupSysfsSwitch(String key) {
        SwitchPreference pref = (SwitchPreference) findPreference(key);
        if (pref != null) {

            // Set UI status
            String node = Constants.sBooleanNodePreferenceMap.get(key);
            if (node != null && new File(node).exists()) {
                String curNodeValue = FileUtils.readOneLine(node);
                pref.setChecked(curNodeValue.equals("1"));
            } else {
                pref.setEnabled(false);
            }

            // Enable/Disable behavior
            pref.setOnPreferenceChangeListener(new Preference.OnPreferenceChangeListener() {
                public boolean onPreferenceChange(Preference preference, Object newValue) {
                    if (!TextUtils.isEmpty(node)) {
                        Boolean value = (Boolean) newValue;
                        FileUtils.writeLine(node, value ? "1" : "0");
                        return true;
                    }
                    return false;
                }
            });
        }
    }
}
