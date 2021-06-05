


#if DBG_V4L2_STREAM

#endif
#define LOG_TAG "CameraHALv3_V4L2Stream"
#undef NDEBUG

#include <android/log.h>


#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <string.h>

//#include <linux/videodev2.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
//#include <stdio.h>

#include <utils/Timers.h>
#include <cutils/properties.h>



#include <android-base/unique_fd.h>

#include "common.h"
#include "v4l2_stream.h"// Helpers of logging (showing function name and line number).
#include "camera_config.h"

// For making difference between main stream and sub stream.
#define HAL_LOGD(fmt, args...) if(LOG_LEVEL > 2) \
    ALOGD("%s:%d:%d: " fmt, __func__, __LINE__, device_ss_, ##args)
#define HAL_LOGV(fmt, args...) if(LOG_LEVEL > 3) \
    ALOGV("%s:%d:%d: " fmt, __func__, __LINE__, device_ss_,  ##args)

#include "stream_format.h"
#include "v4l2_gralloc.h"
#include "linux/videodev2.h"
#include "type_camera.h"
#include <hal_public.h> //GPU dependencies


extern "C" int AWJpecEnc(JpegEncInfo* pJpegInfo, EXIFInfo* pExifInfo, void* pOutBuffer, int* pOutBufferSize);

namespace v4l2_camera_hal {



const int32_t kStandardSizes[][2] = {{1920, 1080}, {640, 480}};

V4L2Stream* V4L2Stream::NewV4L2Stream(const int id, const std::string device_path, CCameraConfig* pCameraCfg) {
  return new V4L2Stream(id, device_path, pCameraCfg);
}

V4L2Stream::V4L2Stream(const int id,
                              const std::string device_path, CCameraConfig* pCameraCfg)
    : device_id_(id),
      device_path_(std::move(device_path)),
      mCameraConfig(pCameraCfg),
      has_StreamOn(false),
      buffer_state_(BUFFER_UNINIT),
      isTakePicure(false),
      mflush_buffers(false),
#ifdef USE_ISP
      mAWIspApi(NULL),
      mIspId(-1),
#endif
      connection_count_(0),
      device_fd_(-1) {
  HAL_LOG_ENTER();
  if(device_path_.compare(MAIN_STREAM_PATH) == 0) {
    device_ss_ = MAIN_STREAM;
  } else if(device_path_.compare(SUB_0_STREAM_PATH) == 0) {
    device_ss_ = SUB_0_STREAM;
  }

}

V4L2Stream::~V4L2Stream() {
  HAL_LOG_ENTER();
  std::unique_lock<std::mutex> lock(buffer_queue_lock_);
  HAL_LOGD("~V4L2Stream %s, device_ss_:%d.", device_path_.c_str(), device_ss_);
}

int V4L2Stream::Connect() {
  HAL_LOG_ENTER();
  std::lock_guard<std::mutex> lock(connection_lock_);

  if (connected()) {
    HAL_LOGV("Camera stream %s is already connected.", device_path_.c_str());
    ++connection_count_;
    return 0;
  }
  HAL_LOGD("Camera stream will link to %s.", device_path_.c_str());
  int try_num = 5;
  int fd = -1;
  while(try_num--) {
    HAL_LOGD("try to link %s, the %d time.", device_path_.c_str(), 5 -try_num);   
    // Open in nonblocking mode (DQBUF may return EAGAIN).
    fd = TEMP_FAILURE_RETRY(open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0));
    if (fd < 0) {
      HAL_LOGE("failed to open %s (%s)", device_path_.c_str(), strerror(errno));
      //return -ENODEV;
      usleep(200*1000);
      continue;
    }
    break;
  }
  if (fd < 0) {
    HAL_LOGE("failed to open %s (%s)", device_path_.c_str(), strerror(errno));
    return -ENODEV;
  }

  //device_fd_.reset(fd);
  device_fd_ = fd;
  ++connection_count_;

  HAL_LOGV("Detect camera stream %s, stream serial:%d.", device_path_.c_str(), device_ss_);

  // TODOzjw: setting ccameraconfig for open front&back sensor .
  struct v4l2_input inp;
  inp.index = device_id_;
   if (TEMP_FAILURE_RETRY(ioctl(fd, VIDIOC_S_INPUT, &inp)) != 0) {
     HAL_LOGE(
         "VIDIOC_S_INPUT on %s error: %s.", inp.index, strerror(errno));
   }

#ifdef USE_ISP
   mAWIspApi = new android::AWIspApi();
#endif

  return 0;
}

void V4L2Stream::Disconnect() {
  HAL_LOG_ENTER();
  std::lock_guard<std::mutex> lock(connection_lock_);

  if (connection_count_ == 0) {
    // Not connected.
    HAL_LOGE("Camera device %s is not connected, cannot disconnect.",
             device_path_.c_str());
    return;
  }

  --connection_count_;
  if (connection_count_ > 0) {
    HAL_LOGV("Disconnected from camera device %s. %d connections remain.",
             device_path_.c_str());
    return;
  }
  int res = TEMP_FAILURE_RETRY(close(device_fd_));
  HAL_LOGV("Close device path:%s, fd:%d, res: %s", device_path_.c_str(), device_fd_, strerror(res));
  if (res) {
    HAL_LOGW("Disconnected from camera device %s. fd:%d encount err(%s).",device_path_.c_str(), device_fd_, strerror(res));
  }
  // Delay for open after close success encount open device busy.
  //TODO: optimize this, keep node open until close the camera hal.
  usleep(200*1000);

#ifdef USE_ISP
  if (mAWIspApi != NULL) {
      delete mAWIspApi;
      mAWIspApi = NULL;
  }
#endif

  //device_fd_.reset(-1);  // Includes close().
  device_fd_ = -1;
  format_.reset();
  buffers_.clear();
  // Closing the device releases all queued buffers back to the user.
}

// Helper function. Should be used instead of ioctl throughout this class.
template <typename T>
int V4L2Stream::IoctlLocked(int request, T data) {
  // Potentially called so many times logging entry is a bad idea.
  std::lock_guard<std::mutex> lock(device_lock_);

  if (!connected()) {
    HAL_LOGE("Stream %s not connected.", device_path_.c_str());
    return -ENODEV;
  }
  HAL_LOGV("Stream fd:%d..", device_fd_);
  return TEMP_FAILURE_RETRY(ioctl(device_fd_, request, data));
}

int V4L2Stream::StreamOn() {
  HAL_LOG_ENTER();

  if (!format_) {
    HAL_LOGE("Stream format must be set before turning on stream.");
    return -EINVAL;
  }

  if (has_StreamOn) {
    HAL_LOGV("Stream had been turned on.");
    return 0;
  }
#if DELAY_BETWEEN_ON_OFF
  mTimeStampsFstreamon = systemTime() / 1000000;
#endif

  int32_t type = format_->type();
  if (IoctlLocked(VIDIOC_STREAMON, &type) < 0) {
    HAL_LOGE("STREAMON fails: %s", strerror(errno));
    return -ENODEV;
  }else {
    buffer_state_ = BUFFER_UNINIT;
    has_StreamOn = true;
  }
#if DELAY_BETWEEN_ON_OFF
  HAL_LOGV("Stream turned on.");
  usleep(100*1000);
  HAL_LOGV("Stream after turned on sleep for stream on prepare.");
#endif


#ifdef USE_ISP
  mIspId = 0;
  int mDevice_id = device_id_;
  mIspId = mAWIspApi->awIspGetIspId(mDevice_id);
  if (mIspId >= 0) {
      mAWIspApi->awIspStart(mIspId);
      HAL_LOGD("ISP turned on.");
  } else {
    HAL_LOGE("ISP turned on failed!");
  }
#endif

  return 0;
}

int V4L2Stream::StreamOff() {
  HAL_LOG_ENTER();

  if (!format_) {
    // Can't have turned on the stream without format being set,
    // so nothing to turn off here.
    return 0;
  }
#if DELAY_BETWEEN_ON_OFF 
  // TODO: Remove it.
  // Delay between vin stream on and off time that less than DELAY_BETWEEN_STREAM
  // for resource release completely.
  unsigned long  mDeltaStream = systemTime() / 1000000 - mTimeStampsFstreamon;
  HAL_LOGD("mDeltaStream:%ld, mTimeStampsFstreamon:%ld, systemTime() / 1000000:%ld.", mDeltaStream, mTimeStampsFstreamon, systemTime() / 1000000);
  if(mDeltaStream < DELAY_BETWEEN_STREAM) {
    HAL_LOGD("mDeltaStream:%ld.", mDeltaStream);
    usleep((DELAY_BETWEEN_STREAM -mDeltaStream)*1000);
  }
#endif
  int32_t type = format_->type();
  int res = IoctlLocked(VIDIOC_STREAMOFF, &type);
  if (res) {
    HAL_LOGW("Stream turned off failed, err(%s).",strerror(res));
    //return res;
  }
  if (res < 0) {
    HAL_LOGE("STREAMOFF fails: %s", strerror(errno));
    //return -ENODEV;
  }
  HAL_LOGD("After stream %d, ind:%d turned off.", device_id_, device_fd_);
#ifdef USE_ISP
  mAWIspApi->awIspStop(mIspId);
  HAL_LOGV("Stream %d, ind:%d awIspStop.", device_id_, device_fd_);
#endif


  // Calling STREAMOFF releases all queued buffers back to the user.
  //int gralloc_res = gralloc_->unlockAllBuffers();
  // No buffers in flight.
  for (size_t i = 0; i < buffers_.size(); ++i) {
    buffers_[i] = false;
  }
  // munmap buffer.
  for (int i = 0; i < buffers_.size(); i++)
  {

    HAL_LOGV("munmap index:%d!", i);
    res = munmap(mMapMem.mem[i], mMapMem.length);
    if (res < 0) {
        HAL_LOGE("munmap failed");
    }
    mMapMem.mem[i] = NULL;
  }

  has_StreamOn = false;
  HAL_LOGV("Stream %d, ind:%d turned off.", device_id_, device_fd_);
  return 0;
}

int V4L2Stream::flush() {
  HAL_LOG_ENTER();
  mflush_buffers = true;
  buffer_availabl_queue_.notify_one();
  HAL_LOGV("Stream %d, ss:%d, ind:%d flush.", device_id_, device_ss_, device_fd_);
  return 0;
}

int V4L2Stream::QueryControl(uint32_t control_id,
                              v4l2_query_ext_ctrl* result) {
  int res;

  memset(result, 0, sizeof(*result));

  if (extended_query_supported_) {
    result->id = control_id;
    res = IoctlLocked(VIDIOC_QUERY_EXT_CTRL, result);
    // Assuming the operation was supported (not ENOTTY), no more to do.
    if (errno != ENOTTY) {
      if (res) {
        HAL_LOGE("QUERY_EXT_CTRL fails: %s", strerror(errno));
        return -ENODEV;
      }
      return 0;
    }
  }

  // Extended control querying not supported, fall back to basic control query.
  v4l2_queryctrl query;
  query.id = control_id;
  if (IoctlLocked(VIDIOC_QUERYCTRL, &query)) {
    HAL_LOGE("QUERYCTRL fails: %s", strerror(errno));
    return -ENODEV;
  }

  // Convert the basic result to the extended result.
  result->id = query.id;
  result->type = query.type;
  memcpy(result->name, query.name, sizeof(query.name));
  result->minimum = query.minimum;
  if (query.type == V4L2_CTRL_TYPE_BITMASK) {
    // According to the V4L2 documentation, when type is BITMASK,
    // max and default should be interpreted as __u32. Practically,
    // this means the conversion from 32 bit to 64 will pad with 0s not 1s.
    result->maximum = static_cast<uint32_t>(query.maximum);
    result->default_value = static_cast<uint32_t>(query.default_value);
  } else {
    result->maximum = query.maximum;
    result->default_value = query.default_value;
  }
  result->step = static_cast<uint32_t>(query.step);
  result->flags = query.flags;
  result->elems = 1;
  switch (result->type) {
    case V4L2_CTRL_TYPE_INTEGER64:
      result->elem_size = sizeof(int64_t);
      break;
    case V4L2_CTRL_TYPE_STRING:
      result->elem_size = result->maximum + 1;
      break;
    default:
      result->elem_size = sizeof(int32_t);
      break;
  }

  return 0;
}

int V4L2Stream::GetControl(uint32_t control_id, int32_t* value) {
  // For extended controls (any control class other than "user"),
  // G_EXT_CTRL must be used instead of G_CTRL.
  if (V4L2_CTRL_ID2CLASS(control_id) != V4L2_CTRL_CLASS_USER) {
    v4l2_ext_control control;
    v4l2_ext_controls controls;
    memset(&control, 0, sizeof(control));
    memset(&controls, 0, sizeof(controls));

    control.id = control_id;
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control_id);
    controls.count = 1;
    controls.controls = &control;

    if (IoctlLocked(VIDIOC_G_EXT_CTRLS, &controls) < 0) {
      HAL_LOGE("G_EXT_CTRLS fails: %s", strerror(errno));
      return -ENODEV;
    }
    *value = control.value;
  } else {
    v4l2_control control{control_id, 0};
    if (IoctlLocked(VIDIOC_G_CTRL, &control) < 0) {
      HAL_LOGE("G_CTRL fails: %s", strerror(errno));
      return -ENODEV;
    }
    *value = control.value;
  }
  return 0;
}

int V4L2Stream::SetControl(uint32_t control_id,
                            int32_t desired,
                            int32_t* result) {
  int32_t result_value = 0;

  // TODO(b/29334616): When async, this may need to check if the stream
  // is on, and if so, lock it off while setting format. Need to look
  // into if V4L2 supports adjusting controls while the stream is on.

  // For extended controls (any control class other than "user"),
  // S_EXT_CTRL must be used instead of S_CTRL.
  if (V4L2_CTRL_ID2CLASS(control_id) != V4L2_CTRL_CLASS_USER) {
    v4l2_ext_control control;
    v4l2_ext_controls controls;
    memset(&control, 0, sizeof(control));
    memset(&controls, 0, sizeof(controls));

    control.id = control_id;
    control.value = desired;
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control_id);
    controls.count = 1;
    controls.controls = &control;

    if (IoctlLocked(VIDIOC_S_EXT_CTRLS, &controls) < 0) {
      HAL_LOGE("S_EXT_CTRLS fails: %s", strerror(errno));
      return -ENODEV;
    }
    result_value = control.value;
  } else {
    v4l2_control control{control_id, desired};
    if (IoctlLocked(VIDIOC_S_CTRL, &control) < 0) {
      HAL_LOGE("S_CTRL fails: %s", strerror(errno));
      return -ENODEV;
    }
    result_value = control.value;
  }

  // If the caller wants to know the result, pass it back.
  if (result != nullptr) {
    *result = result_value;
  }
  return 0;
}

