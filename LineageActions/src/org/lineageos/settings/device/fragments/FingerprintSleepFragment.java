/*
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

import android.text.TextUtils;

import org.lineageos.settings.device.actions.Constants;
import org.lineageos.settings.device.ActionsFragment;
import org.lineageos.settings.device.FileUtils;
import org.lineageos.settings.device.R;

import java.io.File;

public class FingerprintSleepFragment extends ActionsFragment {

    private final String DISABLED = "0";
    private final String ENABLED = "1";

    @Override
    protected int getPreferenceScreenResId() {
        return R.xml.fp_sleep_panel;
    }

    @Override
    protected String getPreferenceKey() {
        return Constants.FP_SLEEP_KEY;
    }

    @Override
    protected boolean isAvailable() {
        File node = new File(Constants.FP_SLEEP_NODE);
        return node.exists();
    }

    @Override
    protected Boolean isChecked() {
        String curNodeValue = FileUtils.readOneLine(Constants.FP_SLEEP_NODE);
        return curNodeValue.equals(ENABLED);
    }

    @Override
    protected boolean setChecked(boolean isChecked) {
        if (!TextUtils.isEmpty(Constants.FP_SLEEP_NODE)) {
            FileUtils.writeLine(Constants.FP_SLEEP_NODE,
                isChecked ? ENABLED : DISABLED);
            return true;
        }
        return false;
    }
}
