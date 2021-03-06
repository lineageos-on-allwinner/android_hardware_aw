
#ifndef V4L2_CAMERA_HAL_METADATA_PROPERTY_H_
#define V4L2_CAMERA_HAL_METADATA_PROPERTY_H_

#include "../common.h"
#include "metadata_common.h"
#include "partial_metadata_interface.h"

namespace v4l2_camera_hal {

// A Property is a PartialMetadata that only has a single static tag.
template <typename T>
class Property : public PartialMetadataInterface {
 public:
  Property(int32_t tag, T value) : tag_(tag), value_(std::move(value)){};

  virtual std::vector<int32_t> StaticTags() const override { return {tag_}; };

  virtual std::vector<int32_t> ControlTags() const override { return {}; };

  virtual std::vector<int32_t> DynamicTags() const override { return {}; };

  virtual int PopulateStaticFields(
      CameraMetadata* metadata) const override {
    return UpdateMetadata(metadata, tag_, value_);
  };

  virtual int PopulateDynamicFields(
      CameraMetadata* metadata) const override {
    return 0;
  };

  virtual int PopulateTemplateRequest(
      int template_type, CameraMetadata* metadata) const override {
    return 0;
  };

  virtual bool SupportsRequestValues(
      const CameraMetadata& metadata) const override {
    return true;
  };

  virtual int SetRequestValues(
      const CameraMetadata& metadata) override {
    return 0;
  };

 private:
  int32_t tag_;
  T value_;
};

}  // namespace v4l2_camera_hal

#endif  // V4L2_CAMERA_HAL_METADATA_PROPERTY_H_
