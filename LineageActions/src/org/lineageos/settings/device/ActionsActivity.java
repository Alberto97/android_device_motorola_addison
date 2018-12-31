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

package org.lineageos.settings.device;

import android.app.Fragment;
import android.app.FragmentTransaction;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.preference.PreferenceActivity;

public class ActionsActivity extends PreferenceActivity {

    private static final String LOG_TAG = "ActionsActivity";

    public static final String META_DATA_KEY_FRAGMENT_CLASS =
            "org.lineageos.settings.device.FRAGMENT_CLASS";

    private String mFragmentClass;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (getActionBar() != null) {
            getActionBar().setDisplayHomeAsUpEnabled(true);
        }

        getMetaData();

        Fragment f = Fragment.instantiate(this, mFragmentClass);
        FragmentTransaction transaction = getFragmentManager().beginTransaction();
        transaction.replace(android.R.id.content, f);
        transaction.commit();
    }

    private void getMetaData() {
        try {
            ActivityInfo ai = getPackageManager().getActivityInfo(getComponentName(),
                    PackageManager.GET_META_DATA);
            if (ai == null || ai.metaData == null) return;
            mFragmentClass = ai.metaData.getString(META_DATA_KEY_FRAGMENT_CLASS);
        } catch (PackageManager.NameNotFoundException nnfe) {
            // No recovery
            throw new RuntimeException("Cannot get Metadata for: " + getComponentName().toString());
        }
    }
}
