#
# Copyright (C) 2017-2019 Allwinner Technology Co., Ltd. All rights reserved.
#

GPU_ROOT := $(shell dirname $(lastword $(MAKEFILE_LIST)))

ifneq ($(filter $(TARGET_GPU_TYPE), mali400 mali450),)
GPU_ARCH := mali-utgard
else ifneq ($(filter $(TARGET_GPU_TYPE), mali-t720 mali-t760),)
GPU_ARCH := mali-midgard
else ifneq ($(filter $(TARGET_GPU_TYPE), sgx544),)
GPU_ARCH := img-sgx
else ifneq ($(filter $(TARGET_GPU_TYPE), mali-g31),)
GPU_ARCH := mali-bifrost
else
$(error TARGET_GPU_TYPE($(TARGET_GPU_TYPE)) is invalid!)
endif

GPU_COPY_ROOT_DIR := $(GPU_ROOT)/$(GPU_ARCH)/$(TARGET_GPU_TYPE)/$(TARGET_ARCH)

ifeq ($(wildcard $(GPU_COPY_ROOT_DIR)/lib/*.so),)
$(error There is no libraries in $(GPU_COPY_ROOT_DIR)!)
endif

ifneq ($(filter $(GPU_ARCH), mali-),)

PRODUCT_PACKAGES += \
	gralloc.$(TARGET_BOARD_PLATFORM)

PRODUCT_COPY_FILES += \
	$(GPU_COPY_ROOT_DIR)/lib/libGLES_mali.so:vendor/lib/egl/libGLES_mali.so
ifneq ($(filter $(TARGET_ARCH), arm64),)
PRODUCT_COPY_FILES += \
	$(GPU_COPY_ROOT_DIR)/lib64/libGLES_mali.so:vendor/lib64/egl/libGLES_mali.so
endif

else ifneq ($(filter $(GPU_ARCH), img-sgx),)

ifneq ($(wildcard $(GPU_ROOT)/$(GPU_ARCH)/egl.cfg),)
PRODUCT_COPY_FILES += $(GPU_ROOT)/$(GPU_ARCH)/egl.cfg:system/lib/egl/egl.cfg
endif

ifneq ($(wildcard $(GPU_COPY_ROOT_DIR)/powervr.ini),)
PRODUCT_COPY_FILES += $(GPU_COPY_ROOT_DIR)/powervr.ini:etc/powervr.ini
endif

PRODUCT_COPY_FILES += \
	$(GPU_COPY_ROOT_DIR)/lib/libusc.so:vendor/lib/libusc.so \
	$(GPU_COPY_ROOT_DIR)/lib/libglslcompiler.so:vendor/lib/libglslcompiler.so \
	$(GPU_COPY_ROOT_DIR)/lib/libIMGegl.so:vendor/lib/libIMGegl.so \
	$(GPU_COPY_ROOT_DIR)/lib/libpvr2d.so:vendor/lib/libpvr2d.so \
	$(GPU_COPY_ROOT_DIR)/lib/libpvrANDROID_WSEGL.so:vendor/lib/libpvrANDROID_WSEGL.so \
	$(GPU_COPY_ROOT_DIR)/lib/libPVRScopeServices.so:vendor/lib/libPVRScopeServices.so \
	$(GPU_COPY_ROOT_DIR)/lib/libsrv_init.so:vendor/lib/libsrv_init.so \
	$(GPU_COPY_ROOT_DIR)/lib/libsrv_um.so:vendor/lib/libsrv_um.so \
	$(GPU_COPY_ROOT_DIR)/lib/libEGL_POWERVR_SGX544_115.so:vendor/lib/egl/libEGL_POWERVR_SGX544_115.so \
	$(GPU_COPY_ROOT_DIR)/lib/libGLESv1_CM_POWERVR_SGX544_115.so:vendor/lib/egl/libGLESv1_CM_POWERVR_SGX544_115.so \
	$(GPU_COPY_ROOT_DIR)/lib/libGLESv2_POWERVR_SGX544_115.so:vendor/lib/egl/libGLESv2_POWERVR_SGX544_115.so \
	$(GPU_COPY_ROOT_DIR)/lib/gralloc.sunxi.so:vendor/lib/hw/gralloc.$(TARGET_BOARD_PLATFORM).so \
	$(GPU_COPY_ROOT_DIR)/bin/pvrsrvctl:vendor/bin/pvrsrvctl

endif

