#include "ajantv2_output.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <ajabase/system/process.h>
#include <ntv2signalrouter.h>

#include <utility>

#include "ajantv2.hpp"

using namespace godot;

AJAOutput::AJAOutput() { _output_mutex = memnew(Mutex); }

AJAOutput::~AJAOutput() {
  close();
  memdelete(_output_mutex);
  _output_mutex = nullptr;
}

void AJAOutput::_bind_methods() {
  ClassDB::bind_method(
      D_METHOD("open", "device", "channel", "video_format", "pixel_format"),
      &AJAOutput::open, DEFVAL((int64_t)aja::PIXEL_FORMAT_AUTO));
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
  ClassDB::bind_method(D_METHOD("get_output_destination"),
                       &AJAOutput::get_output_destination);
  ClassDB::bind_method(D_METHOD("set_output_destination", "destination"),
                       &AJAOutput::set_output_destination);
  ClassDB::bind_method(D_METHOD("get_active_output_destination"),
                       &AJAOutput::get_active_output_destination);
  ClassDB::bind_method(D_METHOD("get_video_format"),
                       &AJAOutput::get_video_format);
  ClassDB::bind_method(D_METHOD("set_video_format", "video_format"),
                       &AJAOutput::set_video_format);
  ClassDB::bind_method(D_METHOD("get_pixel_format"),
                       &AJAOutput::get_pixel_format);
  ClassDB::bind_method(D_METHOD("set_pixel_format", "pixel_format"),
                       &AJAOutput::set_pixel_format);
  ClassDB::bind_method(D_METHOD("get_active_pixel_format"),
                       &AJAOutput::get_active_pixel_format);
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
                        {Variant::INT, "output_destination", PROPERTY_HINT_ENUM},
                        "set_output_destination", "get_output_destination");
  ClassDB::add_property("AJAOutput",
                        {Variant::INT, "video_format", PROPERTY_HINT_ENUM},
                        "set_video_format", "get_video_format");
  ClassDB::add_property("AJAOutput",
                        {Variant::INT, "pixel_format", PROPERTY_HINT_ENUM},
                        "set_pixel_format", "get_pixel_format");
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
  } else if (name == "output_destination") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_output_destination_hint_string();
  } else if (name == "video_format") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_video_format_hint_string();
  } else if (name == "pixel_format") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_pixel_format_hint_string();
  }
}