int V4L2Stream::SetParm(int mCapturemode) {
  HAL_LOG_ENTER();

  struct v4l2_streamparm params;
  memset(&params, 0, sizeof(params));

  params.parm.capture.timeperframe.numerator = 1;
  params.parm.capture.timeperframe.denominator = 30;
  params.parm.capture.reserved[0] = 0;
  params.type = V4L2_CAPTURE_TYPE;
  params.parm.capture.capturemode = mCapturemode;

  if (IoctlLocked(VIDIOC_S_PARM, &params) < 0) {
    HAL_LOGE("S_PARM fails: %s", strerror(errno));
    return -ENODEV;
  }

  return 0;
}

int V4L2Stream::GetFormats(std::set<uint32_t>* v4l2_formats) {
  HAL_LOG_ENTER();

  #if 0
  v4l2_fmtdesc format_query;
  memset(&format_query, 0, sizeof(format_query));

  // TODO(b/30000211): multiplanar support.
  format_query.type = V4L2_CAPTURE_TYPE;
  while (IoctlLocked(VIDIOC_ENUM_FMT, &format_query) >= 0) {
    v4l2_formats->insert(format_query.pixelformat);
    ++format_query.index;
    HAL_LOGV(
      "ENUM_FMT at index %d: ,pixelformat:%d.", format_query.index, format_query.pixelformat);
  }
  if (errno != EINVAL) {
    HAL_LOGE(
        "ENUM_FMT fails at index %d: %s", format_query.index, strerror(errno));
    return -ENODEV;
  }
  #endif

  int format_temp = V4L2_PIX_FMT_NV12;
  v4l2_formats->insert(format_temp);

  format_temp = V4L2_PIX_FMT_YUV420;
  v4l2_formats->insert(format_temp);

  format_temp = V4L2_PIX_FMT_NV21;
  v4l2_formats->insert(format_temp);

  // Add the jpeg format for take picture.
  format_temp = V4L2_PIX_FMT_JPEG;
  v4l2_formats->insert(format_temp);

  return 0;
}

