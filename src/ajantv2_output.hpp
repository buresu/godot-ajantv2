#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ajantv2_common.hpp"

namespace godot {

class AJAOutput : public Node {
  GDCLASS(AJAOutput, Node)

public:
  AJAOutput();
  ~AJAOutput() override;

  void _ready() override;
  void _exit_tree() override;
  void _validate_property(PropertyInfo &p_property) const;

  bool open(int p_device, int p_channel, int64_t p_video_format);
  void close();
  bool is_open() const;

  bool is_enabled() const;
  void set_enabled(bool p_enabled);
  int get_device() const;
  void set_device(int p_device);
  int get_channel() const;
  void set_channel(int p_channel);
  int64_t get_video_format() const;
  void set_video_format(int64_t p_video_format);
  Ref<Texture2D> get_texture() const;
  void set_texture(Ref<Texture2D> p_texture);
  int get_width() const;
  int get_height() const;

protected:
  static void _bind_methods();

private:
  void _output_thread_loop();
  void _start_output_thread();
  void _stop_output_thread();
  bool _is_thread_stop_requested() const;
  void _capture_texture();
  void _on_frame_post_draw();
  void _connect_frame_post_draw();
  void _disconnect_frame_post_draw();
  void _restart_if_enabled();
  String _get_device_hint_string() const;
  String _get_channel_hint_string() const;
  String _get_video_format_hint_string() const;

  CNTV2Card _card;
  int _device = 0;
  int _channel = 0;
  NTV2VideoFormat _video_format = NTV2_FORMAT_1080p_5994_B;
  NTV2PixelFormat _pixel_format = NTV2_FBF_ABGR;
  int _width = 0;
  int _height = 0;
  bool _open = false;
  bool _enabled = false;
  bool _thread_stop_requested = false;
  bool _has_frame = false;
  bool _frame_post_draw_connected = false;

  mutable Mutex *_output_mutex = nullptr;
  Ref<Thread> _output_thread;
  PackedByteArray _latest_rgba;
  Ref<Texture2D> _texture;
};

} // namespace godot
