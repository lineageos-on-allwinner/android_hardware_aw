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


#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bootloader_message/bootloader_message.h>
#include <cutils/properties.h>

#include "bootinfo.h"


int boot_info_open_partition(const char *name, uint64_t *out_size, int flags)
{
  const char* dev = "/dev/block/by-name/";
  char *path;
  int fd;

  size_t trimmed_len, name_len;
  trimmed_len = strlen(dev);
  name_len = strlen(name);
  path = static_cast<char *>(calloc(trimmed_len + name_len + 1, 1));
  strncpy(path, dev, trimmed_len);
  strncpy(path + trimmed_len, name, name_len);
  fd = open(path, flags);
  free(path);

  // If we successfully opened the device, get size if requested.
  if (fd != -1 && out_size != NULL) {
    if (ioctl(fd, BLKGETSIZE64, out_size) != 0) {
      close(fd);
      return -1;
    }
  }

  return fd;
}

// As per struct bootloader_message_ab which is defined in
// bootable/recovery/bootloader.h we can use the 32 bytes in the
// bootctrl_suffix field provided that they start with the active slot
// suffix terminated by NUL. It just so happens that BrilloBootInfo is
// laid out this way.
#define BOOTINFO_OFFSET offsetof(struct bootloader_message_ab, slot_suffix)

bool boot_info_load(BrilloBootInfo *out_info)
{
  int fd;

  memset(out_info, '\0', sizeof(BrilloBootInfo));

  fd = boot_info_open_partition("misc", NULL, O_RDONLY);
  if (fd == -1)
    return false;
  if (lseek(fd, BOOTINFO_OFFSET, SEEK_SET) != BOOTINFO_OFFSET) {
    close(fd);
    return false;
  }
  ssize_t num_read;
  do {
    num_read = read(fd, (void*) out_info, sizeof(BrilloBootInfo));
  } while (num_read == -1 && errno == EINTR);
  close(fd);
  if (num_read != sizeof(BrilloBootInfo))
    return false;
  return true;
}

bool boot_info_save(BrilloBootInfo *info)
{
  int fd;

  fd = boot_info_open_partition("misc", NULL, O_RDWR);
  if (fd == -1)
    return false;
  if (lseek(fd, BOOTINFO_OFFSET, SEEK_SET) != BOOTINFO_OFFSET) {
    close(fd);
    return false;
  }
  ssize_t num_written;
  do {
    num_written = write(fd, (void*) info, sizeof(BrilloBootInfo));
  } while (num_written == -1 && errno == EINTR);
  close(fd);
  if (num_written != sizeof(BrilloBootInfo))
    return false;
  return true;
}

bool boot_info_validate(BrilloBootInfo* info)
{
  if (info->magic[0] != 'B' ||
      info->magic[1] != 'C' ||
      info->magic[2] != 'c')
    return false;
  if (info->active_slot >= 2)
    return false;
  return true;
}

void boot_info_reset(BrilloBootInfo* info)
{
  memset(info, '\0', sizeof(BrilloBootInfo));
  info->magic[0] = 'B';
  info->magic[1] = 'C';
  info->magic[2] = 'c';
}