int V4L2Stream::GetFormatFrameSizes(uint32_t v4l2_format,
                                     std::set<std::array<int32_t, 2>, std::greater<std::array<int32_t, 2>>>* sizes) {
  v4l2_frmsizeenum size_query;
  memset(&size_query, 0, sizeof(size_query));

  // Add the jpeg format for take picture.
  if(v4l2_format == V4L2_PIX_FMT_JPEG) {
    v4l2_format = V4L2_PIX_FMT_DEFAULT;
  }

  size_query.pixel_format = v4l2_format;

#if 0
  if (IoctlLocked(VIDIOC_ENUM_FRAMESIZES, &size_query) < 0) {
    HAL_LOGE("ENUM_FRAMESIZES failed: %s", strerror(errno));
    return -ENODEV;
  }
  if (size_query.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    // Discrete: enumerate all sizes using VIDIOC_ENUM_FRAMESIZES.
    // Assuming that a driver with discrete frame sizes has a reasonable number
    // of them.
    do {
      sizes->insert({{{static_cast<int32_t>(size_query.discrete.width),
                       static_cast<int32_t>(size_query.discrete.height)}}});
      ++size_query.index;
      HAL_LOGV("size_query.discrete.width x size_query.discrete.height %u x %u for size_query.index:%d",
         size_query.discrete.width,size_query.discrete.height,
         size_query.index);
    } while (IoctlLocked(VIDIOC_ENUM_FRAMESIZES, &size_query) >= 0);
    if (errno != EINVAL) {
      HAL_LOGW("ENUM_FRAMESIZES fails at index %d: %s,maybe we encount the ends of framesizes.",
               size_query.index,
               strerror(errno));
      //return -ENODEV;
    }
  } else {
    HAL_LOGV("size_query.stepwise.min_width x size_query.stepwise.min_height %u x %u.",
     size_query.stepwise.min_width,size_query.stepwise.min_height);
    HAL_LOGV("size_query.stepwise.max_width x size_query.stepwise.max_height %u x %u.",
     size_query.stepwise.max_width,size_query.stepwise.max_height);
    // Continuous/Step-wise: based on the stepwise struct returned by the query.
    // Fully listing all possible sizes, with large enough range/small enough
    // step size, may produce far too many potential sizes. Instead, find the
    // closest to a set of standard sizes.
    for (const auto size : kStandardSizes) {
      // Find the closest size, rounding up.
      uint32_t desired_width = size[0];
      uint32_t desired_height = size[1];
      if (desired_width < size_query.stepwise.min_width ||
          desired_height < size_query.stepwise.min_height) {
        HAL_LOGV("Standard size %u x %u is too small than %u x %u for format %d",
                 desired_width,desired_height,
                 size_query.stepwise.min_width,size_query.stepwise.min_height,
                 v4l2_format);
        continue;
      } else if (desired_width > size_query.stepwise.max_width &&
                 desired_height > size_query.stepwise.max_height) {
        HAL_LOGV("Standard size %u x %u is too big for format %d",
                 desired_width,desired_height,
                 size_query.stepwise.max_width,size_query.stepwise.max_height,
                 v4l2_format);
        continue;
      }

      // Round up.
      uint32_t width_steps = (desired_width - size_query.stepwise.min_width +
                              size_query.stepwise.step_width - 1) /
                             size_query.stepwise.step_width;
      uint32_t height_steps = (desired_height - size_query.stepwise.min_height +
                               size_query.stepwise.step_height - 1) /
                              size_query.stepwise.step_height;
      sizes->insert(
          {{{static_cast<int32_t>(size_query.stepwise.min_width +
                                  width_steps * size_query.stepwise.step_width),
             static_cast<int32_t>(size_query.stepwise.min_height +
                                  height_steps *
                                      size_query.stepwise.step_height)}}});
    }
  }
#endif

  char * value;
  value = mCameraConfig->supportPictureSizeValue();

  std::string st1 = value;
  int size_width = 0;
  int size_height = 0;
  std::string tmp;
  std::vector<std::string> data;
  std::stringstream input(st1);

  while(getline(input, tmp, ','))
  {
      data.push_back(tmp);
  }
  for(auto s : data)
  {
      sscanf(s.c_str(), "%dx%d", &size_width,&size_height);
      sizes->insert({{{size_width,size_height}}});
  }

  // Add for small sizes.
  /*if(device_id_ == DEVICE_FACING_BACK) {
    sizes->insert({{{1600,1200}}});
  }
  if(device_id_ == DEVICE_FACING_BACK) {
    sizes->insert({{{1280,960}}});
  }
  // Add for 16:9 ratio sizes.
  if(device_id_ == DEVICE_FACING_BACK) {
    sizes->insert({{{1280,720}}});
  }
  // Add for 16:9 ratio sizes.
  if(device_id_ == DEVICE_FACING_BACK || device_id_ == DEVICE_FACING_FRONT ) {
    sizes->insert({{{640,480}}});
  }

  // Add for cts case.
  if(device_id_ == DEVICE_FACING_BACK || device_id_ == DEVICE_FACING_FRONT ) {
    sizes->insert({{{320,240}}});
  }*/

  // TODOzjw: support camera config for device platform configuration. 

  return 0;
}