bool AJAOutput::open(int p_device, int p_channel, int64_t p_video_format,
                     int64_t p_pixel_format) {
  close();
  _device = p_device;
  _channel = p_channel;
  _video_format = (NTV2VideoFormat)p_video_format;
  _pixel_format = p_pixel_format;

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
  _acquired = true;
  _card.SetTaskMode(NTV2_OEM_TASKS);

  NTV2OutputDestinations supported_destinations;
  NTV2DeviceGetSupportedOutputDests(_card.GetDeviceID(),
                                    supported_destinations);
  const bool automatic_destination =
      _output_destination == aja::OUTPUT_DESTINATION_AUTO;
  _active_output_destination =
      automatic_destination
          ? NTV2ChannelToOutputDestination(aja::index_to_channel(_channel))
          : (NTV2OutputDestination)_output_destination;
  if (automatic_destination &&
      supported_destinations.find(_active_output_destination) ==
          supported_destinations.end()) {
    _active_output_destination = NTV2_OUTPUTDESTINATION_INVALID;
    for (const NTV2OutputDestination destination : supported_destinations) {
      if (NTV2_OUTPUT_DEST_IS_SDI(destination) ||
          NTV2_OUTPUT_DEST_IS_HDMI(destination)) {
        _active_output_destination = destination;
        break;
      }
    }
  }
  if (!NTV2_IS_VALID_OUTPUT_DEST(_active_output_destination) ||
      (!NTV2_OUTPUT_DEST_IS_SDI(_active_output_destination) &&
       !NTV2_OUTPUT_DEST_IS_HDMI(_active_output_destination)) ||
      supported_destinations.find(_active_output_destination) ==
          supported_destinations.end()) {
    UtilityFunctions::printerr("[AJAOutput] Device does not support output "
                               "destination ",
                               _output_destination);
    close();
    return false;
  }

  _active_channel =
      NTV2OutputDestinationToChannel(_active_output_destination);
  if ((int)_active_channel >= (int)_card.features().GetNumFrameStores()) {
    UtilityFunctions::printerr(
        "[AJAOutput] Output destination has no corresponding frame store");
    close();
    return false;
  }

  if (_card.features().CanDoMultiFormat()) {
    _card.SetMultiFormatMode(true);
  }
  if (NTV2_OUTPUT_DEST_IS_SDI(_active_output_destination) &&
      _card.features().HasBiDirectionalSDI()) {
    _card.SetSDITransmitEnable(_active_channel, true);
    _card.WaitForOutputVerticalInterrupt(NTV2_CHANNEL1, 10);
  }

  const bool supports_abgr =
      _card.features().CanDoFrameBufferFormat(NTV2_FBF_ABGR);
  const bool supports_ycbcr8 =
      _card.features().CanDoFrameBufferFormat(NTV2_FBF_8BIT_YCBCR);
  const bool supports_ycbcr10 =
      _card.features().CanDoFrameBufferFormat(NTV2_FBF_10BIT_YCBCR);
  const bool has_csc =
      ((int)_active_channel < (int)_card.features().GetNumCSCs());

  if (_pixel_format == aja::PIXEL_FORMAT_AUTO) {
    // Native YUV8 halves the host DMA bandwidth compared with ABGR and is
    // considerably more reliable at 1080p60 on older cards such as Kona LHi.
    if (supports_ycbcr8) {
      _active_pixel_format = NTV2_FBF_8BIT_YCBCR;
    } else if (supports_abgr && has_csc) {
      _active_pixel_format = NTV2_FBF_ABGR;
    } else if (supports_ycbcr10) {
      _active_pixel_format = NTV2_FBF_10BIT_YCBCR;
    } else {
      UtilityFunctions::printerr(
          "[AJAOutput] Device does not support an available output pixel "
          "format");
      close();
      return false;
    }
  } else if (_pixel_format == aja::PIXEL_FORMAT_ABGR) {
    if (!supports_abgr || !has_csc) {
      UtilityFunctions::printerr(
          "[AJAOutput] ABGR output requires device ABGR support and a CSC "
          "for channel ",
          _channel);
      close();
      return false;
    }
    _active_pixel_format = NTV2_FBF_ABGR;
  } else if (_pixel_format == aja::PIXEL_FORMAT_8BIT_YCBCR) {
    if (!supports_ycbcr8) {
      UtilityFunctions::printerr(
          "[AJAOutput] Device does not support 8-bit YCbCr output");
      close();
      return false;
    }
    _active_pixel_format = NTV2_FBF_8BIT_YCBCR;
  } else if (_pixel_format == aja::PIXEL_FORMAT_10BIT_YCBCR) {
    if (!supports_ycbcr10) {
      UtilityFunctions::printerr(
          "[AJAOutput] Device does not support 10-bit YCbCr output");
      close();
      return false;
    }
    _active_pixel_format = NTV2_FBF_10BIT_YCBCR;
  } else {
    UtilityFunctions::printerr("[AJAOutput] Unsupported pixel format ",
                               _pixel_format);
    close();
    return false;
  }

  // Validate video format.
  if (!_card.features().CanDoVideoFormat(_video_format)) {
    UtilityFunctions::printerr(
        "[AJAOutput] Device does not support video format ",
        (int64_t)_video_format);
    close();
    return false;
  }

  NTV2FormatDescriptor fd(_video_format, _active_pixel_format,
                          NTV2_VANCMODE_OFF);
  if (!fd.IsValid()) {
    UtilityFunctions::printerr("[AJAOutput] Invalid format descriptor");
    close();
    return false;
  }
  _width = (int)fd.GetRasterWidth();
  _height = (int)fd.GetRasterHeight(true);
  _device_stride = (int)fd.GetBytesPerRow();
  const size_t transfer_bytes = (size_t)fd.GetTotalBytes();

  _card.DisableChannel(_active_channel);
  _card.SetVANCMode(NTV2_VANCMODE_OFF, _active_channel);
  _card.SetVideoFormat(_video_format, false, false, _active_channel);
  _card.SetFrameBufferFormat(_active_channel, _active_pixel_format);
  _card.SetReference(NTV2_REFERENCE_FREERUN);
  if (NTV2_OUTPUT_DEST_IS_SDI(_active_output_destination)) {
    _card.SetSDIOutputStandard(
        _active_channel, GetNTV2StandardFromVideoFormat(_video_format));
  }

  // Set up signal routing.
  _card.ClearRouting();

  const bool is_rgb = (_active_pixel_format == NTV2_FBF_ABGR);
  const NTV2OutputXptID frame_store_output =
      ::GetFrameStoreOutputXptFromChannel(_active_channel, is_rgb, false);
  NTV2OutputXptID video_output = frame_store_output;

  if (is_rgb) {
    // FrameStore RGB -> CSC (RGB to YCbCr) -> device outputs.
    _card.Connect(::GetCSCInputXptFromChannel(_active_channel),
                  frame_store_output);
    video_output =
        ::GetCSCOutputXptFromChannel(_active_channel, false, false);
  }

  _card.Connect(::GetOutputDestInputXpt(_active_output_destination),
                video_output);
  _card.EnableChannel(_active_channel);
  _card.SubscribeOutputVerticalEvent(_active_channel);

  if (!_video_converter.prepare(_width, _height, _device_stride,
                                transfer_bytes, _active_pixel_format)) {
    UtilityFunctions::printerr("[AJAOutput] Could not prepare video converter");
    close();
    return false;
  }

  PackedByteArray black;
  black.resize(_width * _height * 4);
  uint8_t *black_pixels = black.ptrw();
  for (int i = 3; i < black.size(); i += 4) {
    black_pixels[i] = 255;
  }
  for (size_t i = 0; i < _transfer_buffers.size(); ++i) {
    if (!_transfer_buffers[i].Allocate(transfer_bytes, true) ||
        !_video_converter.convert(
            black.ptr(), false, _transfer_buffers[i].GetHostPointer(),
            (size_t)_transfer_buffers[i].GetByteCount())) {
      UtilityFunctions::printerr(
          "[AJAOutput] Could not allocate or initialise DMA buffer ",
          (int64_t)i);
      close();
      return false;
    }
    _transfer_buffer_locked[i] =
        _card.DMABufferLock(_transfer_buffers[i], true);
    if (!_transfer_buffer_locked[i]) {
      UtilityFunctions::push_warning(
          "[AJAOutput] Could not page-lock a video DMA buffer; "
          "high-frame-rate output may drop frames");
    }
  }

  _card.AutoCirculateStop(_active_channel);
  if (!_card.AutoCirculateInitForOutput(
          _active_channel, DEVICE_FRAME_COUNT, NTV2_AUDIOSYSTEM_INVALID, 0)) {
    UtilityFunctions::printerr(
        "[AJAOutput] AutoCirculateInitForOutput failed for device ", p_device,
        " channel ", (int64_t)_active_channel);
    close();
    return false;
  }

  {
    MutexLock lock(*_output_mutex);
    ++_readback_generation;
    _readback_pending = false;
    _latest_rgba.clear();
    _latest_frame_serial = 0;
    _active_transfer_buffer = 0;
    _staging_transfer_buffer = 1;
    _staging_transfer_ready = false;
  }

  _open = true;
  return true;
}

