/*
 * Copyright (C) 2017 The LineageOS Project
 * Copyright (C) 2017-2018 Alberto97
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

#include <android/sensor.h>
#include <hardware/sensors.h>
#include <sensors/convert.h>

using android::hardware::sensors::V1_0::SensorInfo;

extern "C" int ASensor_getHandle(ASensor const* sensor)
{
    return reinterpret_cast<const SensorInfo *>(sensor)->sensorHandle;
}
