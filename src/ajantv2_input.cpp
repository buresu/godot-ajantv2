#include "ajantv2_input.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include <ajabase/system/process.h>
#include <ntv2signalrouter.h>

#include "ajantv2.hpp"

using namespace godot;

// Convert an AJA UYVY (8-bit 4:2:2) frame buffer to RGBA in-place.
// src_uyvy  = UYVY interleaved, row_bytes per line
// dst_rgba  = output RGBA8 (R,G,B,A bytes), width*4 per line
static bool convert_uyvy_to_rgba(const uint8_t *src_uyvy, int row_bytes,
                                 uint8_t *dst_rgba, int width, int height) {
  // libyuv: UYVYToARGB → BGRA in memory (libyuv "ARGB")
  // Then ARGBToABGR converts BGRA → RGBA in memory (libyuv "ABGR" = Godot RGBA8)
  std::vector<uint8_t> argb(width * height * 4);
  if (libyuv::UYVYToARGB(src_uyvy, row_bytes, argb.data(), width * 4, width,
                         height) != 0) {
    return false;
  }
  return libyuv::ARGBToABGR(argb.data(), width * 4, dst_rgba, width * 4, width,
                             height) == 0;
}

AJAInput::AJAInput() { _frame_mutex = memnew(Mutex); }

AJAInput::~AJAInput() {
  close();
  memdelete(_frame_mutex);
  _frame_mutex = nullptr;
}

void AJAInput::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "device", "channel", "video_format"),
                       &AJAInput::open, DEFVAL((int64_t)NTV2_FORMAT_UNKNOWN));
  ClassDB::bind_method(D_METHOD("close"), &AJAInput::close);
  ClassDB::bind_method(D_METHOD("is_open"), &AJAInput::is_open);
  ClassDB::bind_method(D_METHOD("is_enabled"), &AJAInput::is_enabled);
  ClassDB::bind_method(D_METHOD("set_enabled", "enabled"),
                       &AJAInput::set_enabled);
  ClassDB::bind_method(D_METHOD("get_device"), &AJAInput::get_device);
  ClassDB::bind_method(D_METHOD("set_device", "device"), &AJAInput::set_device);
  ClassDB::bind_method(D_METHOD("get_channel"), &AJAInput::get_channel);
  ClassDB::bind_method(D_METHOD("set_channel", "channel"),
                       &AJAInput::set_channel);
  ClassDB::bind_method(D_METHOD("get_texture"), &AJAInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "texture"),
                       &AJAInput::set_texture);
  ClassDB::bind_method(D_METHOD("has_frame"), &AJAInput::has_frame);
  ClassDB::bind_method(D_METHOD("get_width"), &AJAInput::get_width);
  ClassDB::bind_method(D_METHOD("get_height"), &AJAInput::get_height);

  ClassDB::add_property("AJAInput", {Variant::BOOL, "enabled"}, "set_enabled",
                        "is_enabled");
  ClassDB::add_property("AJAInput",
                        {Variant::INT, "device", PROPERTY_HINT_ENUM},
                        "set_device", "get_device");
  ClassDB::add_property("AJAInput",
                        {Variant::INT, "channel", PROPERTY_HINT_ENUM},
                        "set_channel", "get_channel");
  ClassDB::add_property(
      "AJAInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "ImageTexture"},
      "set_texture", "get_texture");
}

void AJAInput::_ready() {
  set_process(true);
  if (_enabled) {
    set_enabled(true);
  }
}

void AJAInput::_process(double p_delta) {
  (void)p_delta;
  _update_texture();
}

void AJAInput::_exit_tree() { close(); }

void AJAInput::_validate_property(PropertyInfo &p_property) const {
  const String name = p_property.name;
  if (name == "device") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_device_hint_string();
  } else if (name == "channel") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_channel_hint_string();
  }
}

