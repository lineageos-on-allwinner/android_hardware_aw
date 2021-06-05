
#ifndef V4L2_CAMERA_HAL_COMMON_H_
#define V4L2_CAMERA_HAL_COMMON_H_

#include <utils/Log.h>
#include <sys/time.h>

#define ATRACE_TAG (ATRACE_TAG_CAMERA | ATRACE_TAG_HAL)
#include <utils/Trace.h>

#ifndef LOG_TAG
#define LOG_TAG "CameraHALv3_4"
#endif

#define ALIGN_4K(x) (((x) + (4095)) & ~(4095))
#define ALIGN_32B(x) (((x) + (31)) & ~(31))
#define ALIGN_16B(x) (((x) + (15)) & ~(15))
#define ALIGN_8B(x) (((x) + (7)) & ~(7))
// Debug setting.
#define DBG_V4L2_CAMERA               1
#define DBG_CAMERA                    1
#define DBG_V4L2_WRAPPER              1
#define DBG_V4L2_STREAM               1
#define DBG_STREAM_MANAGER            1
#define DBG_V4L2_GRALLOC              1
#define DBG_CAMERA_CONFIG             1
#define DEBUG_PERFORMANCE             1
// Disable all print information
//#define LOG_NDEBUG 1

/*0.Log.e   1.Log.w   2.Log.i   3.Log.d   4.Log.v*/
#define LOG_LEVEL                     0
#define HAL_LOGE(fmt, args...) if(LOG_LEVEL == 0) ALOGE("%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGW(fmt, args...) if(LOG_LEVEL > 0) ALOGW("%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGI(fmt, args...) if(LOG_LEVEL > 1) ALOGI("%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGD(fmt, args...) if(LOG_LEVEL > 2) ALOGD("%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGV(fmt, args...) if(LOG_LEVEL > 3) ALOGV("%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGE_IF(cond, fmt, args...) if(LOG_LEVEL == 0) \
    ALOGE_IF(cond, "%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGW_IF(cond, fmt, args...) if(LOG_LEVEL > 0) \
    ALOGW_IF(cond, "%s:%d: " fmt, __func__, __LINE__, ##args)
#define HAL_LOGI_IF(cond, fmt, args...) if(LOG_LEVEL > 1) \
    ALOGI_IF(cond, "%s:%d: " fmt, __func__, __LINE__, ##args)

// Log enter/exit of methods.
#define HAL_LOG_ENTER() HAL_LOGV("enter")
#define HAL_LOG_EXIT() HAL_LOGV("exit")

// Fix confliction in case it's defined elsewhere.
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);  \
  void operator=(const TypeName&);
#endif


// Common setting.

#define MAX_NUM_OF_CAMERAS    2
#define DEVICE_FACING_FRONT   1
#define DEVICE_FACING_BACK    0
#define TIMEOUT_COUNT    0
#define MAX_STREAM_NUM 3
#define DROP_BUFFERS_NUM    3
#define MAX_FRAME_NUM 128
// ms delay between stream on and off.
#define DELAY_BETWEEN_STREAM 500
#define DELAY_BETWEEN_ON_OFF 0

#define MAIN_STREAM_PATH "/dev/video0"
#define SUB_0_STREAM_PATH "/dev/video1"

enum STREAM_SERIAL {
  MAIN_STREAM = 0,
  MAIN_STREAM_BLOB,
  SUB_0_STREAM,
  SUB_0_STREAM_BLOB,
  MAIN_MIRROR_STREAM,
  MAIN_MIRROR_STREAM_BLOB,
  SUB_0_MIRROR_STREAM,
  SUB_0_MIRROR_STREAM_BLOB,
  MAX_STREAM,
  ERROR_STREAM
};


// Platform setting.
#if defined __A50__
#define MAX_NUM_OF_STREAMS    1

#define USE_CSI_VIN_DRIVER
#define USE_ISP

#define MAX_BUFFER_NUM 3
#define MAX_BUFFER_CSI_RESERVE 2

#define PICTURE_BUFFER_NUM 1

#define V4L2_PIX_FMT_DEFAULT V4L2_PIX_FMT_NV21

// Platform setting.
#elif defined __T3__
#define MAX_NUM_OF_STREAMS    1

#define USE_CSI_VIN_DRIVER
#define USE_ISP

#define MAX_BUFFER_NUM 3
#define MAX_BUFFER_CSI_RESERVE 2

#define PICTURE_BUFFER_NUM 1

#define V4L2_PIX_FMT_DEFAULT V4L2_PIX_FMT_NV21

#elif defined __A63__
#define MAX_NUM_OF_STREAMS    1

#define USE_CSI_VIN_DRIVER
//#define USE_ISP

#define MAX_BUFFER_NUM 3
#define MAX_BUFFER_CSI_RESERVE 2

#define PICTURE_BUFFER_NUM 1


#define V4L2_PIX_FMT_DEFAULT V4L2_PIX_FMT_NV21

// universal implement.
#else
#define MAX_NUM_OF_STREAMS    1

#define USE_CSI_VIN_DRIVER
#define USE_ISP

#define MAX_BUFFER_NUM 3
#define MAX_BUFFER_CSI_RESERVE 2

#define PICTURE_BUFFER_NUM 1

#define V4L2_PIX_FMT_DEFAULT V4L2_PIX_FMT_NV21

#endif

#ifdef USE_CSI_VIN_DRIVER
#define V4L2_CAPTURE_TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#else
#define V4L2_CAPTURE_TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE
#endif

// Tools for save buffers.
#define DBG_BUFFER_SAVE 0
#define DBG_BUFFER_SAVE_ONE_FRAME 1
#define DBG_BUFFER_SAVE_MORE_FRAME 0

#define PATH "/data/camera/"
extern void * buffers_addr[MAX_BUFFER_NUM];
extern bool saveBuffers(char *str,void *p, unsigned int length,bool is_oneframe);
extern bool saveSizes(int width, int height);

// 
typedef uint8_t byte; 
typedef int32_t int32;
typedef int64_t int64;



#endif  // V4L2_CAMERA_HAL_COMMON_H_
