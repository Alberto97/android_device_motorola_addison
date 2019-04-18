/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "VibratorService"

#include <log/log.h>

#include <hardware/hardware.h>
#include <hardware/vibrator.h>

#include "Vibrator.h"

#include <cinttypes>
#include <cmath>
#include <iostream>
#include <fstream>


namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

static constexpr int MAX_VOLTAGE = 3596;
static constexpr int MIN_VOLTAGE = 116;

static constexpr uint32_t CLICK_TIMING_MS = 20;

Vibrator::Vibrator(std::ofstream&& enable, std::ofstream&& amplitude) :
        mEnable(std::move(enable)),
        mAmplitude(std::move(amplitude)) {}

volatile uint32_t timeout;

int32_t readFile(const std::string &filename, std::string *contents) {
    FILE *fp;
    ssize_t read = 0;
    char *line = NULL;
    size_t len = 0;
    fp = fopen(filename.c_str(), "r");
    if (fp != NULL) {
        if ((read = getline(&line, &len, fp)) != -1) {
            char *pos;
            if ((pos = strchr(line, '\n')) != NULL) *pos = '\0';
            *contents = line;
        }
        free(line);
        fclose(fp);
        return 0;
    } else {
        ALOGE("fopen failed");
    }
    return -1;
}

bool isTabletop() {
    std::string status;
    std::string filename =
      "/sys/devices/virtual/motosh/motosh_as/tabletop_mode";

    if (readFile(filename, &status)) {
        ALOGE("isTabletop: Failed to open filesystem node: %s",
              filename.c_str());
        return false;
    }

    int out = std::stoi(status);

    return out > 0;
}

// Methods from ::android::hardware::vibrator::V1_0::IVibrator follow.
Return<Status> Vibrator::on(uint32_t timeout_ms) {
    timeout = timeout_ms;
    mEnable << timeout_ms << std::endl;
    if (!mEnable) {
        ALOGE("Failed to turn vibrator on (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<Status> Vibrator::off()  {
    mEnable << 0 << std::endl;
    if (!mEnable) {
        ALOGE("Failed to turn vibrator off (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<bool> Vibrator::supportsAmplitudeControl()  {
    return true;
}

Return<Status> Vibrator::setAmplitude(uint8_t amplitude) {
    if (amplitude == 0) {
        return Status::BAD_VALUE;
    }

    if (isTabletop() && timeout > 100 && amplitude > 96) {
        amplitude = 96;
        ALOGI("On a table, lowering amplitude");
    }

    // Scale the voltage such that an amplitude of 1 is MIN_VOLTAGE, an amplitude of 255 is
    // MAX_VOLTAGE, and there are equal steps for every value in between.
    long voltage =
            std::lround((amplitude - 1) / 254.0 * (MAX_VOLTAGE - MIN_VOLTAGE) + MIN_VOLTAGE);
    ALOGE("Setting amplitude  to: %ld", voltage);
    mAmplitude << voltage << std::endl;
    if (!mAmplitude) {
        ALOGE("Failed to set amplitude (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<void> Vibrator::perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) {
    if (effect == Effect::CLICK) {
        uint8_t amplitude;
        switch (strength) {
        case EffectStrength::LIGHT:
            amplitude = 36;
            break;
        case EffectStrength::MEDIUM:
            amplitude = 128;
            break;
        case EffectStrength::STRONG:
            amplitude = 255;
            break;
        default:
            _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
            return Void();
        }
        on(CLICK_TIMING_MS);
        setAmplitude(amplitude);
        _hidl_cb(Status::OK, CLICK_TIMING_MS);
    } else {
        _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
    }
    return Void();
}

} // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