bool AJAInput::open(int p_device, int p_channel, int64_t p_video_format) {
  close();
  _device = p_device;
  _channel = p_channel;

  if (!CNTV2DeviceScanner::GetDeviceAtIndex((ULWord)p_device, _card)) {
    UtilityFunctions::printerr("[AJAInput] Could not open device ", p_device);
    return false;
  }

  if (!_card.IsDeviceReady()) {
    UtilityFunctions::printerr("[AJAInput] Device ", p_device, " not ready");
    close();
    return false;
  }

  if (!_card.features().CanDoCapture()) {
    UtilityFunctions::printerr("[AJAInput] Device ", p_device,
                               " does not support capture");
    close();
    return false;
  }

  if (!_card.AcquireStreamForApplication(aja::kAppSignature,
                                         int32_t(AJAProcess::GetPid()))) {
    UtilityFunctions::printerr("[AJAInput] Could not acquire device ",
                               p_device);
    close();
    return false;
  }
  _card.SetTaskMode(NTV2_OEM_TASKS);

  const NTV2Channel channel = aja::index_to_channel(_channel);
  const NTV2InputSource input_source = aja::channel_to_input_source(channel);

  // Configure bidirectional SDI connector as input.
  if (_card.features().HasBiDirectionalSDI() &&
      NTV2_INPUT_SOURCE_IS_SDI(input_source)) {
    _card.SetSDITransmitEnable(channel, false);
    _card.WaitForOutputVerticalInterrupt(NTV2_CHANNEL1, 10);
  }

  // Auto-detect or use specified video format.
  NTV2VideoFormat fmt = (p_video_format != NTV2_FORMAT_UNKNOWN)
                            ? (NTV2VideoFormat)p_video_format
                            : _card.GetInputVideoFormat(input_source);
  if (!NTV2_IS_VALID_VIDEO_FORMAT(fmt)) {
    UtilityFunctions::printerr("[AJAInput] No valid input signal on device ",
                               p_device, " channel ", _channel);
    close();
    return false;
  }
  _video_format = fmt;
  _card.SetVideoFormat(_video_format, false, false, channel);
  _card.SetReference(NTV2_REFERENCE_FREERUN);

  // Select pixel format: prefer ABGR (= Godot RGBA8, needs CSC) else UYVY.
  const bool has_csc = (_card.features().GetNumCSCs() > 0);
  _pixel_format = has_csc ? NTV2_FBF_ABGR : NTV2_FBF_8BIT_YCBCR;

  // Get raster dimensions.
  NTV2FormatDescriptor fd(_video_format, _pixel_format);
  _width = (int)fd.GetRasterWidth();
  _height = (int)fd.GetRasterHeight();

  // Set up signal routing.
  _card.ClearRouting();
  if (_pixel_format == NTV2_FBF_ABGR) {
    // SDI → CSC (YCbCr→RGB) → FrameStore
    _card.Connect(::GetCSCInputXptFromChannel(channel),
                  ::GetSDIInputOutputXptFromChannel(channel));
    _card.Connect(::GetFrameStoreInputXptFromChannel(channel),
                  ::GetCSCOutputXptFromChannel(channel, false, true));
  } else {
    // SDI → FrameStore (raw YCbCr)
    _card.Connect(::GetFrameStoreInputXptFromChannel(channel),
                  ::GetSDIInputOutputXptFromChannel(channel));
  }
  _card.SetFrameBufferFormat(channel, _pixel_format);
  _card.EnableChannel(channel);
  _card.SetSDIInLevelBtoLevelAConversion(channel, false);

  // Subscribe to input vertical event.
  _card.EnableInputInterrupt(channel);
  _card.SubscribeInputVerticalEvent(channel);

  // Initialise AutoCirculate (3 device frame buffers, no audio).
  _card.AutoCirculateStop(channel);
  if (!_card.AutoCirculateInitForInput(channel, 3, NTV2_AUDIOSYSTEM_INVALID,
                                       0)) {
    UtilityFunctions::printerr(
        "[AJAInput] AutoCirculateInitForInput failed for device ", p_device,
        " channel ", _channel);
    close();
    return false;
  }

  {
    MutexLock lock(*_frame_mutex);
    _latest_rgba.resize(_width * _height * 4);
    _has_frame = false;
    _texture_dirty = false;
  }

  _start_capture_thread();

  if (!_card.AutoCirculateStart(channel)) {
    UtilityFunctions::printerr("[AJAInput] AutoCirculateStart failed");
    close();
    return false;
  }

  _open = true;
  _enabled = true;
  return true;
}

void AJAInput::close() {
  _stop_capture_thread();

  if (_card.IsOpen()) {
    const NTV2Channel channel = aja::index_to_channel(_channel);
    _card.AutoCirculateStop(channel);
    _card.UnsubscribeInputVerticalEvent(channel);
    _card.ReleaseStreamForApplication(aja::kAppSignature,
                                      int32_t(AJAProcess::GetPid()));
    _card.Close();
  }

  {
    MutexLock lock(*_frame_mutex);
    _latest_rgba.clear();
    _has_frame = false;
    _texture_dirty = false;
  }

  _open = false;
  _enabled = false;
  _width = 0;
  _height = 0;
}

bool AJAInput::is_open() const { return _open; }

bool AJAInput::is_enabled() const { return _enabled; }

void AJAInput::set_enabled(bool p_enabled) {
  if (_enabled == p_enabled && _open == p_enabled) {
    return;
  }
  if (p_enabled) {
    if (!open(_device, _channel)) {
      _enabled = false;
    }
  } else {
    close();
  }
}

int AJAInput::get_device() const { return _device; }

void AJAInput::set_device(int p_device) {
  if (_device == p_device) {
    return;
  }
  _device = p_device;
  notify_property_list_changed();
  _restart_if_enabled();
}

int AJAInput::get_channel() const { return _channel; }

void AJAInput::set_channel(int p_channel) {
  if (_channel == p_channel) {
    return;
  }
  _channel = p_channel;
  _restart_if_enabled();
}

Ref<ImageTexture> AJAInput::get_texture() const { return _texture; }

void AJAInput::set_texture(Ref<ImageTexture> p_texture) {
  _texture = p_texture;
  MutexLock lock(*_frame_mutex);
  if (!_texture.is_null() && _has_frame) {
    _texture_dirty = true;
  }
}