// Converts a v4l2_fract with units of seconds to an int64_t with units of ns.
inline int64_t FractToNs(const v4l2_fract& fract) {
  return (1000000000LL * fract.numerator) / fract.denominator;
}

int V4L2Stream::GetFormatFrameDurationRange(
    uint32_t v4l2_format,
    const std::array<int32_t, 2>& size,
    std::array<int64_t, 2>* duration_range) {
  // Potentially called so many times logging entry is a bad idea.

  v4l2_frmivalenum duration_query;
  memset(&duration_query, 0, sizeof(duration_query));

  // Add the jpeg format for take picture.
  if(v4l2_format == V4L2_PIX_FMT_JPEG) {
    v4l2_format = V4L2_PIX_FMT_DEFAULT;
  }

  duration_query.pixel_format = v4l2_format;
  duration_query.width = size[0];
  duration_query.height = size[1];
  //TODOzjw: fix the driver interface VIDIOC_ENUM_FRAMEINTERVALS
  if (IoctlLocked(VIDIOC_ENUM_FRAMEINTERVALS, &duration_query) < 0) {
    HAL_LOGW("ENUM_FRAMEINTERVALS failed: %s", strerror(errno));
    //return -ENODEV;
  }

  int64_t min = std::numeric_limits<int64_t>::max();
  int64_t max = std::numeric_limits<int64_t>::min();
  #if 0
  if (duration_query.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    // Discrete: enumerate all durations using VIDIOC_ENUM_FRAMEINTERVALS.
    do {
      min = std::min(min, FractToNs(duration_query.discrete));
      max = std::max(max, FractToNs(duration_query.discrete));
      ++duration_query.index;
    } while (IoctlLocked(VIDIOC_ENUM_FRAMEINTERVALS, &duration_query) >= 0);
    if (errno != EINVAL) {
      HAL_LOGE("ENUM_FRAMEINTERVALS fails at index %d: %s",
               duration_query.index,
               strerror(errno));
      return -ENODEV;
    }
  } else {
    // Continuous/Step-wise: simply convert the given min and max.
    min = FractToNs(duration_query.stepwise.min);
    max = FractToNs(duration_query.stepwise.max);
  }
  #endif
  min = 33300000;
  max = 100000000;
  (*duration_range)[0] = min;
  (*duration_range)[1] = max;
  return 0;
}

