#include "ajantv2_output.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include <ajabase/system/process.h>
#include <ntv2signalrouter.h>

#include "ajantv2.hpp"

using namespace godot;

AJAOutput::AJAOutput() { _output_mutex = memnew(Mutex); }

AJAOutput::~AJAOutput() {
  close();
  memdelete(_output_mutex);
  _output_mutex = nullptr;
}

void AJAOutput::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "device", "channel", "video_format"),
                       &AJAOutput::open);
  ClassDB::bind_method(D_METHOD("close"), &AJAOutput::close);
  ClassDB::bind_method(D_METHOD("is_open"), &AJAOutput::is_open);
  ClassDB::bind_method(D_METHOD("is_enabled"), &AJAOutput::is_enabled);
  ClassDB::bind_method(D_METHOD("set_enabled", "enabled"),
                       &AJAOutput::set_enabled);
  ClassDB::bind_method(D_METHOD("get_device"), &AJAOutput::get_device);
  ClassDB::bind_method(D_METHOD("set_device", "device"),
                       &AJAOutput::set_device);
  ClassDB::bind_method(D_METHOD("get_channel"), &AJAOutput::get_channel);
  ClassDB::bind_method(D_METHOD("set_channel", "channel"),
                       &AJAOutput::set_channel);
  ClassDB::bind_method(D_METHOD("get_video_format"),
                       &AJAOutput::get_video_format);
  ClassDB::bind_method(D_METHOD("set_video_format", "video_format"),
                       &AJAOutput::set_video_format);
  ClassDB::bind_method(D_METHOD("get_texture"), &AJAOutput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "texture"),
                       &AJAOutput::set_texture);
  ClassDB::bind_method(D_METHOD("get_width"), &AJAOutput::get_width);
  ClassDB::bind_method(D_METHOD("get_height"), &AJAOutput::get_height);

  ClassDB::add_property("AJAOutput", {Variant::BOOL, "enabled"}, "set_enabled",
                        "is_enabled");
  ClassDB::add_property(
      "AJAOutput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"},
      "set_texture", "get_texture");
  ClassDB::add_property("AJAOutput",
                        {Variant::INT, "device", PROPERTY_HINT_ENUM},
                        "set_device", "get_device");
  ClassDB::add_property("AJAOutput",
                        {Variant::INT, "channel", PROPERTY_HINT_ENUM},
                        "set_channel", "get_channel");
  ClassDB::add_property("AJAOutput",
                        {Variant::INT, "video_format", PROPERTY_HINT_ENUM},
                        "set_video_format", "get_video_format");
}

void AJAOutput::_ready() {
  if (_enabled) {
    set_enabled(true);
  }
}

void AJAOutput::_exit_tree() {
  _disconnect_frame_post_draw();
  close();
}

void AJAOutput::_validate_property(PropertyInfo &p_property) const {
  const String name = p_property.name;
  if (name == "device") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_device_hint_string();
  } else if (name == "channel") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_channel_hint_string();
  } else if (name == "video_format") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_video_format_hint_string();
  }
}