void AJAOutput::close() {
  {
    MutexLock lock(*_output_mutex);
    ++_readback_generation;
    _readback_pending = false;
  }
  _stop_output_thread();

  if (_card.IsOpen()) {
    if (_acquired) {
      _card.AutoCirculateStop(_active_channel);
      for (size_t i = 0; i < _transfer_buffers.size(); ++i) {
        if (_transfer_buffer_locked[i]) {
          _card.DMABufferUnlock(_transfer_buffers[i]);
        }
      }
      _card.UnsubscribeOutputVerticalEvent(_active_channel);
      _card.DisableChannel(_active_channel);
      _card.ReleaseStreamForApplication(aja::kAppSignature,
                                        int32_t(AJAProcess::GetPid()));
    }
    _card.Close();
  }
  _acquired = false;
  _transfer_buffer_locked.fill(false);
  for (NTV2Buffer &buffer : _transfer_buffers) {
    buffer.Deallocate();
  }

  {
    MutexLock lock(*_output_mutex);
    _latest_rgba.clear();
    _latest_frame_serial = 0;
    _active_transfer_buffer = 0;
    _staging_transfer_buffer = 1;
    _staging_transfer_ready = false;
  }

  _open = false;
  _active_output_destination = NTV2_OUTPUTDESTINATION_INVALID;
  _active_channel = NTV2_CHANNEL1;
  _active_pixel_format = NTV2_FBF_INVALID;
  _device_stride = 0;
  _width = 0;
  _height = 0;
}

