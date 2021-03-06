
#ifndef V4L2_CAMERA_HAL_STREAM_FORMAT_H_
#define V4L2_CAMERA_HAL_STREAM_FORMAT_H_

#include <string.h>

#include <linux/videodev2.h>

#include "common.h"

namespace v4l2_camera_hal {

enum FormatCategory {
  kFormatCategoryRaw,
  kFormatCategoryStalling,
  kFormatCategoryNonStalling,
  kFormatCategoryUnknown,
};

class StreamFormat {
 public:
  StreamFormat(int format, uint32_t width, uint32_t height);
  StreamFormat(const v4l2_format& format);
  virtual ~StreamFormat() = default;
  // Only uint32_t members, use default generated copy and assign.

  void FillFormatRequest(v4l2_format* format) const;
  FormatCategory Category() const;

  // Accessors.
  inline uint32_t type() const { return type_; };
  inline uint32_t memory() const { return memory_; };
  inline uint32_t nplanes() const { return nplanes_; };
  inline uint32_t width() const { return width_; };
  inline uint32_t height() const { return height_; };
  inline uint32_t bytes_per_line() const { return bytes_per_line_; };

  bool operator==(const StreamFormat& other) const;
  bool operator!=(const StreamFormat& other) const;

  // HAL <-> V4L2 conversions
  // Returns 0 for unrecognized.
  static uint32_t HalToV4L2PixelFormat(int hal_pixel_format);
  // Returns -1 for unrecognized.
  static int V4L2ToHalPixelFormat(uint32_t v4l2_pixel_format);

 private:
  uint32_t type_;
  uint32_t memory_;
  uint32_t nplanes_;
  uint32_t v4l2_pixel_format_;
  uint32_t width_;
  uint32_t height_;
  uint32_t bytes_per_line_;
  uint32_t min_buffer_size_;
};

}  // namespace v4l2_camera_hal

#endif  // V4L2_CAMERA_HAL_STREAM_FORMAT_H_
