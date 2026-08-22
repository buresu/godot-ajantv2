#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <array>
#include <cstdint>

#include "ajantv2_common.hpp"
#include "ajantv2_video_converter.hpp"

namespace godot {

class AJAOutput : public Node {
  GDCLASS(AJAOutput, Node)

public:
  AJAOutput();
  ~AJAOutput() override;

  void _ready() override;
  void _exit_tree() override;
  void _validate_property(PropertyInfo &p_property) const;

  bool open(int p_device, int p_channel, int64_t p_video_format,
            int64_t p_pixel_format = aja::PIXEL_FORMAT_AUTO);
  void close();
  bool is_open() const;

  bool is_enabled() const;
  void set_enabled(bool p_enabled);
  int get_device() const;
  void set_device(int p_device);
  int get_channel() const;
  void set_channel(int p_channel);
  int64_t get_output_destination() const;
  void set_output_destination(int64_t p_output_destination);
  int64_t get_active_output_destination() const;
  int64_t get_video_format() const;
  void set_video_format(int64_t p_video_format);
  int64_t get_pixel_format() const;
  void set_pixel_format(int64_t p_pixel_format);
  int64_t get_active_pixel_format() const;
  Ref<Texture2D> get_texture() const;
  void set_texture(Ref<Texture2D> p_texture);
  int get_width() const;
  int get_height() const;

protected:
  static void _bind_methods();

private:
  static constexpr int DEVICE_FRAME_COUNT = 4;
  static constexpr int PREROLL_FRAME_COUNT = 3;

  void _output_thread_loop();
  void _converter_thread_loop();
  void _start_output_thread();
  void _stop_output_thread();
  bool _is_thread_stop_requested() const;
  void _capture_texture();
  void _capture_texture_synchronously();
  void _on_texture_data(PackedByteArray p_data, bool p_source_bgra,
                        int64_t p_generation);
  void _publish_frame(PackedByteArray p_data, bool p_source_bgra,
                      int64_t p_generation);
  void _on_frame_post_draw();
  void _connect_frame_post_draw();
  void _disconnect_frame_post_draw();
  void _restart_if_enabled();
  String _get_device_hint_string() const;
  String _get_channel_hint_string() const;
  String _get_output_destination_hint_string() const;
  String _get_video_format_hint_string() const;
  String _get_pixel_format_hint_string() const;
  int _get_pixel_format_channel() const;

  CNTV2Card _card;
  int _device = 0;
  int _channel = 0;
  int64_t _output_destination = aja::OUTPUT_DESTINATION_AUTO;
  NTV2OutputDestination _active_output_destination =
      NTV2_OUTPUTDESTINATION_INVALID;
  NTV2Channel _active_channel = NTV2_CHANNEL1;
  NTV2VideoFormat _video_format = NTV2_FORMAT_1080p_5994_B;
  int64_t _pixel_format = aja::PIXEL_FORMAT_AUTO;
  NTV2PixelFormat _active_pixel_format = NTV2_FBF_INVALID;
  int _width = 0;
  int _height = 0;
  bool _open = false;
  bool _acquired = false;
  bool _enabled = false;
  bool _thread_stop_requested = false;
  bool _frame_post_draw_connected = false;
  bool _readback_pending = false;
  int64_t _readback_generation = 0;
  uint64_t _latest_frame_serial = 0;

  mutable Mutex *_output_mutex = nullptr;
  Ref<Thread> _output_thread;
  Ref<Thread> _converter_thread;
  PackedByteArray _latest_rgba;
  bool _latest_source_bgra = false;
  std::array<NTV2Buffer, 2> _transfer_buffers;
  std::array<bool, 2> _transfer_buffer_locked{};
  int _active_transfer_buffer = 0;
  int _staging_transfer_buffer = 1;
  bool _staging_transfer_ready = false;
  int _device_stride = 0;
  AJAVideoConverter _video_converter;
  Ref<Texture2D> _texture;
};

} // namespace godot
