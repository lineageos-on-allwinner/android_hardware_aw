#
# Copyright (C) 2017-2019 Allwinner Technology Co., Ltd. All rights reserved.
#

ifneq ($(filter $(TARGET_GPU_TYPE), mali400 mali450),)
GPU_ARCH := mali-utgard
else ifneq ($(filter $(TARGET_GPU_TYPE), mali-t720 mali-t760),)
GPU_ARCH := mali-midgard
else ifneq ($(filter $(TARGET_GPU_TYPE), mali-g31),)
GPU_ARCH := mali-bifrost
else ifneq ($(filter $(TARGET_GPU_TYPE), sgx544),)
GPU_ARCH := img-sgx
else
$(error TARGET_GPU_TYPE($(TARGET_GPU_TYPE)) is invalid!)
endif

include $(call all-named-subdir-makefiles, $(GPU_ARCH))