int V4L2Stream::SetFormat(const StreamFormat& desired_format,
                           uint32_t* result_max_buffers) {
  HAL_LOG_ENTER();

  if (format_ && desired_format == *format_) {
    HAL_LOGV("The desired format is as same as the format set last.");
    //*result_max_buffers = buffers_.size();
    //return 0;
  }

  // Not in the correct format, set the new one.

  if (format_) {
    // If we had an old format, first request 0 buffers to inform the device
    // we're no longer using any previously "allocated" buffers from the old
    // format. This seems like it shouldn't be necessary for USERPTR memory,
    // and/or should happen from turning the stream off, but the driver
    // complained. May be a driver issue, or may be intended behavior.
    int res = RequestBuffers(0);
    if (res) {
      return res;
    }
  }

  // Set the camera to the new format.
  v4l2_format new_format;
  desired_format.FillFormatRequest(&new_format);
  // TODO(b/29334616): When async, this will need to check if the stream
  // is on, and if so, lock it off while setting format.
  if (IoctlLocked(VIDIOC_S_FMT, &new_format) < 0) {
    HAL_LOGE("S_FMT failed: %s", strerror(errno));
    return -ENODEV;
  }

  if (IoctlLocked(VIDIOC_G_FMT, &new_format) < 0) {
    HAL_LOGE("G_FMT failed: %s", strerror(errno));
    return -ENODEV;
  }

  // Check that the driver actually set to the requested values.
  if (desired_format != new_format) {
    HAL_LOGE("Device doesn't support desired stream configuration.");
    //return -EINVAL;
  }

  // Keep track of our new format.
  format_.reset(new StreamFormat(new_format));

  // Format changed, request new buffers.
  int res = RequestBuffers(*result_max_buffers);
  if (res) {
    HAL_LOGE("Requesting buffers for new format failed.");
    return res;
  }
  *result_max_buffers = buffers_.size();
  HAL_LOGD("*result_max_buffers:%d.",*result_max_buffers);
  return 0;
}

int V4L2Stream::RequestBuffers(uint32_t num_requested) {
  v4l2_requestbuffers req_buffers;
  memset(&req_buffers, 0, sizeof(req_buffers));
  req_buffers.type = format_->type();
  req_buffers.memory = format_->memory();
  req_buffers.count = num_requested;

  int res = IoctlLocked(VIDIOC_REQBUFS, &req_buffers);
  // Calling REQBUFS releases all queued buffers back to the user.
  //int gralloc_res = gralloc_->unlockAllBuffers();
  if (res < 0) {
    HAL_LOGE("REQBUFS failed: %s", strerror(errno));
    return -ENODEV;
  }


  // V4L2 will set req_buffers.count to a number of buffers it can handle.
  if (num_requested > 0 && req_buffers.count < 1) {
    HAL_LOGE("REQBUFS claims it can't handle any buffers.");
    return -ENODEV;
  }

  {
    std::lock_guard<std::mutex> guard(cmd_queue_lock_);
    buffer_cnt_inflight_ = 0;
  }

  // refresh buffers_num_ queue.
  while(!buffers_num_.empty()) {
    buffers_num_.pop();
  }

  if(buffers_num_.empty()) {
    for (int i = 0; i < req_buffers.count; ++i) {
      buffers_num_.push(i);
      HAL_LOGD("buffers_num_ push:%d, size:%d.", i, buffers_num_.size());
    }
  }

  buffers_.resize(req_buffers.count, false);

  HAL_LOGD("num_requested:%d,req_buffers.count:%d.",num_requested,req_buffers.count);

  return 0;
}
int V4L2Stream::queueBuffer(v4l2_buffer* pdevice_buffer) {
  int res;
  std::lock_guard<std::mutex> guard(cmd_queue_lock_);
  res = IoctlLocked(VIDIOC_QBUF, pdevice_buffer);
  if(res >= 0) {
    buffer_cnt_inflight_++;
    HAL_LOGD("After queue buffer csi driver has %d buffer(s) now.",buffer_cnt_inflight_);
  }
  return res;
}
int V4L2Stream::dequeueBuffer(v4l2_buffer* pdevice_buffer) {
  int res;
  std::lock_guard<std::mutex> guard(cmd_queue_lock_);

  res = IoctlLocked(VIDIOC_DQBUF, pdevice_buffer);
  if(res >= 0) {
    buffer_cnt_inflight_--;
    HAL_LOGD("After dequeue buffer csi driver has %d buffer(s) now.",buffer_cnt_inflight_);
  }
  return res;
}