bool AJAOutput::open(int p_device, int p_channel, int64_t p_video_format) {
  close();
  _device = p_device;
  _channel = p_channel;
  _video_format = (NTV2VideoFormat)p_video_format;

  if (!CNTV2DeviceScanner::GetDeviceAtIndex((ULWord)p_device, _card)) {
    UtilityFunctions::printerr("[AJAOutput] Could not open device ", p_device);
    return false;
  }

  if (!_card.IsDeviceReady()) {
    UtilityFunctions::printerr("[AJAOutput] Device ", p_device, " not ready");
    close();
    return false;
  }

  if (!_card.features().CanDoPlayback()) {
    UtilityFunctions::printerr("[AJAOutput] Device ", p_device,
                               " does not support playback");
    close();
    return false;
  }

  if (!_card.AcquireStreamForApplication(aja::kAppSignature,
                                         int32_t(AJAProcess::GetPid()))) {
    UtilityFunctions::printerr("[AJAOutput] Could not acquire device ",
                               p_device);
    close();
    return false;
  }
  _card.SetTaskMode(NTV2_OEM_TASKS);

  const NTV2Channel channel = aja::index_to_channel(_channel);

  // Choose pixel format: ABGR (=Godot RGBA8 in memory, needs CSC for SDI)
  // or fall back to UYVY.
  const bool has_csc = ((int)channel < (int)_card.features().GetNumCSCs());
  _pixel_format = has_csc ? NTV2_FBF_ABGR : NTV2_FBF_8BIT_YCBCR;

  // Validate video format.
  if (!_card.features().CanDoVideoFormat(_video_format)) {
    UtilityFunctions::printerr(
        "[AJAOutput] Device does not support video format ",
        (int64_t)_video_format);
    close();
    return false;
  }

  NTV2FormatDescriptor fd(_video_format, _pixel_format);
  _width = (int)fd.GetRasterWidth();
  _height = (int)fd.GetRasterHeight();

  _card.DisableChannel(channel);
  _card.SetVANCMode(NTV2_VANCMODE_OFF, channel);
  _card.SetVideoFormat(_video_format, false, false, channel);
  _card.SetFrameBufferFormat(channel, _pixel_format);
  _card.EnableChannel(channel);
  _card.SubscribeOutputVerticalEvent(channel);
  _card.SetReference(NTV2_REFERENCE_FREERUN);

  // Set up signal routing.
  _card.ClearRouting();
  if (_pixel_format == NTV2_FBF_ABGR) {
    // FrameStore → CSC (RGB→YCbCr) → SDI output
    _card.Connect(::GetCSCInputXptFromChannel(channel),
                  ::GetFrameStoreOutputXptFromChannel(channel));
    _card.Connect(::GetSDIOutputInputXptFromChannel(channel),
                  ::GetCSCOutputXptFromChannel(channel, false, false));
  } else {
    // FrameStore → SDI output (raw YCbCr)
    _card.Connect(::GetSDIOutputInputXptFromChannel(channel),
                  ::GetFrameStoreOutputXptFromChannel(channel));
  }

  // Initialise AutoCirculate for output (3 device frame buffers, no audio).
  _card.AutoCirculateStop(channel);
  if (!_card.AutoCirculateInitForOutput(channel, 3, NTV2_AUDIOSYSTEM_INVALID,
                                        0)) {
    UtilityFunctions::printerr(
        "[AJAOutput] AutoCirculateInitForOutput failed for device ", p_device,
        " channel ", _channel);
    close();
    return false;
  }

  {
    MutexLock lock(*_output_mutex);
    _latest_rgba.clear();
    _has_frame = false;
  }

  _open = true;
  return true;
}

void AJAOutput::close() {
  _stop_output_thread();

  if (_card.IsOpen()) {
    const NTV2Channel channel = aja::index_to_channel(_channel);
    _card.AutoCirculateStop(channel);
    _card.UnsubscribeOutputVerticalEvent(channel);
    _card.ReleaseStreamForApplication(aja::kAppSignature,
                                      int32_t(AJAProcess::GetPid()));
    _card.Close();
  }

  {
    MutexLock lock(*_output_mutex);
    _latest_rgba.clear();
    _has_frame = false;
  }

  _open = false;
  _width = 0;
  _height = 0;
}

bool AJAOutput::is_open() const { return _open; }

bool AJAOutput::is_enabled() const { return _enabled; }

void AJAOutput::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  if (_enabled) {
    if (!_open && !open(_device, _channel, (int64_t)_video_format)) {
      _enabled = false;
      return;
    }
    _start_output_thread();
    _connect_frame_post_draw();
  } else {
    _disconnect_frame_post_draw();
    close();
  }
}

int AJAOutput::get_device() const { return _device; }

