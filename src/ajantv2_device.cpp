#include "ajantv2_device.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <ntv2publicinterface.h>

using namespace godot;

AJADevice::AJADevice() {}

AJADevice::~AJADevice() {}

void AJADevice::_bind_methods() {
  ClassDB::bind_method(D_METHOD("get_device_index"),
                       &AJADevice::get_device_index);
  ClassDB::bind_method(D_METHOD("get_display_name"),
                       &AJADevice::get_display_name);
  ClassDB::bind_method(D_METHOD("get_model_name"), &AJADevice::get_model_name);
  ClassDB::bind_method(D_METHOD("get_num_video_inputs"),
                       &AJADevice::get_num_video_inputs);
  ClassDB::bind_method(D_METHOD("get_num_video_outputs"),
                       &AJADevice::get_num_video_outputs);
  ClassDB::bind_method(D_METHOD("can_capture"), &AJADevice::can_capture);
  ClassDB::bind_method(D_METHOD("can_playback"), &AJADevice::can_playback);
  ClassDB::bind_method(D_METHOD("to_dictionary"), &AJADevice::to_dictionary);
  ClassDB::bind_method(D_METHOD("get_video_formats"),
                       &AJADevice::get_video_formats);
  ClassDB::bind_method(D_METHOD("get_pixel_formats"),
                       &AJADevice::get_pixel_formats);
  ClassDB::bind_method(
      D_METHOD("can_output_pixel_format", "channel", "pixel_format"),
      &AJADevice::can_output_pixel_format);
}

void AJADevice::setup(ULWord p_device_index) {
  _device_index = p_device_index;

  CNTV2Card card;
  if (!CNTV2DeviceScanner::GetDeviceAtIndex(p_device_index, card)) {
    return;
  }

  _display_name = aja::string_to_godot(card.GetDisplayName());
  _model_name = aja::string_to_godot(card.GetModelName());
  _num_video_inputs = (int)card.features().GetNumVideoInputs();
  _num_video_outputs = (int)card.features().GetNumVideoOutputs();
  _can_capture = card.features().CanDoCapture();
  _can_playback = card.features().CanDoPlayback();
  _supports_8bit_ycbcr =
      card.features().CanDoFrameBufferFormat(NTV2_FBF_8BIT_YCBCR);
  _supports_abgr = card.features().CanDoFrameBufferFormat(NTV2_FBF_ABGR);
  _num_cscs = (int)card.features().GetNumCSCs();

  // Enumerate supported video formats
  NTV2VideoFormatSet formats;
  NTV2DeviceGetSupportedVideoFormats(card.GetDeviceID(), formats);
  _video_formats.clear();
  for (const NTV2VideoFormat fmt : formats) {
    Dictionary d;
    d["id"] = (int64_t)fmt;
    d["name"] = aja::string_to_godot(::NTV2VideoFormatToString(fmt));
    NTV2FormatDescriptor fd(fmt, NTV2_FBF_ABGR);
    d["width"] = (int64_t)fd.GetRasterWidth();
    d["height"] = (int64_t)fd.GetRasterHeight();
    d["progressive"] = (bool)IsProgressivePicture(fmt);
    _video_formats.push_back(d);
  }
}

ULWord AJADevice::get_device_index() const { return _device_index; }

String AJADevice::get_display_name() const { return _display_name; }

String AJADevice::get_model_name() const { return _model_name; }

int AJADevice::get_num_video_inputs() const { return _num_video_inputs; }

int AJADevice::get_num_video_outputs() const { return _num_video_outputs; }

bool AJADevice::can_capture() const { return _can_capture; }

bool AJADevice::can_playback() const { return _can_playback; }

Dictionary AJADevice::to_dictionary() const {
  Dictionary d;
  d["display_name"] = _display_name;
  d["model_name"] = _model_name;
  d["num_video_inputs"] = _num_video_inputs;
  d["num_video_outputs"] = _num_video_outputs;
  d["can_capture"] = _can_capture;
  d["can_playback"] = _can_playback;
  return d;
}

Array AJADevice::get_video_formats() const { return _video_formats; }

Array AJADevice::get_pixel_formats() const {
  Array formats;
  if (_supports_8bit_ycbcr) {
    Dictionary ycbcr;
    ycbcr["id"] = (int64_t)NTV2_FBF_8BIT_YCBCR;
    ycbcr["name"] = "8-bit YCbCr (UYVY)";
    formats.push_back(ycbcr);
  }
  if (_supports_abgr) {
    Dictionary abgr;
    abgr["id"] = (int64_t)NTV2_FBF_ABGR;
    abgr["name"] = "ABGR";
    formats.push_back(abgr);
  }
  return formats;
}

bool AJADevice::can_output_pixel_format(int p_channel,
                                        int64_t p_pixel_format) const {
  if (p_pixel_format == aja::PIXEL_FORMAT_AUTO) {
    return _supports_8bit_ycbcr ||
           (_supports_abgr && p_channel >= 0 && p_channel < _num_cscs);
  }
  if (p_pixel_format == aja::PIXEL_FORMAT_8BIT_YCBCR) {
    return _supports_8bit_ycbcr;
  }
  if (p_pixel_format == aja::PIXEL_FORMAT_ABGR) {
    return _supports_abgr && p_channel >= 0 && p_channel < _num_cscs;
  }
  return false;
}
