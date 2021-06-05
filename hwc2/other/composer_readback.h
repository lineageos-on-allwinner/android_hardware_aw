/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef _COMPOSER_READ_BACK_H_
#define _COMPOSER_READ_BACK_H_

#include <hardware/hwcomposer2.h>
#include "hwc.h"

#define DEFAULT_READBACK_PIXEL_FORMAT HAL_PIXEL_FORMAT_RGBA_8888
#define DEFAULT_READBACK_DATASPACE    HAL_DATASPACE_UNKNOWN

int initReadback(void);
void setReadbackBuffer(hwc2_display_t display,
        buffer_handle_t buffer, int32_t releaseFence);
int32_t getReadbackBufferFence(hwc2_display_t display, int32_t* fence);

int doReadback(Display_t* hwdevice, struct sync_info *sync);
void resetReadback(void);

#endif