void AJAOutput::set_device(int p_device) {
  if (_device == p_device) {
    return;
  }
  _device = p_device;
  notify_property_list_changed();
  _restart_if_enabled();
}

int AJAOutput::get_channel() const { return _channel; }

void AJAOutput::set_channel(int p_channel) {
  if (_channel == p_channel) {
    return;
  }
  _channel = p_channel;
  _restart_if_enabled();
}

int64_t AJAOutput::get_video_format() const { return (int64_t)_video_format; }

void AJAOutput::set_video_format(int64_t p_video_format) {
  if (_video_format == (NTV2VideoFormat)p_video_format) {
    return;
  }
  _video_format = (NTV2VideoFormat)p_video_format;
  notify_property_list_changed();
  _restart_if_enabled();
}

Ref<Texture2D> AJAOutput::get_texture() const { return _texture; }

void AJAOutput::set_texture(Ref<Texture2D> p_texture) {
  _texture = p_texture;
}

int AJAOutput::get_width() const { return _width; }

int AJAOutput::get_height() const { return _height; }

void AJAOutput::_on_frame_post_draw() {
  if (_enabled) {
    _capture_texture();
  }
}

void AJAOutput::_connect_frame_post_draw() {
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs || _frame_post_draw_connected) {
    return;
  }
  Callable cb = callable_mp(this, &AJAOutput::_on_frame_post_draw);
  if (!rs->is_connected("frame_post_draw", cb)) {
    rs->connect("frame_post_draw", cb);
  }
  _frame_post_draw_connected = true;
}

void AJAOutput::_disconnect_frame_post_draw() {
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs || !_frame_post_draw_connected) {
    return;
  }
  Callable cb = callable_mp(this, &AJAOutput::_on_frame_post_draw);
  if (rs->is_connected("frame_post_draw", cb)) {
    rs->disconnect("frame_post_draw", cb);
  }
  _frame_post_draw_connected = false;
}

void AJAOutput::_capture_texture() {
  if (!_open || _texture.is_null()) {
    return;
  }

  Ref<Image> img = _texture->get_image();
  if (img.is_null()) {
    return;
  }

  Ref<Image> frame = img->duplicate();
  if (frame->get_width() != _width || frame->get_height() != _height) {
    frame->resize(_width, _height);
  }
  if (frame->get_format() != Image::FORMAT_RGBA8) {
    frame->convert(Image::FORMAT_RGBA8);
  }

  PackedByteArray rgba = frame->get_data();
  {
    MutexLock lock(*_output_mutex);
    _latest_rgba = rgba;
    _has_frame = !_latest_rgba.is_empty();
  }
}

void AJAOutput::_output_thread_loop() {
  const NTV2Channel channel = aja::index_to_channel(_channel);

  AUTOCIRCULATE_TRANSFER xfer;
  PackedByteArray transfer_buf;
  const int bytes_per_pixel = (_pixel_format == NTV2_FBF_ABGR) ? 4 : 2;
  transfer_buf.resize(_width * _height * bytes_per_pixel);

  if (!_card.AutoCirculateStart(channel)) {
    UtilityFunctions::printerr("[AJAOutput] AutoCirculateStart failed");
    return;
  }

  OS *os = OS::get_singleton();
  while (!_is_thread_stop_requested()) {
    AUTOCIRCULATE_STATUS status;
    _card.AutoCirculateGetStatus(channel, status);

    if (status.CanAcceptMoreOutputFrames()) {
      PackedByteArray rgba;
      {
        MutexLock lock(*_output_mutex);
        if (_has_frame) {
          rgba = _latest_rgba;
        }
      }

      if (!rgba.is_empty() && rgba.size() >= _width * _height * 4) {
        const uint8_t *src = rgba.ptr();
        uint8_t *dst =
            reinterpret_cast<uint8_t *>(transfer_buf.ptrw());
        bool ok = false;

        if (_pixel_format == NTV2_FBF_ABGR) {
          // Godot RGBA8 = memory R,G,B,A = AJA ABGR. Direct copy.
          memcpy(dst, src, (size_t)(_width * _height * 4));
          ok = true;
        } else {
          // Godot RGBA8 (libyuv ABGR) → ARGB (libyuv) → UYVY
          PackedByteArray argb;
          argb.resize(_width * _height * 4);
          uint8_t *argb_ptr = argb.ptrw();
          ok =
              libyuv::ABGRToARGB(src, _width * 4, argb_ptr, _width * 4, _width,
                                 _height) == 0 &&
              libyuv::ARGBToUYVY(argb_ptr, _width * 4, dst, _width * 2, _width,
                                 _height) == 0;
        }

        if (ok) {
          xfer.SetVideoBuffer(reinterpret_cast<PULWord>(transfer_buf.ptrw()),
                              (ULWord)transfer_buf.size());
          _card.AutoCirculateTransfer(channel, xfer);
        }
      } else if (os) {
        os->delay_usec(1000);
      }
    } else if (os) {
      os->delay_usec(1000);
    } else {
      break;
    }
  }

  _card.AutoCirculateStop(channel);
}