bool AJAOutput::is_open() const { return _open; }

bool AJAOutput::is_enabled() const { return _enabled; }

void AJAOutput::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  if (_enabled) {
    if (!_open &&
        !open(_device, _channel, (int64_t)_video_format, _pixel_format)) {
      _enabled = false;
      return;
    }
    _start_output_thread();
    if (_output_thread.is_null() || !_output_thread->is_started()) {
      _enabled = false;
      close();
      return;
    }
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
  notify_property_list_changed();
  _restart_if_enabled();
}

int64_t AJAOutput::get_output_destination() const {
  return _output_destination;
}

void AJAOutput::set_output_destination(int64_t p_output_destination) {
  if (_output_destination == p_output_destination) {
    return;
  }
  _output_destination = p_output_destination;
  notify_property_list_changed();
  _restart_if_enabled();
}

int64_t AJAOutput::get_active_output_destination() const {
  return _open ? (int64_t)_active_output_destination
               : (int64_t)aja::OUTPUT_DESTINATION_AUTO;
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

int64_t AJAOutput::get_pixel_format() const { return _pixel_format; }

void AJAOutput::set_pixel_format(int64_t p_pixel_format) {
  if (_pixel_format == p_pixel_format) {
    return;
  }
  _pixel_format = p_pixel_format;
  _restart_if_enabled();
}

int64_t AJAOutput::get_active_pixel_format() const {
  return _open ? (int64_t)_active_pixel_format
               : (int64_t)aja::PIXEL_FORMAT_AUTO;
}

Ref<Texture2D> AJAOutput::get_texture() const { return _texture; }

void AJAOutput::set_texture(Ref<Texture2D> p_texture) { _texture = p_texture; }

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

  {
    MutexLock lock(*_output_mutex);
    if (_readback_pending) {
      return;
    }
  }

  RenderingServer *rs = RenderingServer::get_singleton();
  RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
  if (rd && _texture->get_width() == _width &&
      _texture->get_height() == _height) {
    const RID rd_texture = rs->texture_get_rd_texture(_texture->get_rid());
    const Ref<RDTextureFormat> format =
        rd_texture.is_valid() ? rd->texture_get_format(rd_texture)
                              : Ref<RDTextureFormat>();
    if (format.is_valid()) {
      const RenderingDevice::DataFormat data_format = format->get_format();
      const bool source_rgba =
          data_format == RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM ||
          data_format == RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB;
      const bool source_bgra =
          data_format == RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM ||
          data_format == RenderingDevice::DATA_FORMAT_B8G8R8A8_SRGB;
      if (source_rgba || source_bgra) {
        int64_t generation = 0;
        {
          MutexLock lock(*_output_mutex);
          if (!_open || _readback_pending) {
            return;
          }
          _readback_pending = true;
          generation = _readback_generation;
        }
        const Error err = rd->texture_get_data_async(
            rd_texture, 0,
            callable_mp(this, &AJAOutput::_on_texture_data)
                .bind(source_bgra, generation));
        if (err == OK) {
          return;
        }
        {
          MutexLock lock(*_output_mutex);
          if (generation == _readback_generation) {
            _readback_pending = false;
          }
        }
      }
    }
  }

  _capture_texture_synchronously();
}

void AJAOutput::_capture_texture_synchronously() {
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
  if (rgba.is_empty()) {
    return;
  }
  int64_t generation = 0;
  {
    MutexLock lock(*_output_mutex);
    generation = _readback_generation;
  }
  _publish_frame(rgba, false, generation);
}

void AJAOutput::_on_texture_data(PackedByteArray p_data, bool p_source_bgra,
                                 int64_t p_generation) {
  {
    MutexLock lock(*_output_mutex);
    if (p_generation != _readback_generation) {
      return;
    }
    _readback_pending = false;
  }
  _publish_frame(p_data, p_source_bgra, p_generation);
}

