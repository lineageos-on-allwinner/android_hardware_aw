
// Modified from hardware/libhardware/modules/camera/CameraHAL.cpp
#undef NDEBUG

#define LOG_TAG "CameraHALv3_4"

#include "v4l2_camera_hal.h"
//#define LOG_TAG "CameraHALv3_V4L2Camera"


#include <utils/Log.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include <android-base/parseint.h>

#include "common.h"
#include "v4l2_camera.h"
#include "camera_config.h"

/*
 * This file serves as the entry point to the HAL. It is modified from the
 * example default HAL available in hardware/libhardware/modules/camera.
 * It contains the module structure and functions used by the framework
 * to load and interface to this HAL, as well as the handles to the individual
 * camera devices.
 */

namespace v4l2_camera_hal {

// Default global camera hal.
static V4L2CameraHAL gCameraHAL;

V4L2CameraHAL::V4L2CameraHAL() : mCameras(), mCallbacks(NULL) {
  HAL_LOG_ENTER();

  // Find /dev/video* nodes.
  // Fix stream is not manager now.
  int ret;
  std::string node;
#if 0
  for (int i = 0; i < MAX_NUM_OF_STREAMS; i++)
  {
    char dev_node[16];
    sprintf(dev_node, "/dev/video%d", i);
    ret = access(dev_node, F_OK);
    if(ret == 0) {
      nodes.push(dev_node);
      HAL_LOGV("Found video node %s.", dev_node);
    }
    // Open the node.
    fd = TEMP_FAILURE_RETRY(open(dev_node, O_RDWR));
    if (fd < 0) {
      HAL_LOGE("failed to open %s (%s).", dev_node, strerror(errno));
      continue;
    }

    v4l2_capability cap;
    // Read V4L2 capabilities.
    if (TEMP_FAILURE_RETRY(ioctl(fd, VIDIOC_QUERYCAP, &cap)) != 0) {
      HAL_LOGE(
          "VIDIOC_QUERYCAP on %s fail: %s.", dev_node, strerror(errno));
    } else if (!(cap.capabilities & V4L2_CAPTURE_TYPE)) {
      HAL_LOGE("%s is not a V4L2 video capture device.", dev_node);
    }
    // TODOzjw: setting ccameraconfig for open front&back sensor .
    struct v4l2_input inp;
    inp.index = id;
    if (TEMP_FAILURE_RETRY(ioctl(fd, VIDIOC_S_INPUT, &inp)) != 0) {
      HAL_LOGE(
        "VIDIOC_S_INPUT on %s error: %s.", inp.index, strerror(errno));
    }

    TEMP_FAILURE_RETRY(close(fd));
  }
#endif

  // Test each for V4L2 support and uniqueness.
  int fd;
  int id = 0;
  int i = 0;
  char dev_node[16];
  sprintf(dev_node, "/dev/video%d", i);
  ret = access(dev_node, F_OK);
  if(ret == 0) {
    HAL_LOGV("Found video node %s.", dev_node);
  }

  // id == 0 camera facing back;
  // id == 1 camera facing front;
  for (id = 0; id <MAX_NUM_OF_CAMERAS; id ++) {

    // camera config information
    mCameraConfig[id] = new CCameraConfig(id);
    if(mCameraConfig[id] == 0)
    {
        HAL_LOGW("create CCameraConfig failed");
    }
    else
    {
        mCameraConfig[id]->initParameters();
        mCameraConfig[id]->dumpParameters();
    }

    std::unique_ptr<V4L2Camera> cam(V4L2Camera::NewV4L2Camera(id, dev_node, mCameraConfig[id]));
    if (cam) {
      mCameras.push_back(std::move(cam));
    } else {
      HAL_LOGE("Failed to initialize camera at %s.", dev_node);
    }
  }

}

V4L2CameraHAL::~V4L2CameraHAL() {
  HAL_LOG_ENTER();
  for (int n = 0; n < MAX_NUM_OF_CAMERAS; n++)
  {
      if (mCameraConfig[n] != NULL)
      {
          delete mCameraConfig[n];
          mCameraConfig[n] = NULL;
      }
  }
}

int V4L2CameraHAL::getNumberOfCameras() {
  HAL_LOGV("returns %d", mCameras.size());
  return mCameras.size();
}

int V4L2CameraHAL::getCameraInfo(int id, camera_info_t* info) {
  HAL_LOG_ENTER();
  if (id < 0 || id >= mCameras.size()) {
    return -EINVAL;
  }
  // TODO(b/29185945): Hotplugging: return -EINVAL if unplugged.
  return mCameras[id]->getInfo(info);
}

int V4L2CameraHAL::setCallbacks(const camera_module_callbacks_t* callbacks) {
  HAL_LOG_ENTER();
  mCallbacks = callbacks;
  return 0;
}

void V4L2CameraHAL::getVendorTagOps(vendor_tag_ops_t* ops) {
  HAL_LOG_ENTER();
  // No vendor ops for this HAL. From <hardware/camera_common.h>:
  // "leave ops unchanged if no vendor tags are defined."
}

int V4L2CameraHAL::openLegacy(const hw_module_t* module,
                              const char* id,
                              uint32_t halVersion,
                              hw_device_t** device) {
  HAL_LOG_ENTER();
  // Not supported.
  return -ENOSYS;
}

int V4L2CameraHAL::setTorchMode(const char* camera_id, bool enabled) {
  HAL_LOG_ENTER();
  // TODO(b/29158098): HAL is required to respond appropriately if
  // the desired camera actually does support flash.
  return -ENOSYS;
}

int V4L2CameraHAL::openDevice(const hw_module_t* module,
                              const char* name,
                              hw_device_t** device) {
  HAL_LOG_ENTER();

  if (module != &HAL_MODULE_INFO_SYM.common) {
    HAL_LOGE(
        "Invalid module %p expected %p", module, &HAL_MODULE_INFO_SYM.common);
    return -EINVAL;
  }

  int id;
  if (!android::base::ParseInt(name, &id, 0, getNumberOfCameras() - 1)) {
    return -EINVAL;
  }

  int id_tmp;
  for(id_tmp = 0; id_tmp < mCameras.size(); id_tmp++){
      if(mCameras[id_tmp]->isBusy()){
        return -EUSERS;
      }
  }

  // TODO(b/29185945): Hotplugging: return -EINVAL if unplugged.
  return mCameras[id]->openDevice(module, device);
}

/*
 * The framework calls the following wrappers, which in turn
 * call the corresponding methods of the global HAL object.
 */

static int get_number_of_cameras() {
  return gCameraHAL.getNumberOfCameras();
}

static int get_camera_info(int id, struct camera_info* info) {
  
  return gCameraHAL.getCameraInfo(id, info);
}

static int set_callbacks(const camera_module_callbacks_t* callbacks) {
  return gCameraHAL.setCallbacks(callbacks);
}

static void get_vendor_tag_ops(vendor_tag_ops_t* ops) {
  return gCameraHAL.getVendorTagOps(ops);
}

static int open_legacy(const hw_module_t* module,
                       const char* id,
                       uint32_t halVersion,
                       hw_device_t** device) {
  return gCameraHAL.openLegacy(module, id, halVersion, device);
}

static int set_torch_mode(const char* camera_id, bool enabled) {
  return gCameraHAL.setTorchMode(camera_id, enabled);
}

static int open_dev(const hw_module_t* module,
                    const char* name,
                    hw_device_t** device) {
  return gCameraHAL.openDevice(module, name, device);
}

}  // namespace v4l2_camera_hal

static hw_module_methods_t v4l2_module_methods = {
    .open = v4l2_camera_hal::open_dev};

camera_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = CAMERA_HARDWARE_MODULE_ID,
            .name = "V4L2 Camera HAL v3",
            .author = "ZJW",
            .methods = &v4l2_module_methods,
            .dso = nullptr,
            .reserved = {0},
        },
    .get_number_of_cameras = v4l2_camera_hal::get_number_of_cameras,
    .get_camera_info = v4l2_camera_hal::get_camera_info,
    .set_callbacks = v4l2_camera_hal::set_callbacks,
    .get_vendor_tag_ops = v4l2_camera_hal::get_vendor_tag_ops,
    .open_legacy = v4l2_camera_hal::open_legacy,
    .set_torch_mode = v4l2_camera_hal::set_torch_mode,
    .init = nullptr,
    .reserved = {nullptr, nullptr}};
