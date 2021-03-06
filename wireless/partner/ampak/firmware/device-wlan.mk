#
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
#

-include hardware/broadcom/wlan/bcmdhd/config/config-bcm.mk

FW_BASE_PATH := hardware/aw/wireless/partner/ampak/firmware/$(BOARD_USR_WIFI)

PRODUCT_COPY_FILES += $(call find-copy-subdir-files,"*.bin",$(FW_BASE_PATH),$(TARGET_COPY_OUT_VENDOR)/etc/firmware)
PRODUCT_COPY_FILES += $(FW_BASE_PATH)/nvram_$(BOARD_USR_WIFI).txt:$(TARGET_COPY_OUT_VENDOR)/etc/firmware/nvram.txt
PRODUCT_COPY_FILES += $(call find-copy-subdir-files,"config.txt",$(FW_BASE_PATH),$(TARGET_COPY_OUT_VENDOR)/etc/firmware)
