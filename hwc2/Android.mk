# Copyright (C) 2008 The Android Open Source Project
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

# HAL module implemenation stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)

LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)

LOCAL_PROPRIETARY_MODULE := true

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SHARED_LIBRARIES := liblog libEGL
ifeq ($(TARGET_USES_G2D),true)
		ROTATE :=other/sunxi_g2d.cpp
		LOCAL_CFLAGS += -DUSE_G2D
else
		ROTATE :=other/rotate.cpp
endif

ifeq ($(TARGET_BOARD_CHIP),sun50iw6p1)
    LOCAL_CFLAGS += -DSUN50IW6P1
endif

LOCAL_SRC_FILES :=\
	hwc.cpp \
    layer.cpp \
    de2family/DisplayOpr.cpp \
    hwc_common.cpp \
    other/ion.cpp \
    $(ROTATE) \
    other/debug.cpp \
    other/memcontrl.cpp \
    other/layerRecord.cpp \
    threadResouce/hwc_event_thread.cpp \
    threadResouce/hwc_submit_thread.cpp

ifeq ($(USE_IOMMU),true)
	LOCAL_CFLAGS += -DUSE_IOMMU
endif

ifneq ($(wildcard hardware/aw/gpu/include/hal_public.h),)
LOCAL_C_INCLUDES += hardware/aw/gpu/include
LOCAL_CFLAGS += -DHAL_PUBLIC_UNIFIED_ENABLE
endif

# support readback on composer@2.2
COMPOSER_READBACK_ENABLE := enable

ifeq ($(HWC_WRITEBACK_ENABLE), enable)
ifeq ($(HWC_WRITEBACK_MODE), always)
	LOCAL_CFLAGS += -DWB_MODE=1
else
	LOCAL_CFLAGS += -DWB_MODE=2
endif
	LOCAL_CFLAGS += -DENABLE_WRITEBACK
	LOCAL_SRC_FILES += other/write_back.cpp
else
ifeq ($(COMPOSER_READBACK_ENABLE), enable)
	LOCAL_CFLAGS += -DCOMPOSER_READBACK
	LOCAL_CFLAGS += -DWB_MODE=1
	LOCAL_SRC_FILES += other/write_back.cpp
	LOCAL_SRC_FILES += other/composer_readback.cpp
endif
endif

ifeq ($(TARGET_USES_DE30),true)
	LOCAL_CFLAGS += -DDE_VERSION=30
	LOCAL_CFLAGS += -DGRALLOC_SUNXI_METADATA_BUF
endif

ifneq ($(SF_PRIMARY_DISPLAY_ORIENTATION),)
	LOCAL_CFLAGS += -DPRIMARY_DISPLAY_ORIENTATION=$(SF_PRIMARY_DISPLAY_ORIENTATION)
endif

ifneq ($(UI_WITDH), )
	LOCAL_CFLAGS += -DUI_WITDH=$(UI_WITDH)
endif

ifneq ($(UI_HEIGHT), )
	LOCAL_CFLAGS += -DUI_HEIGHT=$(UI_HEIGHT)
endif

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libEGL \
    libGLESv1_CM \
    liblog \
    libcutils \
    libsync_aw \
    libion \

ENABLE_VENDOR_SERVICE := true
ifeq ($(ENABLE_VENDOR_SERVICE),true)
    LOCAL_SRC_FILES += other/vendorservice.cpp
    LOCAL_C_INCLUDES += \
        hardware/aw/display/include \
        hardware/aw/display/interfaces/config/1.0/src

    LOCAL_SHARED_LIBRARIES += \
        libbinder \
        libhidlbase \
        libdisplayconfig \
        vendor.display.config@1.0 \
        vendor.display.config@1.0-impl

    LOCAL_CFLAGS += -DHWC_VENDOR_SERVICE
endif

ifeq ($(TARGET_PLATFORM), homlet)
	LOCAL_SRC_FILES += other/homlet.cpp
	LOCAL_CFLAGS += -DTARGET_PLATFORM_HOMLET
endif

ifeq ($(TARGET_PLATFORM), auto)
	LOCAL_CFLAGS += -DTARGET_PLATFORM_AUTO
endif

LOCAL_C_INCLUDES += $(TARGET_HARDWARE_INCLUDE)
LOCAL_C_INCLUDES += system/core/libion/include \
    system/core/include \
    hardware/libhardware/include \
    hardware/aw/hwc2/include

LOCAL_CFLAGS += -Wno-error=unused-variable -Wno-error=unused-function -Wno-error=unused-label -Wno-error=unused-value -Wno-error=unused-parameter -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=format -Wno-error=return-type
LOCAL_CFLAGS += -DLOG_TAG=\"sunxihwc\" -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional
#TARGET_GLOBAL_CFLAGS += -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
include $(BUILD_SHARED_LIBRARY)