void AJAOutput::_publish_frame(PackedByteArray p_data, bool p_source_bgra,
                               int64_t p_generation) {
  if (p_data.size() < _width * _height * 4) {
    return;
  }
  MutexLock lock(*_output_mutex);
  if (!_open || !_enabled || p_generation != _readback_generation) {
    return;
  }
  // A single newest-frame slot prevents latency from accumulating when the
  // renderer is faster than the AJA wire clock.
  _latest_rgba = p_data;
  _latest_source_bgra = p_source_bgra;
  ++_latest_frame_serial;
}

void AJAOutput::_converter_thread_loop() {
  OS *os = OS::get_singleton();
  uint64_t converted_serial = 0;

  while (true) {
    PackedByteArray rgba;
    bool source_bgra = false;
    int staging_buffer = -1;
    {
      MutexLock lock(*_output_mutex);
      if (_thread_stop_requested) {
        break;
      }
      if (!_staging_transfer_ready && !_latest_rgba.is_empty() &&
          _latest_frame_serial != converted_serial) {
        rgba = _latest_rgba;
        source_bgra = _latest_source_bgra;
        converted_serial = _latest_frame_serial;
        staging_buffer = _staging_transfer_buffer;
      }
    }

    if (staging_buffer < 0) {
      if (os) {
        os->delay_usec(1000);
        continue;
      }
      break;
    }

    NTV2Buffer &buffer = _transfer_buffers[(size_t)staging_buffer];
    if (_video_converter.convert(rgba.ptr(), source_bgra,
                                 buffer.GetHostPointer(),
                                 (size_t)buffer.GetByteCount())) {
      MutexLock lock(*_output_mutex);
      if (!_thread_stop_requested &&
          staging_buffer == _staging_transfer_buffer) {
        _staging_transfer_ready = true;
      }
    }
  }
}

void AJAOutput::_output_thread_loop() {
  AUTOCIRCULATE_TRANSFER xfer;
  OS *os = OS::get_singleton();
  int preroll_frames = 0;
  bool started = false;
  while (!_is_thread_stop_requested()) {
    AUTOCIRCULATE_STATUS status;
    _card.AutoCirculateGetStatus(_active_channel, status);

    if (status.CanAcceptMoreOutputFrames()) {
      int transfer_buffer = 0;
      {
        MutexLock lock(*_output_mutex);
        if (_staging_transfer_ready) {
          std::swap(_active_transfer_buffer, _staging_transfer_buffer);
          _staging_transfer_ready = false;
        }
        transfer_buffer = _active_transfer_buffer;
      }

      NTV2Buffer &buffer = _transfer_buffers[(size_t)transfer_buffer];
      xfer.SetVideoBuffer(reinterpret_cast<PULWord>(buffer.GetHostPointer()),
                          buffer.GetByteCount());
      if (_card.AutoCirculateTransfer(_active_channel, xfer) && !started) {
        ++preroll_frames;
        if (preroll_frames >= PREROLL_FRAME_COUNT) {
          if (!_card.AutoCirculateStart(_active_channel)) {
            UtilityFunctions::printerr(
                "[AJAOutput] AutoCirculateStart failed");
            break;
          }
          started = true;
        }
      }
    } else if (os) {
      os->delay_usec(1000);
    } else {
      break;
    }
  }

  _card.AutoCirculateStop(_active_channel);
}

void AJAOutput::_start_output_thread() {
  if (_output_thread.is_valid() && _output_thread->is_started()) {
    return;
  }
  {
    MutexLock lock(*_output_mutex);
    _thread_stop_requested = false;
  }
  _converter_thread.instantiate();
  Error err = _converter_thread->start(
      callable_mp(this, &AJAOutput::_converter_thread_loop));
  if (err != OK) {
    UtilityFunctions::printerr(
        "[AJAOutput] Could not start converter thread: ", (int64_t)err);
    _converter_thread.unref();
    return;
  }

  _output_thread.instantiate();
  err = _output_thread->start(
      callable_mp(this, &AJAOutput::_output_thread_loop), Thread::PRIORITY_HIGH);
  if (err != OK) {
    UtilityFunctions::printerr("[AJAOutput] Could not start output thread: ",
                               (int64_t)err);
    _output_thread.unref();
    {
      MutexLock lock(*_output_mutex);
      _thread_stop_requested = true;
    }
    _converter_thread->wait_to_finish();
    _converter_thread.unref();
  }
}

