


#if DBG_V4L2_CAMERA

#endif
#define LOG_TAG "CameraHALv3_V4L2Camera"
#undef NDEBUG

#include <utils/Log.h>


#include "v4l2_camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <cstdlib>

#include "CameraMetadata.h"
#include <hardware/camera3.h>


#include "common.h"
#include "function_thread.h"
#include "metadata/metadata_common.h"
#include "stream_format.h"
#include "v4l2_metadata_factory.h"
#include "camera_config.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

namespace v4l2_camera_hal {

using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

// Helper function for managing metadata.
static std::vector<int32_t> getMetadataKeys(const camera_metadata_t* metadata) {
  std::vector<int32_t> keys;
  size_t num_entries = get_camera_metadata_entry_count(metadata);
  for (size_t i = 0; i < num_entries; ++i) {
    camera_metadata_ro_entry_t entry;
    get_camera_metadata_ro_entry(metadata, i, &entry);
    keys.push_back(entry.tag);
  }
  return keys;
}

V4L2Camera* V4L2Camera::NewV4L2Camera(int id, const std::string path, CCameraConfig* pCameraCfg) {
  HAL_LOG_ENTER();

  // Select one stream for fill metadata.The path has been pick the stream for query format.
  std::shared_ptr<V4L2Wrapper> v4l2_wrapper(V4L2Wrapper::NewV4L2Wrapper(id, pCameraCfg));
  if (!v4l2_wrapper) {
    HAL_LOGE("Failed to initialize V4L2 wrapper.");
    return nullptr;
  }
  HAL_LOGD("v4l2_wrapper.count %d", v4l2_wrapper.use_count());

  std::unique_ptr<Metadata> metadata;
  // TODOzjw: fix the Metadata frameworks, the google Metadata manager frameworks is
  // too too too complex for us to debug and using.Damn it.
  // Using for format query.
  int res = GetV4L2Metadata(v4l2_wrapper, &metadata, pCameraCfg);
  if (res) {
    HAL_LOGE("Failed to initialize V4L2 metadata: %d", res);
    return nullptr;
  }

  HAL_LOGD("v4l2_wrapper.count %d", v4l2_wrapper.use_count());

  return new V4L2Camera(id, std::move(v4l2_wrapper), std::move(metadata));

  //return new V4L2Camera(id, std::move(metadata));
}

V4L2Camera::V4L2Camera(int id,
                       std::shared_ptr<V4L2Wrapper> v4l2_wrapper,
                       std::unique_ptr<Metadata> metadata)
    : default_camera_hal::Camera(id),
      device_(std::move(v4l2_wrapper)),
      device_id_(id),
      mgSituationMode(INIT),
      metadata_(std::move(metadata)),
      max_input_streams_(0),
      max_output_streams_({{0, 0, 0}}),
      buffers_in_flight_flag_(false)
{
  HAL_LOG_ENTER();
  instance = std::shared_ptr<V4L2Camera>(this);
#if 0
  connection[MAIN_STREAM].reset(new V4L2Wrapper::Connection(device_, MAIN_STREAM));
  if (connection[MAIN_STREAM]->status()) {
    HAL_LOGE("Failed to connect to device: %d.", connection[MAIN_STREAM]->status());
    //return connection[MAIN_STREAM].status();
  }
  mStreamm = device_->getStream(MAIN_STREAM);
  if (mStreamm == nullptr) {
    HAL_LOGE("Failed to get Stream %d, we should connect first.", MAIN_STREAM);
    //return 1;
  }
  connection[SUB_0_STREAM].reset(new V4L2Wrapper::Connection(device_, SUB_0_STREAM));
  if (connection[SUB_0_STREAM]->status()) {
    HAL_LOGE("Failed to connect to device: %d.", connection[SUB_0_STREAM]->status());
    //return connection[SUB_0_STREAM]->status();
  }
  mStreams = device_->getStream(SUB_0_STREAM);
  if (mStreams == nullptr) {
    HAL_LOGE("Failed to get Stream %d, we should connect first.", SUB_0_STREAM);
    //return 1;
  }
#endif

  HAL_LOGD("v4l2_wrapper.count %d", v4l2_wrapper.use_count());

  memset(&mBeffer_status, 0, sizeof(mBeffer_status));
  memset(&mStream_status, 0, sizeof(mStream_status));
  //memset(&mStreamFormatMap, 0, sizeof(mStreamFormatMap));
  //mBeffer_status.pbuffers = INIT_BUFFER;
  mStream_status.pstream = OPEN_STREAM;

}

V4L2Camera::~V4L2Camera() {
  HAL_LOG_ENTER();
  #if 0
  connection[MAIN_STREAM].reset();
  connection[SUB_0_STREAM].reset();
  #endif
}

int V4L2Camera::connect() {
  HAL_LOG_ENTER();

  return 0;
}

void V4L2Camera::disconnect() {
  HAL_LOG_ENTER();
  HAL_LOGD("before disconnect.");
  //std::lock_guard<std::mutex> guard(stream_lock_);
  // stop stream.
  #if 1
  if(mStreamManager_ != nullptr) {
    for(int ss = 0; ss < MAX_STREAM; ss++) {
      mStreamManager_->stop((STREAM_SERIAL)ss);
    }
  }
  #endif
  HAL_LOGD("after disconnect.");


  mStreamManager_.reset();

  flushRequests(-ENODEV);
  mBeffer_status.pbuffers = INIT_BUFFER;
  mBeffer_status.vbuffers = INIT_BUFFER;

  // TODO(b/29158098): Inform service of any flashes that are available again
  // because this camera is no longer in use.
}

int V4L2Camera::flushBuffers() {
  HAL_LOG_ENTER();
  int res = 0;
  return res;
}

int V4L2Camera::flushRequests(int err) {
  HAL_LOG_ENTER();
    //Calvin: encount wrong in picture mode.

  // This is not strictly necessary, but prevents a buildup of aborted
  // requests in the in_flight_ map. These should be cleared
  // whenever the stream is turned off.
  HAL_LOGD("We hold %d request(s) in Camera HAL,let us send them to frameworks.",in_flight_.size());
  for(int i = (MAX_BUFFER_NUM -1); i >= 0; i--) {
    //std::lock_guard<std::mutex> guard(in_flight_lock_); // confict in picture mode.
    auto brequest = in_flight_.find(i);
    if(brequest != in_flight_.end()) {
      HAL_LOGD("Send No.%d request to frameworks request queue.",i);
      completeRequest(brequest->second, err);
      in_flight_.erase(i);
    } else {
      HAL_LOGD("No.%d request request had been called back.",i);
      in_flight_.erase(i);
    }
  }

  HAL_LOG_EXIT();

  return 0;
}
int V4L2Camera::flushRequestsForCTS(int err) {
  HAL_LOG_ENTER();
  uint32_t mFrameNum = 0;
  std::shared_ptr<default_camera_hal::CaptureRequest> mFrameNumRequest;
  // This is not strictly necessary, but prevents a buildup of aborted
  // requests in the in_flight_ map. These should be cleared
  // whenever the stream is turned off.
#if 1 // lock confict with framework send request to us. So why should we clear the request here?
  {
    std::lock_guard<std::mutex> guard(request_queue_stream_lock_);
    while(!request_queue_pstream_.empty()) {
      request_queue_pstream_.pop();
    }
    while(!request_queue_tstream_.empty()) {
      request_queue_tstream_.pop();
    }
    while(!request_queue_vstream_.empty()) {
      request_queue_vstream_.pop();
    }
  }
#endif

  HAL_LOGD("We hold %d request(s) in Camera HAL,let us send them to frameworks.",in_flight_.size());
  {
    std::lock_guard<std::mutex> guard(in_flight_lock_);
    for(int i = (MAX_BUFFER_NUM -1); i >= 0; i--) {
      auto brequest = in_flight_.find(i);
      if(brequest != in_flight_.end()) {
        HAL_LOGD("Send No.%d request to frameworks request queue, frame_number:%d.", i, brequest->second->frame_number);
        if((brequest->second->frame_number < mFrameNum) && (0 != mFrameNum)) {
          completeRequest(brequest->second, err);
          completeRequest(mFrameNumRequest, err);
        } else if((brequest->second->frame_number > mFrameNum) && (0 != mFrameNum)) {
          completeRequest(mFrameNumRequest, err);
          completeRequest(brequest->second, err);
        }
        mFrameNum = brequest->second->frame_number;
        mFrameNumRequest = brequest->second;
        // Erase the buffer in the lock of in_flight_lock_.
        in_flight_.erase(i);
      } else {
        // Erase the buffer in the lock of in_flight_lock_.
        // There are leave buffer in csi driver that will lead the request delay the buffer.
        in_flight_.erase(i);
        HAL_LOGD("No.%d request request had been called back.",i);
      }
    }
  }

  HAL_LOG_EXIT();

  return 0;
}

int V4L2Camera::initStaticInfo(CameraMetadata* out) {
  HAL_LOG_ENTER();

  int res = metadata_->FillStaticMetadata(out);
  if (res) {
    HAL_LOGE("Failed to get static metadata.");
    return res;
  }
  // Extract max streams for use in verifying stream configs.
  res = SingleTagValue(
      *out, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &max_input_streams_);
  if (res) {
    HAL_LOGE("Failed to get max num input streams from static metadata.");
    return res;
  }
  res = SingleTagValue(
      *out, ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, &max_output_streams_);
  if (res) {
    HAL_LOGE("Failed to get max num output streams from static metadata.");
    return res;
  }

  return 0;
}

int V4L2Camera::initTemplate(int type, CameraMetadata* out) {
  HAL_LOG_ENTER();

  return metadata_->GetRequestTemplate(type, out);
}

void V4L2Camera::initDeviceInfo(camera_info_t* info) {
  HAL_LOG_ENTER();

  // TODO(b/31044975): move this into device interface.
  // For now, just constants.
  info->resource_cost = 100;
  info->conflicting_devices = nullptr;
  info->conflicting_devices_length = 0;
}

int V4L2Camera::initDevice() {
  HAL_LOG_ENTER();
  return 0;
}


int V4L2Camera::updateStatus(SituationMode mSituationMode) {

  mgSituationMode = mSituationMode;
  switch (mSituationMode) {
    case BEFORE_PREVIEW:
      break;
    case BEFORE_VIDEO:
      break;
    case BEFORE_PICTURE:
      mStream_status.pstream = CLOSE_STREAM;
      mStream_status.tstream = OPEN_STREAM;
      mBeffer_status.tbuffers = INIT_BUFFER;
      break;
    case AFTER_PICTURE:
      mStream_status.pstream = OPEN_STREAM;
      mBeffer_status.pbuffers = INIT_BUFFER;
      mBeffer_status.tbuffers = INIT_BUFFER;
      break;
    default:
      break;
    }
  return -1;
}


V4L2Camera::SituationMode V4L2Camera::getStatus() {
  return mgSituationMode;
}

V4L2Camera::RequestMode V4L2Camera::analysisRequest(
    std::shared_ptr<default_camera_hal::CaptureRequest> request) {

  if(request == nullptr) {
    if((mVideoflag||isVideoByTemplate) && mBeffer_status.vbuffers == PREPARE_BUFFER) {
      return RUN_VIDEO_MODE;
    } else if(mBeffer_status.pbuffers == PREPARE_BUFFER){
      return RUN_PREVIEW_MODE;
    }
  } else {
    // If the request include the tstream, return immediatly.
    for(int i = 0; i <request->output_buffers.size(); i++) {
      if((analysisStreamModes(request->output_buffers[i].stream) == TSTREAM) && (request->tbuffer_has_been_used == false)) {
        return PICTURE_MODE;
      }
    }

    // If the request include the vstream and in video mode, return immediatly.
    for(int i = 0; i <request->output_buffers.size(); i++) {
      if((analysisStreamModes(request->output_buffers[i].stream) == VSTREAM) && (mVideoflag||isVideoByTemplate) ) {
        return VIDEO_MODE;
      }
    }

    // Deal the preview stream in the final.
    for(int i = 0; i <request->output_buffers.size(); i++) {
      if((analysisStreamModes(request->output_buffers[i].stream) == PSTREAM) ) {
        return PREVIEW_MODE;
      }
    }
  } 

  return ERROR_MODE;
}
int V4L2Camera::analysisStreamNum(
    std::shared_ptr<default_camera_hal::CaptureRequest> request, RequestMode mRequestMode) {


  int revalue = -1;
  for(int i = 0; i <request->output_buffers.size(); i++) {
    HAL_LOGD("Get format %d!", request->output_buffers[i].stream->format);
    if(request->output_buffers[i].stream->format == HAL_PIXEL_FORMAT_BLOB) {
      if(mRequestMode == VIDEO_SNAP_SHOT_MODE) {
        revalue = i;
      }
    }

    if(request->output_buffers[i].stream->format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
      if(mRequestMode == PICTURE_MODE) {
        revalue = i;
      }
    }

    if(request->output_buffers[i].stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if((mRequestMode == PREVIEW_MODE) && (analysisStreamModes(request->output_buffers[i].stream) == PSTREAM)) {
        revalue = i;
      }
      if((mRequestMode == VIDEO_MODE) && (analysisStreamModes(request->output_buffers[i].stream) == VSTREAM)) {
        revalue = i;
      }
    }
  }
  HAL_LOGD("No.%d is picked.", revalue);

  return revalue;
}

V4L2Camera::StreamIedex V4L2Camera::analysisStreamModes(
    camera3_stream_t * stream) {

  HAL_LOGD("Get usage %d, format %d!", stream->usage, stream->format);

  StreamIedex mStreamIedex = ESTREAM;
  switch (stream->format) {
    case HAL_PIXEL_FORMAT_YCBCR_420_888: {
        HAL_LOGD("Detected the picture stream.");
        mStreamIedex = TSTREAM;
        break;
    }
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED: {
        if((stream->usage & GRALLOC_USAGE_HW_TEXTURE)||(stream->usage & (GRALLOC_USAGE_HW_COMPOSER|GRALLOC_USAGE_HW_TEXTURE))) {
          mStreamIedex = PSTREAM;
        } else if((stream->usage & (GRALLOC_USAGE_HW_VIDEO_ENCODER|GRALLOC_USAGE_HW_2D)) 
                //||((stream->usage & GRALLOC_USAGE_SW_READ_OFTEN)&&(isVideoByTemplate))
                  ) {
          mStreamIedex = VSTREAM;
        }
        break;
    }
    // Be compatible with Camera api 1.
    case HAL_PIXEL_FORMAT_BLOB: {
        #if 1
        if((stream->usage & GRALLOC_USAGE_SW_READ_OFTEN)&&(isVideoByTemplate)) {
             mStreamIedex = VSTREAM;
        }
        #endif
        if ((stream->usage & GRALLOC_USAGE_SW_READ_OFTEN)&& (stream->usage & GRALLOC_USAGE_SW_WRITE_OFTEN)) {
          mStreamIedex = VHSTREAM;
          //isVideoSnap  = true;
          HAL_LOGD("Set stream isVideoSnap flag to ture!");
        }
        break;
    }
    default: {
      HAL_LOGE("Do not find any stream!");
      break;
    }
  }

  return mStreamIedex;
}

int V4L2Camera::max(int a, int b, int c) {

  int ret = -1;
  if(a >= b) {
    if(a >= c){
      ret = 0;
    } else {
      ret = 2;
    }
  } else {
    if(b >= c){
      ret = 1;
    } else {
      ret = 2;
    }
  }
  HAL_LOGV("a:%d,b:%d,c:%d, ret index: %d!", a, b, c, ret);
  return ret;
}

int V4L2Camera::min(int a, int b, int c) {

  int ret = -1;
  if(a >= b) {
    if(b >= c){
      ret = 2;
    } else {
      ret = 1;
    }
  } else {
    if(a >= c){
      ret = 2;
    } else {
      ret = 0;
    }
  }
  HAL_LOGV("a:%d,b:%d,c:%d, ret index: %d!", a, b, c, ret);
  return ret;
}

int V4L2Camera::maxWidth(camera3_stream_t ** streams, int num) {
  int ret = -1;
  if(num == 0) {
    HAL_LOGE("Prepare num err!");  
  } else if (num == 1) {
    ret = 0;
  } else if (num == 2) {
    if(streams[0]->width >streams[1]->width) {
      ret = 0;
    } else if (streams[0]->width = streams[1]->width) {
      ret = 0;
    } else {
      ret = 1;
    }  
  } else if (num == 3) {
    ret = max(streams[0]->width, streams[1]->width, streams[2]->width);
  }
  return ret;
}
int V4L2Camera::maxHeight(camera3_stream_t ** streams, int num) {
  int ret = -1;
  if(num == 0) {
    HAL_LOGE("Prepare num err!");  
  } else if (num == 1) {
    ret = 0;
  } else if (num == 2) {
    if(streams[0]->height >streams[1]->height) {
      ret = 0;
    } else if (streams[0]->height = streams[1]->height) {
      ret = 0;
    } else {
      ret = 1;
    }  
  } else if (num == 3) {
    ret = max(streams[0]->height, streams[1]->height, streams[2]->height);
  }
  return ret;
}
int V4L2Camera::fillStreamInfo(camera3_stream_t * stream) {
  int res = 0;
  // Doesn't matter what was requested, we always use dataspace V0_JFIF.
  // Note: according to camera3.h, this isn't allowed, but the camera
  // framework team claims it's underdocumented; the implementation lets the
  // HAL overwrite it. If this is changed, change the validation above.
  stream->data_space = HAL_DATASPACE_V0_JFIF;

  // Usage: currently using sw graphics.
  switch (stream->stream_type) {
    
    case CAMERA3_STREAM_INPUT:
      stream->usage |= GRALLOC_USAGE_SW_READ_OFTEN;
      break;
    case CAMERA3_STREAM_OUTPUT:
      // Set the usage to GRALLOC_USAGE_SW_WRITE_OFTEN for buffers with cache alloc by gralloc.
      stream->usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
      break;
    case CAMERA3_STREAM_BIDIRECTIONAL:
      stream->usage |=
          (GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
      break;
    default:
      // nothing to do.
      break;
  }
  // Add camera usage. 
  stream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE;

  switch (stream->format) {
    case HAL_PIXEL_FORMAT_BLOB: {
        stream->max_buffers = PICTURE_BUFFER_NUM;
        break;
    }
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED: {
        stream->max_buffers = MAX_BUFFER_NUM;
        break;
    }
    case HAL_PIXEL_FORMAT_YCBCR_420_888: {
        stream->max_buffers = PICTURE_BUFFER_NUM;
        break;
    }
    default: {
      HAL_LOGE("Do not find any format!");
      break;
    }
  }
  return res;
}
int V4L2Camera::findStreamModes(STREAM_SERIAL stream_serial,
    camera3_stream_configuration_t* stream_config, int *isBlob) {

  int mStreamIndex = -1;
  int tmpMaxWI = -1;
  int tmpMaxHI = -1;
  int tmpMax = -1;

  switch (stream_serial) {
    case MAIN_STREAM: {
        if(stream_config->num_streams == 1) {
          mStreamIndex = 0;
        } else if(stream_config->num_streams == 2) { 
          if((stream_config->streams[0]->width * stream_config->streams[0]->height)
            >= (stream_config->streams[1]->width * stream_config->streams[1]->height)) {
            mStreamIndex = 0;
          } else {
            mStreamIndex = 1;
          }
        } else if(stream_config->num_streams == 3) {
          mStreamIndex = max((stream_config->streams[0]->width * stream_config->streams[0]->height),
          (stream_config->streams[1]->width * stream_config->streams[1]->height),
          (stream_config->streams[2]->width * stream_config->streams[2]->height));
        } else {
          HAL_LOGE("Get stream_config->num_streams:%d failed!", stream_config->num_streams);
          break;
        }
        if(stream_config->streams[mStreamIndex]->format == HAL_PIXEL_FORMAT_BLOB) {
          *isBlob = 1;
        } else {
          *isBlob = 0;
        }
        break;
    }
    case SUB_0_STREAM: {
        if(stream_config->num_streams <= 1) {
          HAL_LOGE("Get stream_config->num_streams:%d failed!", stream_config->num_streams);
        } else if(stream_config->num_streams == 2) {
          int maintmp = findStreamModes(MAIN_STREAM, stream_config, isBlob);
          if(maintmp == 1) {
            mStreamIndex = 0;
          } else {
            mStreamIndex = 1;
          }
        } else if(stream_config->num_streams == 3) {
          int maxtmp = max((stream_config->streams[0]->width * stream_config->streams[0]->height),
          (stream_config->streams[1]->width * stream_config->streams[1]->height),
          (stream_config->streams[2]->width * stream_config->streams[2]->height));
          int mintmp = min((stream_config->streams[0]->width * stream_config->streams[0]->height),
          (stream_config->streams[1]->width * stream_config->streams[1]->height),
          (stream_config->streams[2]->width * stream_config->streams[2]->height));

          for(int i = 0;i <stream_config->num_streams; i++) {
            if((i != maxtmp) && (i != mintmp) ) {
              mStreamIndex = i;
            }
          }
        } else {
          HAL_LOGE("Get stream_config->num_streams:%d failed!", stream_config->num_streams);
          break;
        }
        if(stream_config->streams[mStreamIndex]->format == HAL_PIXEL_FORMAT_BLOB) {
          *isBlob = 1;
        } else {
          *isBlob = 0;
        }
        break;
    }
    default: {
      HAL_LOGE("Do not find any stream!");
      break;
    }
  }

  return mStreamIndex;
}

int V4L2Camera::enqueueRequest(
    std::shared_ptr<default_camera_hal::CaptureRequest> request) {
  HAL_LOG_ENTER();
  int res;

  // Be compatible with Camera api 1.
  mRunStreamNum = request->output_buffers.size();
  uint32_t frameNumber;
  {
    std::lock_guard<std::mutex> guard(frameNumber_request_lock_);
    frameNumber = request->frame_number;
    mMapFrameNumRequest.emplace(frameNumber, request);
    rfequest_queue_stream_.push(request);
  }
  mMetadata = &request->settings;

  for(int i = 0; i <request->output_buffers.size(); i++) {
    std::lock_guard<std::mutex> guard(request_queue_stream_lock_);
    const camera3_stream_buffer_t& output = request->output_buffers[i];
    CameraStream *mCameraStream = (CameraStream *)output.stream->priv;
    HAL_LOGD("request %d x %d, format:%d.", output.stream->width, output.stream->height, output.stream->format);
    res = mCameraStream->request(output.buffer, frameNumber, mMetadata, mStreamManager_.get());
    if (res) {
        HAL_LOGE("Fail to request on mCameraStream");
    }
  }

  res = metadata_->SetRequestSettings(*mMetadata);
  if (res) {
    HAL_LOGE("Failed to set settings.");
    //completeRequest(request, res);
    //return true;
  }

  res = metadata_->FillResultMetadata(mMetadata);
  if (res) {
    HAL_LOGE("Failed to fill result metadata.");
    //completeRequest(request, res);
    //return true;
  }

  //requests_available_stream_.notify_one();
  return 0;
}

std::shared_ptr<default_camera_hal::CaptureRequest>
V4L2Camera::dequeueRequest() {
  HAL_LOG_ENTER();

  // Wait. 
  // 1. No request in stream.
  // 2. And exchange to record, record exchange to preview.
  //    a.exchange to record: In preview mode, so pbuffers must be PREPARE_BUFFER, until timeout that mean 
  // in_flight_.size() less than 2 buffers.
  //    b.exchange to preview: In record mode, so vbuffers must be PREPARE_BUFFER, until timeout that mean 
  // in_flight_.size() less than 2 buffers.
  // Jump out while wait. 
  // 1. There are request in stream.
  // 2. Or exchange to record, record exchange to preview.
  //    a.exchange to record: In preview mode, so pbuffers must be PREPARE_BUFFER, until timeout that mean 
  // in_flight_.size() less than 2 buffers.
  //    b.exchange to preview: In record mode, so vbuffers must be PREPARE_BUFFER, until timeout that mean 
  // in_flight_.size() less than 2 buffers.
#if 0
  if(!(((mBeffer_status.pbuffers == PREPARE_BUFFER)||(mBeffer_status.vbuffers == PREPARE_BUFFER))
              &&(in_flight_.size() > 0)
              //&& isVideoByTemplate
              )) {
    while ( request_queue_pstream_.empty()
            && request_queue_tstream_.empty()
            && request_queue_vstream_.empty()
            ) {
      HAL_LOGD("Wait the requests_available_stream_ lock,in_flight_ buffer(s) size:%d.",in_flight_.size());
      requests_available_stream_.wait(lock);
      HAL_LOGD("The lock has been notified.");
    }
  }

  while ( request_queue_pstream_.empty()
          && request_queue_tstream_.empty()
          && request_queue_vstream_.empty()
          ) {
    HAL_LOGD("Wait the requests_available_stream_ lock,in_flight_ buffer(s) size:%d.",in_flight_.size());
    requests_available_stream_.wait(lock);
    HAL_LOGD("The lock has been notified.");
  }
#endif

  HAL_LOGD("The request queue will be pop,there are leave %d in prequest, %d in trequest, %d in vrequest,\
  %d in vhrequest now!",
        request_queue_pstream_.size(),
        request_queue_tstream_.size(),
        request_queue_vstream_.size(),
        request_queue_vhstream_.size());

  std::shared_ptr<default_camera_hal::CaptureRequest> request = nullptr;

  while(1) {
    //{
      std::unique_lock<std::mutex> lock(request_queue_stream_lock_);
      // Only pick one stream.
      if(!request_queue_tstream_.empty()) {
        request = request_queue_tstream_.front();
        request_queue_tstream_.pop();
        break;
      
      } else if(!request_queue_vstream_.empty() && mVideoflag) {
        request = request_queue_vstream_.front();
        request_queue_vstream_.pop();
        break;

      } else if(!request_queue_pstream_.empty() && ((mVideoflag == false)||mRunStreamNum == 1)) { 
        // Think about the stream has only one cache for using, add detect stream number for preview in video mode.
        request = request_queue_pstream_.front();
        request_queue_pstream_.pop();
        break;
      }
    //}
    HAL_LOGD("Wait the requests_available_stream_ lock,in_flight_ buffer(s) size:%d.",in_flight_.size());
    requests_available_stream_.wait(lock);
    HAL_LOGD("The lock has been notified.");

  }

  HAL_LOGD("Stream queue has been pop,there are leave %d in prequest, %d in trequest, %d in vrequest,\
  %d in vhrequest now!",
        request_queue_pstream_.size(),
        request_queue_tstream_.size(),
        request_queue_vstream_.size(),
        request_queue_vhstream_.size());

  return request;
}
void V4L2Camera::ReadyForBuffer() {
  // Wait until something is available before looping again.
  std::unique_lock<std::mutex> lock(in_flight_lock_);
  while(!(((mBeffer_status.pbuffers == PREPARE_BUFFER)||(mBeffer_status.vbuffers == PREPARE_BUFFER))
              &&(in_flight_.size() > 0)
              //&& isVideoByTemplate
              )) {

    HAL_LOGD("Wait the buffers_in_flight_ lock,in_flight_ buffer(s) size:%d.",in_flight_.size());
    buffers_in_flight_.wait(lock);
    HAL_LOGD("The lock has been notified.");
  }
}

bool V4L2Camera::validateDataspacesAndRotations(
    const camera3_stream_configuration_t* stream_config) {
  HAL_LOG_ENTER();

  for (uint32_t i = 0; i < stream_config->num_streams; ++i) {
    if (stream_config->streams[i]->rotation != CAMERA3_STREAM_ROTATION_0) {
      HAL_LOGV("Rotation %d for stream %d not supported",
               stream_config->streams[i]->rotation,
               i);
      return false;
    }
    // Accept all dataspaces, as it will just be overwritten below anyways.
  }
  return true;
}

int V4L2Camera::sResultCallback(uint32_t frameNumber,struct timeval ts) {
  std::lock_guard<std::mutex> guard(frameNumber_request_lock_);
  int res = 0;
  int ret = 0;
  std::shared_ptr<default_camera_hal::CaptureRequest> request = rfequest_queue_stream_.front();
  auto map_entry = mMapFrameNumRequest.find(frameNumber);
  if (map_entry == mMapFrameNumRequest.end()) {
    HAL_LOGE("No matching request for frameNumber:%d, something wrong!", frameNumber);
    return -ENOMEM;
  } else {
    if(request->frame_number != frameNumber) {
      HAL_LOGE("No the successfully front request frameNumber:%d for result frameNumber:%d, something wrong!", request->frame_number, frameNumber);
      wfequest_queue_stream_.push(map_entry->second);
    } else {
      // Fix cts case: android.hardware.camera2.cts.StillCaptureTest#testJpegExif
      // We update the mThumbnailSize and sort for avoid framework sort the metadata 
      // so mThumbnailSize changed.
      std::array<int32_t, 2> mThumbnailSize = {-1, -1};
      ret = SingleTagValue(&map_entry->second->settings,
                              ANDROID_JPEG_THUMBNAIL_SIZE,
                              &mThumbnailSize);
      (&map_entry->second->settings)->sort();
      HAL_LOGD("Before completeRequest mThumbnailSize info:mThumbnailSize:%dx%d.", mThumbnailSize[0], mThumbnailSize[1]);
      if((mThumbnailSize[0] == 0)&&(mThumbnailSize[1] == 0)) {
        int mThumbnailSizet[2] = {0, 0};
        ret = (&map_entry->second->settings)->update(ANDROID_JPEG_THUMBNAIL_SIZE, mThumbnailSizet, 2);
        if (ret) {
          HAL_LOGE("Failed to update metadata tag 0x%x", ANDROID_JPEG_THUMBNAIL_SIZE);
        }
      }

      int64_t timestamp = 0;
      timestamp = ts.tv_sec * 1000000000ULL + ts.tv_usec*1000;
      ret = (&map_entry->second->settings)->update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
      if (ret) {
          HAL_LOGE("Failed to update metadata tag 0x%x", ANDROID_SENSOR_TIMESTAMP);
      }

      completeRequest(map_entry->second, res);
      rfequest_queue_stream_.pop();
      while(!wfequest_queue_stream_.empty()) {
        int64_t timestamp_w = 0;
        timestamp_w = ts.tv_sec * 1000000000ULL + ts.tv_usec*1000;
        ret = (&map_entry->second->settings)->update(ANDROID_SENSOR_TIMESTAMP, &timestamp_w, 1);
        if (ret) {
            HAL_LOGE("Failed to update metadata tag 0x%x", ANDROID_SENSOR_TIMESTAMP);
        }

        completeRequest(wfequest_queue_stream_.front(), res);
        wfequest_queue_stream_.pop();
        rfequest_queue_stream_.pop();
      }
    }
    mMapFrameNumRequest.erase(frameNumber);
  }
  return res;
}

int V4L2Camera::setupStreams(camera3_stream_configuration_t* stream_config) {
  HAL_LOG_ENTER();
  int res = 0;
  // We should flush before change the situation.
  //flush();
  // stream_config should have been validated; assume at least 1 stream.
  if(stream_config->num_streams < 1) {
    HAL_LOGE("Validate the stream numbers, at least 1 stream.");
    return -EINVAL;
  }
  // stream_config should have been validated; do not over MAX_STREAM_NUM.
  if(stream_config->num_streams > MAX_STREAM_NUM) {
    HAL_LOGE("Validate the stream numbers, over the max stream number %d we support.", MAX_STREAM_NUM);
    return -EINVAL;
  }

  //std::lock_guard<std::mutex> guard(stream_lock_);
  // stop stream.
  if(mStreamManager_ != nullptr) {
    HAL_LOGD("Stop stream.");
    for(int ss = 0; ss < MAX_STREAM; ss++) {
      mStreamManager_->stop((STREAM_SERIAL)ss);
    }
  }

  while(!wfequest_queue_stream_.empty()) {
    wfequest_queue_stream_.pop();
  }
  while(!rfequest_queue_stream_.empty()) {
    rfequest_queue_stream_.pop();
  }
  mMapFrameNumRequest.clear();
  mStreamManager_.reset();
  mStreamManager_ = StreamManager::NewStreamManager(device_, instance);


  int numStreamsSet = 0;
  int retIndex = -1;
  int mainIndex = -1;
  int subIndex = -1;
  int thirdIndex = -1;

  for(int j = 0; j <MAX_STREAM; j++) {
    mStreamTracker[j] = false;
  }
  for(int j = 0; j <MAX_STREAM; j++) {
    mSourceStreamTracker[j] = false;
  }

  // Analysis and create stream.
  for (uint32_t i = 0; i < stream_config->num_streams; ++i) {

    int isBlob = 0;
    retIndex = findStreamModes(MAIN_STREAM, stream_config, &isBlob);
    if((retIndex >= 0) && (!mStreamTracker[MAIN_STREAM +isBlob])) {
      HAL_LOGD("Detect the main stream %d is format %d, width %d, height %d, usage %d, stream_type %d, data_space %d, num_streams:%d.",
            retIndex,
            stream_config->streams[retIndex]->format,
            stream_config->streams[retIndex]->width,
            stream_config->streams[retIndex]->height,
            stream_config->streams[retIndex]->usage,
            stream_config->streams[retIndex]->stream_type,
            stream_config->streams[retIndex]->data_space,
            stream_config->num_streams);
      mainIndex = retIndex;

      stream_config->streams[retIndex]->priv =  
        reinterpret_cast<void *> (mStreamManager_->createStream(MAIN_STREAM,
        stream_config->streams[retIndex]->width,
        stream_config->streams[retIndex]->height,
        stream_config->streams[retIndex]->format,
        stream_config->streams[retIndex]->usage,
        isBlob));
      if(nullptr == stream_config->streams[retIndex]->priv) {
        HAL_LOGE("Failed create main stream!");
        return -EINVAL;
      }
      mSourceStreamTracker[retIndex] = true;
      mStreamTracker[MAIN_STREAM +isBlob] = true;
      numStreamsSet++;
      continue;
    }
    isBlob = 0;
    retIndex = findStreamModes(SUB_0_STREAM, stream_config, &isBlob);
    if((retIndex >= 0) && (!mStreamTracker[SUB_0_STREAM +isBlob])) {
      HAL_LOGD("Detect the sub stream %d is format %d, width %d, height %d, usage %d, stream_type %d, data_space %d, num_streams:%d.",
            retIndex,
            stream_config->streams[retIndex]->format,
            stream_config->streams[retIndex]->width,
            stream_config->streams[retIndex]->height,
            stream_config->streams[retIndex]->usage,
            stream_config->streams[retIndex]->stream_type,
            stream_config->streams[retIndex]->data_space,
            stream_config->num_streams);

      subIndex = retIndex;

      stream_config->streams[retIndex]->priv =  
        reinterpret_cast<void *> (mStreamManager_->createStream(SUB_0_STREAM,
        stream_config->streams[retIndex]->width,
        stream_config->streams[retIndex]->height,
        stream_config->streams[retIndex]->format,
        stream_config->streams[retIndex]->usage,
        isBlob));
      if(nullptr == stream_config->streams[retIndex]->priv) {
        HAL_LOGE("Failed create sub stream!");
        return -EINVAL;
      }
      mSourceStreamTracker[retIndex] = true;
      mStreamTracker[SUB_0_STREAM +isBlob] = true;
      numStreamsSet++;
      continue;
    }

    // For third stream
    isBlob = 0;
    //  detect source stream
    for(int k = 0; k <stream_config->num_streams; k++) {
      if(mSourceStreamTracker[k] == false) {
        thirdIndex = k;
        if(stream_config->streams[k]->format == HAL_PIXEL_FORMAT_BLOB) {
          isBlob = 1;
        }
      }
    }

    // We can not deal the blob stream when input source/output source large than 5.
    if((numStreamsSet != stream_config->num_streams)
      && (((stream_config->streams[subIndex]->width/stream_config->streams[thirdIndex]->width) < 5)
          &&((stream_config->streams[subIndex]->height/stream_config->streams[thirdIndex]->height) < 5))
      ){
      // For third blob stream
      for(int j = SUB_0_STREAM_BLOB; j >0; ) {
        HAL_LOGD("In j:%d circle!", j);
        if((!mStreamTracker[j]) && isBlob){
          HAL_LOGD("Find j+isBlob:%d false!", j);
          HAL_LOGD("Detect the third blob stream %d link to %d stream is format %d, width %d, height %d, num_streams:%d.",
                thirdIndex,
                j,
                stream_config->streams[thirdIndex]->format,
                stream_config->streams[thirdIndex]->width,
                stream_config->streams[thirdIndex]->height,
                stream_config->num_streams);

          stream_config->streams[thirdIndex]->priv =  
            reinterpret_cast<void *> (mStreamManager_->createStream((STREAM_SERIAL)(j -1),
            stream_config->streams[thirdIndex]->width,
            stream_config->streams[thirdIndex]->height,
            stream_config->streams[thirdIndex]->format,
            stream_config->streams[thirdIndex]->usage,
            isBlob));

          if(nullptr == stream_config->streams[thirdIndex]->priv) {
            HAL_LOGE("Failed create third stream!");
            return -EINVAL;
          }
          mSourceStreamTracker[thirdIndex] = true;
          mStreamTracker[j] = true;
          numStreamsSet++;
          break;
        }

        if(numStreamsSet == stream_config->num_streams) {
          break;
        }
        j = j -2;
      }
    }

    //  find mirror stream
    if(numStreamsSet != stream_config->num_streams) {
      if (!mStreamTracker[SUB_0_MIRROR_STREAM]) {
        HAL_LOGD("Find SUB_0_MIRROR_STREAM:%d!", SUB_0_MIRROR_STREAM+isBlob);
        HAL_LOGD("Detect the third mirror stream %d link to %d stream is format %d, width %d, height %d, num_streams:%d.",
              thirdIndex,
              SUB_0_MIRROR_STREAM +isBlob,
              stream_config->streams[thirdIndex]->format,
              stream_config->streams[thirdIndex]->width,
              stream_config->streams[thirdIndex]->height,
              stream_config->num_streams);
        
        stream_config->streams[thirdIndex]->priv =  
          reinterpret_cast<void *> (mStreamManager_->createStream((STREAM_SERIAL)SUB_0_MIRROR_STREAM,
          stream_config->streams[thirdIndex]->width,
          stream_config->streams[thirdIndex]->height,
          stream_config->streams[thirdIndex]->format,
          stream_config->streams[thirdIndex]->usage,
          isBlob));
        if((stream_config->streams[subIndex]->width != stream_config->streams[thirdIndex]->width) ||
        (stream_config->streams[subIndex]->height != stream_config->streams[thirdIndex]->height)) {
          res = ((CameraSubMirrorStream *)(stream_config->streams[thirdIndex]->priv))->setScaleFlag();
          if(res) {
            HAL_LOGE("Failed setScaleFlag!");
          }
        }
        if(nullptr == stream_config->streams[thirdIndex]->priv) {
          HAL_LOGE("Failed create third stream!");
          return -EINVAL;
        }
        mSourceStreamTracker[thirdIndex] = true;
        mStreamTracker[SUB_0_MIRROR_STREAM +isBlob] = true;
        numStreamsSet++;

      }
    }
#if 0
    if(numStreamsSet != stream_config->num_streams) {
      // For third stream
      if (!mStreamTracker[SUB_0_MIRROR_STREAM] && 
        (stream_config->streams[subIndex]->width == stream_config->streams[thirdIndex]->width) &&
        (stream_config->streams[subIndex]->height == stream_config->streams[thirdIndex]->height)) {
        HAL_LOGD("Find MAIN_MIRROR_STREAM:%d!", SUB_0_MIRROR_STREAM+isBlob);
        HAL_LOGD("Detect the third mirror stream %d link to %d stream is format %d, width %d, height %d, num_streams:%d.",
              thirdIndex,
              SUB_0_MIRROR_STREAM +isBlob,
              stream_config->streams[thirdIndex]->format,
              stream_config->streams[thirdIndex]->width,
              stream_config->streams[thirdIndex]->height,
              stream_config->num_streams);

        stream_config->streams[thirdIndex]->priv =  
          reinterpret_cast<void *> (mStreamManager_->createStream((STREAM_SERIAL)SUB_0_MIRROR_STREAM,
          stream_config->streams[thirdIndex]->width,
          stream_config->streams[thirdIndex]->height,
          stream_config->streams[thirdIndex]->format,
          stream_config->streams[thirdIndex]->usage,
          isBlob));
        
        if(nullptr == stream_config->streams[thirdIndex]->priv) {
          HAL_LOGE("Failed create third stream!");
          return -EINVAL;
        }
        mSourceStreamTracker[thirdIndex] = true;
        mStreamTracker[SUB_0_MIRROR_STREAM +isBlob] = true;
        numStreamsSet++;
    
      }else if(!mStreamTracker[MAIN_MIRROR_STREAM] && 
        (stream_config->streams[mainIndex]->width == stream_config->streams[thirdIndex]->width) &&
        (stream_config->streams[mainIndex]->height == stream_config->streams[thirdIndex]->height)){
        HAL_LOGD("Find MAIN_MIRROR_STREAM:%d!", MAIN_MIRROR_STREAM+isBlob);
        HAL_LOGD("Detect the third mirror stream %d link to %d stream is format %d, width %d, height %d, num_streams:%d.",
              thirdIndex,
              MAIN_MIRROR_STREAM +isBlob,
              stream_config->streams[thirdIndex]->format,
              stream_config->streams[thirdIndex]->width,
              stream_config->streams[thirdIndex]->height,
              stream_config->num_streams);
    
        stream_config->streams[thirdIndex]->priv =  
          reinterpret_cast<void *> (mStreamManager_->createStream((STREAM_SERIAL)MAIN_MIRROR_STREAM,
          stream_config->streams[thirdIndex]->width,
          stream_config->streams[thirdIndex]->height,
          stream_config->streams[thirdIndex]->format,
          stream_config->streams[thirdIndex]->usage,
          isBlob));
    
        if(nullptr == stream_config->streams[thirdIndex]->priv) {
          HAL_LOGE("Failed create third stream!");
          return -EINVAL;
        }
        mSourceStreamTracker[thirdIndex] = true;
        mStreamTracker[MAIN_MIRROR_STREAM +isBlob] = true;
        numStreamsSet++;
      }
    }
#endif

  }

  HAL_LOGD("Configurate stream Manager.");
  if(mStreamManager_ != nullptr) {
    for(int ss = 0; ss < MAX_STREAM; ss++) {
      res = mStreamManager_->configurateManager((STREAM_SERIAL)ss);
      if(res) {
        HAL_LOGE("Configurate stream Manager failed.");
      }
    }
  }

  HAL_LOGD("Start stream.");
  // start stream.
  if(mStreamManager_ != nullptr) {
    for(int ss = 0; ss < MAX_STREAM; ss++) {
      mStreamManager_->start((STREAM_SERIAL)ss);
    }
  }

  // Fill some stream info: stream->usage, stream->max_buffers, stream->data_space.
  for (uint32_t i = 0; i < stream_config->num_streams; ++i) {
    fillStreamInfo(stream_config->streams[i]);
  }
  for (uint32_t i = 0; i < stream_config->num_streams; ++i) {
    HAL_LOGD("stream %d is format %d, width %d, height %d, usage %d, stream_type %d, data_space %d, num_streams:%d.",
          i,
          stream_config->streams[i]->format,
          stream_config->streams[i]->width,
          stream_config->streams[i]->height,
          stream_config->streams[i]->usage,
          stream_config->streams[i]->stream_type,
          stream_config->streams[i]->data_space,
          stream_config->num_streams);
  }

  return 0;
}

bool V4L2Camera::isValidRequestSettings(
    const CameraMetadata& settings) {
  if (!metadata_->IsValidRequest(settings)) {
    HAL_LOGE("Invalid request settings.");
    return false;
  }
  return true;
}

}  // namespace v4l2_camera_hal