int V4L2Stream::PrepareBuffer() {
  if (!format_) {
    HAL_LOGE("Stream format must be set before enqueuing buffers.");
    return -ENODEV;
  }

  int ret = 0;
  struct v4l2_buffer device_buffer;
  int index = -1;

  for (int i = 0; i < buffers_.size(); i++)
  {
    std::lock_guard<std::mutex> guard(buffer_queue_lock_);
    index = buffers_num_.front();
    buffers_num_.pop();
    HAL_LOGD("buffers_num_ pop:%d, size:%d.", index, buffers_num_.size());

    // Set up a v4l2 buffer struct.
    memset(&device_buffer, 0, sizeof(device_buffer));
    device_buffer.type = format_->type();
    device_buffer.index = index;
    device_buffer.memory = format_->memory();
    device_buffer.length = format_->nplanes();
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    //TODOzjw:support mutiplanar.
    memset(planes, 0, VIDEO_MAX_PLANES*sizeof(struct v4l2_plane));
    if(V4L2_CAPTURE_TYPE == device_buffer.type) {
      device_buffer.m.planes =planes;
      if (NULL == device_buffer.m.planes) {
          HAL_LOGE("device_buffer.m.planes calloc failed!\n");
      }
    }

    // Use QUERYBUF to ensure our buffer/device is in good shape,
    // and fill out remaining fields.
    if (IoctlLocked(VIDIOC_QUERYBUF, &device_buffer) < 0) {
      HAL_LOGE("QUERYBUF fails: %s", strerror(errno));
      return -ENODEV;
    }

    mMapMem.mem[i] = mmap (0, device_buffer.m.planes[0].length,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                device_fd_,
                device_buffer.m.planes[0].m.mem_offset);
    mMapMem.length = device_buffer.m.planes[0].length;
    if (mMapMem.mem[i] == MAP_FAILED){
        HAL_LOGE("Unable to map buffer (%s)", strerror(errno));
        for(int j = 0;j < i;j++){
            munmap(buffers_addr[i], mMapMem.length);
        }
        return -1;
    }
    HAL_LOGD("index: %d, fd: %d, mem: %lx, len: %d, offset: %p", i, device_fd_,(unsigned long)mMapMem.mem[i], device_buffer.m.planes[0].length, device_buffer.m.offset);

    if (queueBuffer(&device_buffer) < 0) {
      HAL_LOGE("QBUF fails: %s", strerror(errno));
      return -ENODEV;
    }
    memset((void*)mMapMem.mem[i], 0x10, format_->width() * format_->height());
    memset((char *)mMapMem.mem[i] + format_->width() * format_->height(),
          0x80, format_->width() * format_->height() / 2);

  }

  HAL_LOGD("Buffers had been prepared!");
  return 0;

}

int V4L2Stream::EnqueueBuffer() {
  if (!format_) {
    HAL_LOGE("Stream format must be set before enqueuing buffers.");
    return -ENODEV;
  }

  // Find a free buffer index. Could use some sort of persistent hinting
  // here to improve expected efficiency, but buffers_.size() is expected
  // to be low enough (<10 experimentally) that it's not worth it.
  int index = -1;
  {
    std::unique_lock<std::mutex> lock(buffer_queue_lock_);
    while(buffers_num_.empty()) {
      HAL_LOGD("buffers_num_ is empty now, wait for the queue to be filled.");
      if(mflush_buffers) {
        mflush_buffers = false;
        return 0;
      }
      buffer_availabl_queue_.wait(lock);
      if(mflush_buffers) {
        mflush_buffers = false;
        return 0;
      }
    }
    index = buffers_num_.front();
    buffers_num_.pop();
    HAL_LOGD("buffers_num_ pop:%d, size:%d.", index, buffers_num_.size());
  }

  if (index < 0) {
    // Note: The HAL should be tracking the number of buffers in flight
    // for each stream, and should never overflow the device.
    HAL_LOGE("Cannot enqueue buffer: stream is already full.");
    return -ENODEV;
  }

  // Set up a v4l2 buffer struct.
  v4l2_buffer device_buffer;
  memset(&device_buffer, 0, sizeof(device_buffer));
  device_buffer.type = format_->type();
  device_buffer.index = index;
  device_buffer.memory = format_->memory();
  device_buffer.length = format_->nplanes();
  struct v4l2_plane planes[VIDEO_MAX_PLANES];
  memset(planes, 0, VIDEO_MAX_PLANES*sizeof(struct v4l2_plane));
  if(V4L2_CAPTURE_TYPE == device_buffer.type) {
    device_buffer.m.planes =planes;
    if (NULL == device_buffer.m.planes) {
        HAL_LOGE("device_buffer.m.planes calloc failed!\n");
    }
  }

  HAL_LOGD("mMapMem.mem[%d]:%p.", index, mMapMem.mem[index]);

  if (queueBuffer(&device_buffer) < 0) {
    HAL_LOGE("QBUF fails: %s", strerror(errno));
    return -ENODEV;
  }

  // Mark the buffer as in flight.
  std::lock_guard<std::mutex> guard(buffer_queue_lock_);
  buffers_[index] = true;

  return 0;
}

int V4L2Stream::DequeueBuffer(void ** src_addr_,struct timeval * ts) {
  if (!format_) {
    HAL_LOGV(
        "Format not set, so stream can't be on, "
        "so no buffers available for dequeueing");
    return -EAGAIN;
  }

  v4l2_buffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.type = format_->type();
  buffer.memory = format_->memory();
  buffer.length = format_->nplanes();
  struct v4l2_plane planes[VIDEO_MAX_PLANES];
  memset(planes, 0, VIDEO_MAX_PLANES*sizeof(struct v4l2_plane));
  if(V4L2_CAPTURE_TYPE == buffer.type) {
    buffer.m.planes =planes;
    if (NULL == buffer.m.planes) {
        HAL_LOGE("device_buffer.m.planes calloc failed!\n");
    }
  }

  int res = dequeueBuffer(&buffer);
  if (res) {
    if (errno == EAGAIN) {
      // Expected failure.
      return -EAGAIN;
    } else {
      // Unexpected failure.
      HAL_LOGE("DQBUF fails: %s", strerror(errno));
      return -ENODEV;
    }
  }

  *ts =  buffer.timestamp;
  *src_addr_ = mMapMem.mem[buffer.index];

  // Mark the buffer as no longer in flight.
  {
    std::lock_guard<std::mutex> guard(buffer_queue_lock_);
    buffers_[buffer.index] = false;
    //*index = buffers_num_.front();
    buffers_num_.push(buffer.index);
    HAL_LOGV("buffers_num_ push:%d, size:%d.", buffer.index, buffers_num_.size());
    buffer_availabl_queue_.notify_one();
    HAL_LOGV("buffer.index:%d has been freed by csi driver, and buffer_availabl_queue_ was notified!\n",buffer.index);
  }

  HAL_LOGV("mMapMem.mem[%d]:%p.", buffer.index, mMapMem.mem[buffer.index]);
  return 0;
}

