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

FW_BASE_PATH := hardware/aw/wireless/partner/ampak/firmware

# Find all FW_BIN_FILES
SEARCH_PATH := $(shell find $(FW_BASE_PATH)/ap6* -name "*.hcd" | sed 's/\/[^/]\+\/*$$//g')
$(foreach p,$(SEARCH_PATH), $(eval FW_HCD_FILES += $(call find-copy-subdir-files,"*.hcd",$(p),$(TARGET_COPY_OUT_VENDOR)/etc/firmware)))

PRODUCT_COPY_FILES += $(FW_HCD_FILES)