void AJAOutput::_start_output_thread() {
  if (_output_thread.is_valid() && _output_thread->is_started()) {
    return;
  }
  {
    MutexLock lock(*_output_mutex);
    _thread_stop_requested = false;
  }
  _output_thread.instantiate();
  const Error err = _output_thread->start(
      callable_mp(this, &AJAOutput::_output_thread_loop),
      Thread::PRIORITY_HIGH);
  if (err != OK) {
    UtilityFunctions::printerr("[AJAOutput] Could not start output thread: ",
                               (int64_t)err);
    _output_thread.unref();
  }
}

void AJAOutput::_stop_output_thread() {
  if (_output_thread.is_null()) {
    return;
  }
  {
    MutexLock lock(*_output_mutex);
    _thread_stop_requested = true;
  }
  if (_output_thread->is_started()) {
    _output_thread->wait_to_finish();
  }
  _output_thread.unref();
}

bool AJAOutput::_is_thread_stop_requested() const {
  MutexLock lock(*_output_mutex);
  return _thread_stop_requested;
}

void AJAOutput::_restart_if_enabled() {
  if (!_enabled) {
    return;
  }
  _disconnect_frame_post_draw();
  close();
  if (!open(_device, _channel, (int64_t)_video_format)) {
    _enabled = false;
    return;
  }
  _start_output_thread();
  _connect_frame_post_draw();
}

String AJAOutput::_get_device_hint_string() const {
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

String AJAOutput::_get_channel_hint_string() const {
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  const int num_outputs =
      device.is_valid() ? device->get_num_video_outputs() : 1;
  String hint;
  for (int i = 0; i < num_outputs; ++i) {
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += "Channel " + String::num_int64(i + 1) + ":" + String::num_int64(i);
  }
  return hint.is_empty() ? "Channel 1:0" : hint;
}

String AJAOutput::_get_video_format_hint_string() const {
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  if (device.is_null()) {
    return "1080p59.94:" +
           String::num_int64((int64_t)NTV2_FORMAT_1080p_5994);
  }

  const Array formats = device->get_video_formats();
  String hint;
  for (int i = 0; i < formats.size(); ++i) {
    const Dictionary fmt = formats[i];
    String name = fmt.get("name", String());
    if (name.is_empty()) {
      name = String::num_int64((int64_t)fmt.get("width", 0)) + "x" +
             String::num_int64((int64_t)fmt.get("height", 0));
    }
    name = name.replace(",", " ").replace(":", " ");
    const int64_t id = fmt.get("id", (int64_t)NTV2_FORMAT_1080p_5994);
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += name + ":" + String::num_int64(id);
  }
  return hint.is_empty()
             ? "1080p59.94:" + String::num_int64((int64_t)NTV2_FORMAT_1080p_5994)
             : hint;
}