int V4L2Stream::CopyBuffer(void * dst_addr, void * src_addr) {
  if (!format_) {
    HAL_LOGE("Stream format must be set before enqueuing buffers.");
    return -ENODEV;
  }

  int mCopySize = ALIGN_16B(format_->width())*ALIGN_16B(format_->height())*3/2;
  HAL_LOGD("dst_addr:%p,src_addr:%p,mCopySize:%d.", dst_addr, src_addr, mCopySize);
  memcpy(dst_addr, src_addr, mCopySize);

  return 0;
}
int V4L2Stream::EncodeBuffer(void * dst_addr, void * src_addr, unsigned long mJpegBufferSizes, JPEG_ENC_t jpeg_enc){
  isTakePicure = true;

  unsigned long jpeg_buf = (unsigned long)dst_addr;
  int bufSize = 0;

  // Get buffer size.
  HAL_LOGD("jpeg info:lock_buffer vaddr:%p, buffer size:%d.", jpeg_buf, mJpegBufferSizes);

#if 1
  if(jpeg_enc.src_w == 0) {
    jpeg_enc.src_w            = format_->width();
  }
  if(jpeg_enc.src_h == 0) {
    jpeg_enc.src_h            = format_->height();
  }

  jpeg_enc.colorFormat    = JPEG_COLOR_YUV420_NV21;
  //jpeg_enc.quality        = 90;
  //jpeg_enc.rotate            = 0;

  char                              mDateTime[64];
  time_t t;
  struct tm *tm_t;
  time(&t);
  tm_t = localtime(&t);
  sprintf(mDateTime, "%4d:%02d:%02d %02d:%02d:%02d",
      tm_t->tm_year+1900, tm_t->tm_mon+1, tm_t->tm_mday,
      tm_t->tm_hour, tm_t->tm_min, tm_t->tm_sec);

  char property[PROPERTY_VALUE_MAX];
  if (property_get("ro.product.manufacturer", property, "") > 0)
  {
      strcpy(jpeg_enc.CameraMake, property);
  }
  if (property_get("ro.product.model", property, "") > 0)
  {
      strcpy(jpeg_enc.CameraModel, property);
  }

  strcpy(jpeg_enc.DateTime, mDateTime);
  HAL_LOGD("jpeg info:%s.", mDateTime);

  //jpeg_enc.thumbWidth        = 320;
  //jpeg_enc.thumbHeight    = 240;
  jpeg_enc.whitebalance   = 0;
  jpeg_enc.focal_length    = 3.04;

#if 0
  if ((src_crop.width != jpeg_enc.src_w)
      || (src_crop.height != jpeg_enc.src_h))
  {
      jpeg_enc.enable_crop        = 1;
      jpeg_enc.crop_x                = src_crop.left;
      jpeg_enc.crop_y                = src_crop.top;
      jpeg_enc.crop_w                = src_crop.width;
      jpeg_enc.crop_h                = src_crop.height;
  }
  else
  {
      jpeg_enc.enable_crop        = 0;
  }
#endif
  HAL_LOGD("src: %dx%d, pic: %dx%d, quality: %d, rotate: %d, Gps method: %s,\
      thumbW: %d, thumbH: %d, thubmFactor: %d, crop: [%d, %d, %d, %d]",
      jpeg_enc.src_w, jpeg_enc.src_h,
      jpeg_enc.pic_w, jpeg_enc.pic_h,
      jpeg_enc.quality, jpeg_enc.rotate,
      jpeg_enc.gps_processing_method,
      jpeg_enc.thumbWidth,
      jpeg_enc.thumbHeight,
      jpeg_enc.scale_factor,
      jpeg_enc.crop_x,
      jpeg_enc.crop_y,
      jpeg_enc.crop_w,
      jpeg_enc.crop_h);


  JpegEncInfo sjpegInfo;
  EXIFInfo   exifInfo;
    
  memset(&sjpegInfo, 0, sizeof(JpegEncInfo));
  memset(&exifInfo, 0, sizeof(EXIFInfo));
  
  sjpegInfo.sBaseInfo.nStride = jpeg_enc.src_w;
  sjpegInfo.sBaseInfo.nInputWidth = jpeg_enc.src_w;
  sjpegInfo.sBaseInfo.nInputHeight = jpeg_enc.src_h;
  sjpegInfo.sBaseInfo.nDstWidth = jpeg_enc.pic_w;
  sjpegInfo.sBaseInfo.nDstHeight = jpeg_enc.pic_h;
  //sjpegInfo.sBaseInfo.memops = (ScMemOpsS*)memOpsS;
  //sjpegInfo.pAddrPhyY = (unsigned char*)src_addr_phy;
  //sjpegInfo.pAddrPhyC = (unsigned char*)src_addr_phy + ALIGN_16B(src_width) * src_height;
  sjpegInfo.sBaseInfo.eInputFormat = VENC_PIXEL_YVU420SP;
  sjpegInfo.quality        = jpeg_enc.quality;
  exifInfo.Orientation    = jpeg_enc.rotate;
  if(jpeg_enc.crop_h != 0) {
    sjpegInfo.nShareBufFd = jpeg_enc.crop_h;
    jpeg_enc.crop_h = 0;
    sjpegInfo.bNoUseAddrPhy = 0;
  } else {
    sjpegInfo.nShareBufFd = jpeg_enc.crop_h;
    jpeg_enc.crop_h = 0;
    sjpegInfo.bNoUseAddrPhy = 1;
  }

  sjpegInfo.pAddrPhyY = (unsigned char *)src_addr;
  sjpegInfo.pAddrPhyC = (unsigned char *)((unsigned long)src_addr + jpeg_enc.src_w *jpeg_enc.src_h); 
  sjpegInfo.pAddrVirY = (unsigned char *)src_addr;
  sjpegInfo.pAddrVirC = (unsigned char *)((unsigned long)src_addr + jpeg_enc.src_w *jpeg_enc.src_h); 

#if 0
  if ((((unsigned int)src_crop.width ) != sjpegInfo.sBaseInfo.nInputWidth)
      || (src_crop.height != sjpegInfo.sBaseInfo.nInputHeight))
  {
      sjpegInfo.bEnableCorp        = 1;
      sjpegInfo.sCropInfo.nLeft    = src_crop.left;
      sjpegInfo.sCropInfo.nTop    = src_crop.top;
      sjpegInfo.sCropInfo.nWidth    = src_crop.width;
      sjpegInfo.sCropInfo.nHeight    = src_crop.height;
  }
  else
  {
      sjpegInfo.bEnableCorp        = 0;
  }
#endif

  exifInfo.ThumbWidth = jpeg_enc.thumbWidth;
  exifInfo.ThumbHeight = jpeg_enc.thumbHeight;

  HAL_LOGD("src: %dx%d, pic: %dx%d, quality: %d, rotate: %d,\
      thumbW: %d, thumbH: %d,EnableCorp: %d,crop: [%d, %d, %d, %d],share_fd:%d",
      sjpegInfo.sBaseInfo.nInputWidth, sjpegInfo.sBaseInfo.nInputHeight,
      sjpegInfo.sBaseInfo.nDstWidth, sjpegInfo.sBaseInfo.nDstHeight,
      sjpegInfo.quality, exifInfo.Orientation,
      exifInfo.ThumbWidth,
      exifInfo.ThumbHeight,
      sjpegInfo.bEnableCorp,
      sjpegInfo.sCropInfo.nLeft,
      sjpegInfo.sCropInfo.nTop,
      sjpegInfo.sCropInfo.nWidth,
      sjpegInfo.sCropInfo.nHeight,sjpegInfo.nShareBufFd);

  strcpy((char*)exifInfo.CameraMake,    jpeg_enc.CameraMake);
  strcpy((char*)exifInfo.CameraModel,    jpeg_enc.CameraModel);
  strcpy((char*)exifInfo.DateTime, jpeg_enc.DateTime);

  struct timeval tv;
  int res = gettimeofday(&tv, NULL);
  char       subSecTime1[8];
  char       subSecTime2[8];
  char       subSecTime3[8];
  sprintf(subSecTime1, "%06ld", tv.tv_usec);
  sprintf(subSecTime2, "%06ld", tv.tv_usec);
  sprintf(subSecTime3, "%06ld", tv.tv_usec);
  strcpy((char*)exifInfo.subSecTime,     subSecTime1);
  strcpy((char*)exifInfo.subSecTimeOrig, subSecTime2);
  strcpy((char*)exifInfo.subSecTimeDig,  subSecTime3);

  if (0 != strlen(jpeg_enc.gps_processing_method)){
      strcpy((char*)exifInfo.gpsProcessingMethod,jpeg_enc.gps_processing_method);
      exifInfo.enableGpsInfo = 1;
      exifInfo.gps_latitude = jpeg_enc.gps_latitude;
      exifInfo.gps_longitude = jpeg_enc.gps_longitude;
      exifInfo.gps_altitude = jpeg_enc.gps_altitude;
      exifInfo.gps_timestamp = jpeg_enc.gps_timestamp;
  }
  else
      exifInfo.enableGpsInfo = 0;

  // TODO: fix parameter for sensor
  exifInfo.ExposureTime.num = 25;
  exifInfo.ExposureTime.den = 100;
  
  exifInfo.FNumber.num = 200; //eg:FNum=2.2, aperture = 220, --> num = 220,den = 100
  exifInfo.FNumber.den = 100;
  exifInfo.ISOSpeed = 400;
  
  exifInfo.ExposureBiasValue.num= 25;
  exifInfo.ExposureBiasValue.den= 100;
  
  exifInfo.MeteringMode = 0;
  exifInfo.FlashUsed = 0;
  
  exifInfo.FocalLength.num = 304;
  exifInfo.FocalLength.den = 100;

  exifInfo.DigitalZoomRatio.num = 0;
  exifInfo.DigitalZoomRatio.den = 0;
  
  exifInfo.WhiteBalance = 0;
  exifInfo.ExposureMode = 0;

  int ret = AWJpecEnc(&sjpegInfo, &exifInfo, (void *)jpeg_buf, &bufSize);
  //int64_t lasttime = systemTime();
  if (ret < 0)
  {
      HAL_LOGE("JpegEnc failed");
      return false;
  }
  //LOGV("hw enc time: %lld(ms), size: %d", (systemTime() - lasttime)/1000000, bufSize);
  camera3_jpeg_blob_t jpegHeader;
  jpegHeader.jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
  jpegHeader.jpeg_size = bufSize;
  unsigned long jpeg_eof_offset =
          (unsigned long)(mJpegBufferSizes - (unsigned long)sizeof(jpegHeader));
  char *jpeg_eof = reinterpret_cast<char *>(jpeg_buf +jpeg_eof_offset);
  memcpy(jpeg_eof, &jpegHeader, sizeof(jpegHeader));
#endif


  return 0;
}

int V4L2Stream::WaitCameraReady()
{
  if (!format_) {
    HAL_LOGV(
        "Format not set, so stream can't be on, "
        "so no buffers available for Ready");
    return -EAGAIN;
  }

  fd_set fds;
  struct timeval tv;
  int r;

  FD_ZERO(&fds);
  FD_SET(device_fd_, &fds);

  /* Timeout */
  tv.tv_sec  = 1;
  tv.tv_usec = 0;

  r = select(device_fd_ + 1, &fds, NULL, NULL, &tv);
  if (r == -1)
  {
      HAL_LOGE("select err, %s", strerror(errno));
      return -1;
  }
  else if (r == 0)
  {
      HAL_LOGW("Select timeout.");
      return -1;
  }

  return 0;
}

}  // namespace v4l2_camera_hal
