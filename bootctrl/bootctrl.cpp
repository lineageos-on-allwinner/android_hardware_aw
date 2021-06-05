/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "bootctrl"
//#define LOG_NDEBUG 0

#include <log/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/boot_control.h>

#include "bootinfo.h"

static unsigned number_slots = 2;

void module_init(boot_control_module_t *module)
{
}

unsigned module_getNumberSlots(boot_control_module_t *module)
{
  return number_slots;
}

static bool get_dev_t_for_partition(const char *name, dev_t *out_device)
{
  int fd;
  struct stat statbuf;

  fd = boot_info_open_partition(name, NULL, O_RDONLY);
  if (fd == -1)
    return false;
  if (fstat(fd, &statbuf) != 0) {
    ALOGE("WARNING: Error getting information about part %s: %s\n",
            name, strerror(errno));
    close(fd);
    return false;
  }
  close(fd);
  *out_device = statbuf.st_rdev;
  return true;
}

unsigned module_getCurrentSlot(boot_control_module_t *module)
{
  struct stat statbuf;
  dev_t system_a_dev, system_b_dev;

  if (stat("/system", &statbuf) != 0) {
    ALOGE("WARNING: Error getting information about /system: %s\n",
            strerror(errno));
    return 0;
  }

  if (!get_dev_t_for_partition("system_a", &system_a_dev) ||
      !get_dev_t_for_partition("system_b", &system_b_dev))
    return 0;

  if (statbuf.st_dev == system_a_dev) {
    return 0;
  } else if (statbuf.st_dev == system_b_dev) {
    return 1;
  } else {
    ALOGE("WARNING: Error determining current slot "
            "(/system dev_t of %d:%d does not match a=%d:%d or b=%d:%d)\n",
            major(statbuf.st_dev), minor(statbuf.st_dev),
            major(system_a_dev), minor(system_a_dev),
            major(system_b_dev), minor(system_b_dev));
    return 0;
  }
}
int module_markBootSuccessful(boot_control_module_t *module)
{
  BrilloBootInfo info;
  if (!boot_info_load(&info)) {
    ALOGE("WARNING: Error loading boot-info. Resetting.\n");
    boot_info_reset(&info);
  } else {
    if (!boot_info_validate(&info)) {
      ALOGE("WARNING: boot-info is invalid. Resetting.\n");
      boot_info_reset(&info);
    }
  }
  unsigned i= module_getCurrentSlot(module);
  
  info.slot_info[i].successful_boot= true;
  if (!boot_info_save(&info)) {
    ALOGE("Error saving boot-info.\n");
    return -errno;
  }

  return 0;
}

int module_setActiveBootSlot(boot_control_module_t *module, unsigned slot)
{
  BrilloBootInfo info;

  if (slot >= number_slots)
    return -EINVAL;

  if (!boot_info_load(&info)) {
    ALOGE("WARNING: Error loading boot-info. Resetting.\n");
    boot_info_reset(&info);
  } else {
    if (!boot_info_validate(&info)) {
      ALOGE("WARNING: boot-info is invalid. Resetting.\n");
      boot_info_reset(&info);
    }
  }

  info.active_slot = slot;
  info.slot_info[slot].bootable = true;
  snprintf(info.bootctrl_suffix,
           sizeof(info.bootctrl_suffix),
           "_%c", slot + 'a');

  if (!boot_info_save(&info)) {
    ALOGE("Error saving boot-info.\n");
    return -errno;
  }

  return 0;
}

int module_setSlotAsUnbootable(struct boot_control_module *module, unsigned slot)
{
  BrilloBootInfo info;

  if (slot >= number_slots)
    return -EINVAL;

  if (!boot_info_load(&info)) {
    ALOGE("WARNING: Error loading boot-info. Resetting.\n");
    boot_info_reset(&info);
  } else {
    if (!boot_info_validate(&info)) {
      ALOGE("WARNING: boot-info is invalid. Resetting.\n");
      boot_info_reset(&info);
    }
  }

  info.slot_info[slot].bootable = false;

  if (!boot_info_save(&info)) {
    ALOGE("Error saving boot-info.\n");
    return -errno;
  }

  return 0;
}

int module_isSlotBootable(struct boot_control_module *module, unsigned slot)
{
  BrilloBootInfo info;

  if (slot >= number_slots)
    return -EINVAL;

  if (!boot_info_load(&info)) {
    ALOGE("WARNING: Error loading boot-info. Resetting.\n");
    boot_info_reset(&info);
  } else {
    if (!boot_info_validate(&info)) {
      ALOGE("WARNING: boot-info is invalid. Resetting.\n");
      boot_info_reset(&info);
    }
  }

  return info.slot_info[slot].bootable;
}

int module_isSlotMarkedSuccessful(struct boot_control_module *module, unsigned slot)
{
  BrilloBootInfo info;

  if (slot >= number_slots)
    return -EINVAL;

  if (!boot_info_load(&info)) {
    ALOGE("WARNING: Error loading boot-info. Resetting.\n");
    boot_info_reset(&info);
  } else {
    if (!boot_info_validate(&info)) {
      ALOGE("WARNING: boot-info is invalid. Resetting.\n");
      boot_info_reset(&info);
    }
  }

  return info.slot_info[slot].successful_boot;
}

const char* module_getSuffix(boot_control_module_t *module, unsigned slot)
{
  static const char* suffix[2] = {"_a", "_b"};
  if (slot >= number_slots)
    return NULL;
  return suffix[slot];
}

static struct hw_module_methods_t module_methods = {
  .open  = NULL,
};


/* This boot_control HAL implementation emulates A/B by copying the
 * contents of the boot partition of the requested slot to the boot
 * partition. It hence works with bootloaders that are not yet aware
 * of A/B. This code is only intended to be used for development.
 */

boot_control_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag                 = HARDWARE_MODULE_TAG,
    .module_api_version  = BOOT_CONTROL_MODULE_API_VERSION_0_1,
    .hal_api_version     = HARDWARE_HAL_API_VERSION,
    .id                  = BOOT_CONTROL_HARDWARE_MODULE_ID,
    .name                = "Boot_control HAL",
    .author              = "Allwinner",
    .methods             = &module_methods,
  },
  .init                 = module_init,
  .getNumberSlots       = module_getNumberSlots,
  .getCurrentSlot       = module_getCurrentSlot,
  .markBootSuccessful   = module_markBootSuccessful,
  .setActiveBootSlot    = module_setActiveBootSlot,
  .setSlotAsUnbootable  = module_setSlotAsUnbootable,
  .isSlotBootable       = module_isSlotBootable,
  .isSlotMarkedSuccessful       = module_isSlotMarkedSuccessful,
  .getSuffix            = module_getSuffix,
};
