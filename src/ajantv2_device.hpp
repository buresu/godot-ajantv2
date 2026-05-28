#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ajantv2_common.hpp"

namespace godot {

class AJADevice : public RefCounted {
  GDCLASS(AJADevice, RefCounted)

public:
  AJADevice();
  ~AJADevice() override;

  void setup(ULWord p_device_index);

  ULWord get_device_index() const;
  String get_display_name() const;
  String get_model_name() const;
  int get_num_video_inputs() const;
  int get_num_video_outputs() const;
  bool can_capture() const;
  bool can_playback() const;
  Dictionary to_dictionary() const;
  Array get_video_formats() const;

protected:
  static void _bind_methods();

private:
  ULWord _device_index = 0;
  String _display_name;
  String _model_name;
  int _num_video_inputs = 0;
  int _num_video_outputs = 0;
  bool _can_capture = false;
  bool _can_playback = false;
  Array _video_formats;
};

} // namespace godot
