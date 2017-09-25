#
# Copyright (C) 2016 The CyanogenMod Project
# Copyright (C) 2017 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

# ADSP
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/tinyalsa/include
LOCAL_SRC_FILES := mixer.c
LOCAL_MODULE := libshim_adsp
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# RIL
include $(CLEAR_VARS)
LOCAL_SRC_FILES := sensor.cpp
LOCAL_SHARED_LIBRARIES := libsensor
LOCAL_MODULE := libshim_ril
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# GPS
include $(CLEAR_VARS)
LOCAL_SRC_FILES := get_process_name.c
LOCAL_MODULE := libshims_get_process_name
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# Camera
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    GraphicBuffer.cpp \
    gui/GraphicBuffer.cpp \
    gui/GraphicBufferAlloc.cpp \
    gui/IGraphicBufferAlloc.cpp

LOCAL_SHARED_LIBRARIES := \
    libbinder \
    libgui \
    libhardware \
    libutils \
    liblog \
    libcutils \
    libui

LOCAL_MODULE := libshim_buffer
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := MediaCodec.cpp
LOCAL_SHARED_LIBRARIES := libstagefright libmedia
LOCAL_MODULE := libshim_just
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# Camera
include $(CLEAR_VARS)
LOCAL_SRC_FILES := camera_shim.cpp
LOCAL_MODULE := libshim_camera
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