bool AJAInput::has_frame() const {
  MutexLock lock(*_frame_mutex);
  return _has_frame;
}

int AJAInput::get_width() const { return _width; }

int AJAInput::get_height() const { return _height; }

void AJAInput::_capture_thread_loop() {
  const NTV2Channel channel = aja::index_to_channel(_channel);
  AUTOCIRCULATE_TRANSFER xfer;

  PackedByteArray transfer_buf;
  transfer_buf.resize(_width * _height * (_pixel_format == NTV2_FBF_ABGR
                                               ? 4
                                               : 2)); // ABGR=4B, UYVY=2B/px
  xfer.SetVideoBuffer(reinterpret_cast<PULWord>(transfer_buf.ptrw()),
                      (ULWord)transfer_buf.size());

  OS *os = OS::get_singleton();
  while (!_is_thread_stop_requested()) {
    AUTOCIRCULATE_STATUS status;
    _card.AutoCirculateGetStatus(channel, status);

    if (status.IsRunning() && status.HasAvailableInputFrame()) {
      _card.AutoCirculateTransfer(channel, xfer);

      const uint8_t *src =
          reinterpret_cast<const uint8_t *>(transfer_buf.ptr());
      const int w = _width;
      const int h = _height;

      PackedByteArray rgba;
      rgba.resize(w * h * 4);
      uint8_t *dst = rgba.ptrw();
      bool ok = false;

      if (_pixel_format == NTV2_FBF_ABGR) {
        // ABGR in memory = R,G,B,A = Godot RGBA8. Direct copy.
        memcpy(dst, src, (size_t)(w * h * 4));
        ok = true;
      } else {
        // UYVY → RGBA via libyuv
        ok = convert_uyvy_to_rgba(src, w * 2, dst, w, h);
      }

      if (ok) {
        MutexLock lock(*_frame_mutex);
        _latest_rgba = rgba;
        _has_frame = true;
        _texture_dirty = true;
      }
    } else if (os) {
      os->delay_usec(1000);
    } else {
      break;
    }
  }
}

void AJAInput::_start_capture_thread() {
  if (_capture_thread.is_valid() && _capture_thread->is_started()) {
    return;
  }
  {
    MutexLock lock(*_frame_mutex);
    _thread_stop_requested = false;
  }
  _capture_thread.instantiate();
  const Error err = _capture_thread->start(
      callable_mp(this, &AJAInput::_capture_thread_loop), Thread::PRIORITY_HIGH);
  if (err != OK) {
    UtilityFunctions::printerr("[AJAInput] Could not start capture thread: ",
                               (int64_t)err);
    _capture_thread.unref();
  }
}

void AJAInput::_stop_capture_thread() {
  if (_capture_thread.is_null()) {
    return;
  }
  {
    MutexLock lock(*_frame_mutex);
    _thread_stop_requested = true;
  }
  if (_capture_thread->is_started()) {
    _capture_thread->wait_to_finish();
  }
  _capture_thread.unref();
}

bool AJAInput::_is_thread_stop_requested() const {
  MutexLock lock(*_frame_mutex);
  return _thread_stop_requested;
}

void AJAInput::_update_texture() {
  if (_texture.is_null()) {
    return;
  }
  PackedByteArray rgba;
  int width = 0;
  int height = 0;
  {
    MutexLock lock(*_frame_mutex);
    if (!_texture_dirty || !_has_frame || _latest_rgba.is_empty()) {
      return;
    }
    rgba = _latest_rgba;
    width = _width;
    height = _height;
    _texture_dirty = false;
  }

  if (width <= 0 || height <= 0) {
    return;
  }

  Ref<Image> image =
      Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, rgba);
  if (image.is_null()) {
    return;
  }

  if (_texture->get_width() != width || _texture->get_height() != height ||
      _texture->get_format() != Image::FORMAT_RGBA8) {
    _texture->set_image(image);
  } else {
    _texture->update(image);
  }
}

void AJAInput::_restart_if_enabled() {
  if (!_enabled) {
    return;
  }
  close();
  if (!open(_device, _channel)) {
    _enabled = false;
  }
}

String AJAInput::_get_device_hint_string() const {
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  if (!aja) {
    return "Device 0:0";
  }
  const Array devices = aja->get_devices();
  String hint;
  for (int i = 0; i < devices.size(); ++i) {
    const Dictionary d = devices[i];
    String name = d.get("display_name", String());
    if (name.is_empty()) {
      name = d.get("model_name", String());
    }
    if (name.is_empty()) {
      name = "Device " + String::num_int64(i);
    }
    name = name.replace(",", " ").replace(":", " ");
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += name + ":" + String::num_int64(i);
  }
  return hint.is_empty() ? "Device 0:0" : hint;
}

String AJAInput::_get_channel_hint_string() const {
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  const int num_inputs =
      device.is_valid() ? device->get_num_video_inputs() : 1;
  String hint;
  for (int i = 0; i < num_inputs; ++i) {
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += "Channel " + String::num_int64(i + 1) + ":" + String::num_int64(i);
  }
  return hint.is_empty() ? "Channel 1:0" : hint;
}
