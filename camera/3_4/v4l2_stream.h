
#ifndef V4L2_CAMERA_HAL_V4L2_STREAM_H_
#define V4L2_CAMERA_HAL_V4L2_STREAM_H_

#include <array>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <queue>

#include <android-base/unique_fd.h>
#include <utils/StrongPointer.h>
#include <utils/RefBase.h>
#include <utils/Log.h>
#include <android/log.h>


#include "common.h"
#include "stream_format.h"
#include "v4l2_gralloc.h"
#include "v4l2_wrapper.h"
#include "camera_config.h"

//#include "videodev2_34.h"
#ifdef USE_ISP
#include "AWIspApi.h"
#endif

#include "type_camera.h"

//#include "MetadataBufferType.h"

namespace v4l2_camera_hal {

class V4L2Stream : public virtual android::RefBase  {
 friend class V4L2Wrapper;
 friend class ConnectionStream;
 public:
  // Use this method to create V4L2Stream objects. Functionally equivalent
  // to "new V4L2Stream", except that it may return nullptr in case of failure.
  static V4L2Stream* NewV4L2Stream(const int id, const std::string device_path, CCameraConfig* pCameraCfg);
  virtual ~V4L2Stream();

  // Turn the stream on or off.
  virtual int StreamOn();
  virtual int StreamOff();
  virtual int flush();
  // Manage controls.
  virtual int QueryControl(uint32_t control_id, v4l2_query_ext_ctrl* result);
  virtual int GetControl(uint32_t control_id, int32_t* value);
  virtual int SetControl(uint32_t control_id,
                         int32_t desired,
                         int32_t* result = nullptr);
  virtual int SetParm(int mCapturemode);
  // Manage format.
  virtual int GetFormats(std::set<uint32_t>* v4l2_formats);
  virtual int GetFormatFrameSizes(uint32_t v4l2_format,
                                       std::set<std::array<int32_t, 2>, std::greater<std::array<int32_t, 2>>>* sizes);

  // Durations are returned in ns.
  virtual int GetFormatFrameDurationRange(
      uint32_t v4l2_format,
      const std::array<int32_t, 2>& size,
      std::array<int64_t, 2>* duration_range);
  virtual int SetFormat(const StreamFormat& desired_format,
                        uint32_t* result_max_buffers);
  // Manage buffers.
  virtual int PrepareBuffer();
  virtual int WaitCameraReady();
  virtual int EnqueueBuffer();
  virtual int DequeueBuffer(void ** src_addr_,struct timeval * ts);
  virtual int CopyBuffer(void * dst_addr, void * src_addr);
  virtual int EncodeBuffer(void * dst_addr, void * src_addr, unsigned long mJpegBufferSizes, JPEG_ENC_t jpeg_enc);
  virtual int queueBuffer(v4l2_buffer* pdevice_buffer);
  virtual int dequeueBuffer(v4l2_buffer* pdevice_buffer);

  // Take picture tools.
 // virtual int TakePicture(const camera3_stream_buffer_t* camera_buffer,
  //                               uint32_t result_index);

  // Tools.
  int GetDeviceId(){ return device_id_;};
  int GetDeviceWidth(){ return format_->width();};
  int GetDeviceHeight(){ return format_->height();};

  STREAM_SERIAL getStreamSerial(){ return device_ss_;};


private:
  CCameraConfig *    mCameraConfig;
  // Constructor is private to allow failing on bad input.
  // Use NewV4L2Stream instead.
  V4L2Stream(const int id, const std::string device_path, CCameraConfig* pCameraCfg);

  // Connect or disconnect to the device. Access by creating/destroying
  // a V4L2Wrapper::Connection object.
  int Connect();
  void Disconnect();
  // Perform an ioctl call in a thread-safe fashion.
  template <typename T>
  int IoctlLocked(int request, T data);
  // Request/release userspace buffer mode via VIDIOC_REQBUFS.
  int RequestBuffers(uint32_t num_buffers);

  inline bool connected() { return device_fd_ >= 0; }

  // The camera device path. For example, /dev/video0.
  const std::string device_path_;
  STREAM_SERIAL device_ss_;

  // Weak ptr to parent obj for safty calling.
  //android::wp<V4L2Wrapper> parent_;

  // The opened device fd.
  //android::base::unique_fd device_fd_;
  int device_fd_;
  // The open camera facing.
  const int device_id_;

  // Whether or not the device supports the extended control query.
  bool extended_query_supported_;
  // The format this device is set up for.
  std::unique_ptr<StreamFormat> format_;
  
  unsigned long  mTimeStampsFstreamon;
  //
  bool has_StreamOn;
  //
  bool mflush_buffers;
  bool isTakePicure;
  enum{
    BUFFER_UNINIT,
    BUFFER_PREPARE,
    BUFFER_QUEUE,
    BUFFER_DEQUEUE
    } buffer_state_;
  // Map indecies to buffer status. True if the index is in-flight.
  // |buffers_.size()| will always be the maximum number of buffers this device
  // can handle in its current format.
  std::vector<bool> buffers_pstream_;

  // Map indecies to buffer status. True if the index is in-flight.
  // |buffers_.size()| will always be the maximum number of buffers this device
  // can handle in its current format.
  std::vector<bool> buffers_;

  // Lock protecting use of the buffer tracker.
  std::mutex buffer_queue_lock_;
  std::queue<int>buffers_num_;
  std::condition_variable buffer_availabl_queue_;


  // Buffer manager.
  std::mutex cmd_queue_lock_;
  int buffer_cnt_inflight_;
  // Lock protecting use of the device.
  std::mutex device_lock_;
  // Lock protecting connecting/disconnecting the device.
  std::mutex connection_lock_;
  // Reference count connections.
  int connection_count_;

  // Debug tools for save buffers.
  void * buffers_addr[MAX_BUFFER_NUM];
  int buffers_fd[MAX_BUFFER_NUM];
  
  typedef struct v4l2_mem_map_t{
      void *    mem[MAX_BUFFER_NUM];
      int     length;
      int             nShareBufFd[MAX_BUFFER_NUM];
      int             nDmaBufFd[MAX_BUFFER_NUM];
  }v4l2_mem_map_t;
  v4l2_mem_map_t                    mMapMem;

#ifdef USE_ISP
  android::AWIspApi                          *mAWIspApi;
  int                               mIspId;
#endif

  friend class Connection;
  //friend class V4L2WrapperMock;
#ifdef USE_ISP
  friend class android::AWIspApi;
#endif

  DISALLOW_COPY_AND_ASSIGN(V4L2Stream);
};

// Helper class to ensure all opened connections are closed.
class ConnectionStream {
friend class V4L2Stream;
 public:
  ConnectionStream(std::shared_ptr<V4L2Stream> device)
      : device_(std::move(device)){
      connect_result_ = device_->Connect();
  }
  ~ConnectionStream() {
    if (connect_result_ == 0) {
      device_->Disconnect();
    }
  }
  // Check whether the connection succeeded or not.
  inline int status() const { return connect_result_; }

 private:
  std::shared_ptr<V4L2Stream> device_;
  int connect_result_;
  DISALLOW_COPY_AND_ASSIGN(ConnectionStream);
};

}  // namespace v4l2_camera_hal

#endif  // V4L2_CAMERA_HAL_V4L2_WRAPPER_H_
