#
# Copyright (C) 2015-2016 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

DEVICE_PATH := device/motorola/addison

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a53

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := MSM8953
TARGET_NO_BOOTLOADER := true

# Crypto
TARGET_HW_DISK_ENCRYPTION := true

# Platform
TARGET_BOARD_PLATFORM := msm8953

# Kernel
BOARD_CUSTOM_BUILD_MAKEFILE := $(DEVICE_PATH)/dt_image.mk
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_CMDLINE := androidboot.hardware=qcom ehci-hcd.park=3 androidboot.bootdevice=7824900.sdhci androidboot.selinux=permissive
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_PREBUILT_DT := true
BOARD_KERNEL_SEPARATED_DT := true
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x01000000 --tags_offset 0x00000100
LZMA_RAMDISK_TARGETS := recovery
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/kernel
TARGET_PREBUILT_DTB := $(DEVICE_PATH)/prebuilt/dt.img

ifeq ($(strip $(TARGET_PREBUILT_KERNEL)),)
TARGET_KERNEL_CONFIG := addison_defconfig
TARGET_KERNEL_SOURCE := kernel/motorola/msm8953
endif

# Partitions
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216        #    16384 * 1024 mmcblk0p37
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16879616    #    16484 * 1024 mmcblk0p38
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 4294967296    #  4194304 * 1024 mmcblk0p53
BOARD_USERDATAIMAGE_PARTITION_SIZE := 25614597120 # 25014255 * 1024 mmcblk0p54
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# TWRP
RECOVERY_SDCARD_ON_DATA := true
TARGET_RECOVERY_QCOM_RTC_FIX := true
TW_INCLUDE_CRYPTO := true
TW_NEW_ION_HEAP := true
TW_SCREEN_BLANK_ON_BOOT := true
TW_THEME := portrait_hdpi
