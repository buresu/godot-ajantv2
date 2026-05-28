#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ajantv2_common.hpp"
#include "ajantv2_device.hpp"

namespace godot {

class AJAVideoSystems : public Object {
  GDCLASS(AJAVideoSystems, Object)

public:
  static AJAVideoSystems *get_singleton();

  AJAVideoSystems();
  ~AJAVideoSystems() override;

  void refresh();
  int get_device_count() const;
  Array get_devices() const;
  Ref<AJADevice> get_device(int p_index) const;
  Array get_video_formats(int p_device_index) const;

protected:
  static void _bind_methods();

private:
  static AJAVideoSystems *_singleton;
  Vector<Ref<AJADevice>> _devices;

  void _clear_devices();
};

} // namespace godot
