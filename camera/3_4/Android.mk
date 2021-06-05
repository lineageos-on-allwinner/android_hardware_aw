ifeq ($(USE_CAMERA_HAL_3_4), true)

v4l2_local_path := $(call my-dir)
top_path := $(v4l2_local_path)/../../../..

#$(warning $(TARGET_BOARD_PLATFORM))

# Common setting.
# ==============================================================================
#v4l2_cflags := -D__USE_ONE_THREAD__
#v4l2_cflags := -D__USE_MULTI_THREAD__
v4l2_cflags := \

v4l2_shared_libs := \
  libbase \
  libcamera_metadata \
  libcutils \
  libhardware \
  liblog \
  libsync \
  libutils \
  libbinder \
  libui \
  libexpat \
  libvencoder.vendor

v4l2_static_libs := \
  android.hardware.camera.common@1.0-helper

v4l2_cflags += -fno-short-enums -Wall -Wextra -fvisibility=hidden -Wc++11-narrowing -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM) -Wno-unused-parameter -Wno-macro-redefined -Wno-unused-parameter -Wno-extra-tokens -Wno-null-arithmetic -Wno-format -Wno-reorder -Wno-unused-variable -Wno-writable-strings -Wno-logical-op-parentheses -Wno-sign-compare -Wno-unused-parameter -Wno-unused-value -Wno-unused-function -Wno-parentheses -Wno-extern-c-compat -Wno-null-conversion  -Wno-sometimes-uninitialized -Wno-gnu-designator -Wno-unused-label -Wno-pointer-arith -Wno-empty-body -fPIC -Wno-missing-field-initializers -Wno-pessimizing-move -Wno-unused-private-field -Wno-user-defined-warnings

#v4l2_cflags := -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM) -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)


# Include the build/core/pathmap.mk mapping from pathmap_INCL
v4l2_c_includes := $(call include-path-for, camera) \
	$(v4l2_local_path) \
	$(v4l2_local_path)/include \
	$(top_path)/system/core/libion/kernel-headers \
	$(top_path)/hardware/interfaces/camera/common/1.0/default/include \
	$(top_path)/frameworks/av/media/libcedarc/include \
#	$(v4l2_local_path)/../../../../frameworks/native/libs/nativewindow/include \
#	$(v4l2_local_path)/../../../../frameworks/native/libs/nativebase/include \

v4l2_src_files := \
  common.cpp \
  camera.cpp \
  capture_request.cpp \
  format_metadata_factory.cpp \
  metadata/boottime_state_delegate.cpp \
  metadata/enum_converter.cpp \
  metadata/metadata.cpp \
  metadata/metadata_reader.cpp \
  camera_config.cpp \
  request_tracker.cpp \
  static_properties.cpp \
  stream_format.cpp \
  v4l2_camera.cpp \
  v4l2_camera_hal.cpp \
  v4l2_gralloc.cpp \
  v4l2_metadata_factory.cpp \
  v4l2_stream.cpp \
  v4l2_wrapper.cpp \
  stream_manager.cpp \
  camera_stream.cpp \

v4l2_test_files := \
  format_metadata_factory_test.cpp \
  metadata/control_test.cpp \
  metadata/default_option_delegate_test.cpp \
  metadata/enum_converter_test.cpp \
  metadata/ignored_control_delegate_test.cpp \
  metadata/map_converter_test.cpp \
  metadata/menu_control_options_test.cpp \
  metadata/metadata_reader_test.cpp \
  metadata/metadata_test.cpp \
  metadata/no_effect_control_delegate_test.cpp \
  metadata/partial_metadata_factory_test.cpp \
  metadata/property_test.cpp \
  metadata/ranged_converter_test.cpp \
  metadata/slider_control_options_test.cpp \
  metadata/state_test.cpp \
  metadata/tagged_control_delegate_test.cpp \
  metadata/tagged_control_options_test.cpp \
  metadata/v4l2_control_delegate_test.cpp \
  request_tracker_test.cpp \
  static_properties_test.cpp \

# Platform setting.
# ==============================================================================
# ---A63---
ifeq (uranus,$(TARGET_BOARD_PLATFORM))

v4l2_cflags += -D__A63__

# ---A50---
else ifeq (venus,$(TARGET_BOARD_PLATFORM))

v4l2_cflags += -D__A50__
v4l2_cflags += -DUSE_IOMMU_DRIVER
v4l2_c_includes += \
  $(v4l2_local_path)/allwinnertech/libAWIspApi

v4l2_shared_libs += \
  libAWIspApi

include $(call all-makefiles-under,$(v4l2_local_path))

# ---T3---
else ifeq (t3,$(TARGET_BOARD_PLATFORM))

v4l2_cflags += -D__T3__
v4l2_cflags += -DUSE_IOMMU_DRIVER
v4l2_c_includes += \
  $(v4l2_local_path)/allwinnertech/libAWIspApi

v4l2_shared_libs += \
  libAWIspApi

include $(call all-makefiles-under,$(v4l2_local_path))

# ---universal implement---
else

$(warning Use universal implement for camera hal 3.4, please confirm!)

v4l2_cflags += -D__UNIVERSAL__
v4l2_cflags += -DUSE_IOMMU_DRIVER
v4l2_c_includes += \
  $(v4l2_local_path)/allwinnertech/libAWIspApi

v4l2_shared_libs += \
  libAWIspApi

include $(call all-makefiles-under,$(v4l2_local_path))

endif

# V4L2 Camera HAL.
# ==============================================================================
include $(CLEAR_VARS)

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_PATH := $(v4l2_local_path)
LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
#$(warning $(LOCAL_MODULE))

LOCAL_MODULE_RELATIVE_PATH := hw
# Android O use vendor/lib/hw dir for modules
LOCAL_PROPRIETARY_MODULE := true
LOCAL_CFLAGS += $(v4l2_cflags)
LOCAL_SHARED_LIBRARIES := $(v4l2_shared_libs)
LOCAL_STATIC_LIBRARIES := \
  libgtest_prod \
  $(v4l2_static_libs) \

ifneq ($(wildcard hardware/aw/gpu/include/hal_public.h),)
LOCAL_C_INCLUDES += hardware/aw/gpu/include
LOCAL_CFLAGS += -DHAL_PUBLIC_UNIFIED_ENABLE
endif

LOCAL_C_INCLUDES += $(v4l2_c_includes)
LOCAL_SRC_FILES := $(v4l2_src_files)

include $(BUILD_SHARED_LIBRARY)

# Unit tests for V4L2 Camera HAL.
# ==============================================================================
ifeq ($(USE_CAMERA_HAL_V4L2_TEST), true)
include $(CLEAR_VARS)
LOCAL_PATH := $(v4l2_local_path)
LOCAL_MODULE := camera.v4l2_test
LOCAL_CFLAGS += $(v4l2_cflags)
LOCAL_SHARED_LIBRARIES := $(v4l2_shared_libs)
LOCAL_STATIC_LIBRARIES := \
  libBionicGtestMain \
  libgmock \
  $(v4l2_static_libs) \

LOCAL_C_INCLUDES += $(v4l2_c_includes)
LOCAL_SRC_FILES := \
  $(v4l2_src_files) \
  $(v4l2_test_files) \

include $(BUILD_NATIVE_TEST)
endif #USE_CAMERA_HAL_V4L2_TEST

endif # USE_CAMERA_V4L2_HAL