void AJAOutput::_stop_output_thread() {
  if (_output_thread.is_null() && _converter_thread.is_null()) {
    return;
  }
  {
    MutexLock lock(*_output_mutex);
    _thread_stop_requested = true;
  }
  if (_output_thread.is_valid() && _output_thread->is_started()) {
    _output_thread->wait_to_finish();
  }
  _output_thread.unref();
  if (_converter_thread.is_valid() && _converter_thread->is_started()) {
    _converter_thread->wait_to_finish();
  }
  _converter_thread.unref();
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
  if (!open(_device, _channel, (int64_t)_video_format, _pixel_format)) {
    _enabled = false;
    return;
  }
  _start_output_thread();
  if (_output_thread.is_null() || !_output_thread->is_started()) {
    _enabled = false;
    close();
    return;
  }
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

String AJAOutput::_get_output_destination_hint_string() const {
  String hint = "Auto (from channel):" +
                String::num_int64((int64_t)aja::OUTPUT_DESTINATION_AUTO);
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  if (device.is_null()) {
    return hint;
  }
  const Array destinations = device->get_output_destinations();
  for (int i = 0; i < destinations.size(); ++i) {
    const Dictionary destination = destinations[i];
    String name = destination.get("name", String());
    const String type = destination.get("type", String());
    if (!type.is_empty() && !name.begins_with(type)) {
      name = type + String(" ") + name;
    }
    name = name.replace(",", " ").replace(":", " ");
    hint += "," + name + ":" +
            String::num_int64(destination.get("id", (int64_t)0));
  }
  return hint;
}

String AJAOutput::_get_video_format_hint_string() const {
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  if (device.is_null()) {
    return "1080p59.94:" + String::num_int64((int64_t)NTV2_FORMAT_1080p_5994_B);
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
    const int64_t id = fmt.get("id", (int64_t)NTV2_FORMAT_1080p_5994_B);
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += name + ":" + String::num_int64(id);
  }
  return hint.is_empty()
             ? "1080p59.94:" +
                   String::num_int64((int64_t)NTV2_FORMAT_1080p_5994_B)
             : hint;
}

String AJAOutput::_get_pixel_format_hint_string() const {
  String hint = "Auto (prefer YUV8):" +
                String::num_int64((int64_t)aja::PIXEL_FORMAT_AUTO);
  AJAVideoSystems *aja = AJAVideoSystems::get_singleton();
  Ref<AJADevice> device = aja ? aja->get_device(_device) : Ref<AJADevice>();
  if (device.is_null()) {
    hint += ",8-bit YCbCr (UYVY):" +
            String::num_int64((int64_t)aja::PIXEL_FORMAT_8BIT_YCBCR);
    hint += ",10-bit YCbCr (v210):" +
            String::num_int64((int64_t)aja::PIXEL_FORMAT_10BIT_YCBCR);
    hint += ",ABGR:" + String::num_int64((int64_t)aja::PIXEL_FORMAT_ABGR);
    return hint;
  }

  const Array formats = device->get_pixel_formats();
  for (int i = 0; i < formats.size(); ++i) {
    const Dictionary format = formats[i];
    const int64_t id = format.get("id", (int64_t)aja::PIXEL_FORMAT_AUTO);
    if (!device->can_output_pixel_format(_get_pixel_format_channel(), id)) {
      continue;
    }
    String name = format.get("name", String());
    name = name.replace(",", " ").replace(":", " ");
    hint += "," + name + ":" + String::num_int64(id);
  }
  return hint;
}

int AJAOutput::_get_pixel_format_channel() const {
  if (_output_destination != aja::OUTPUT_DESTINATION_AUTO) {
    const NTV2OutputDestination destination =
        (NTV2OutputDestination)_output_destination;
    if (NTV2_IS_VALID_OUTPUT_DEST(destination)) {
      return (int)NTV2OutputDestinationToChannel(destination);
    }
  }
  return _channel;
}
