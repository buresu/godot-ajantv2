#include "ajantv2.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <ntv2publicinterface.h>

using namespace godot;

AJAVideoSystems *AJAVideoSystems::_singleton = nullptr;

AJAVideoSystems::AJAVideoSystems() {
  _singleton = this;
  refresh();
}

AJAVideoSystems::~AJAVideoSystems() {
  _clear_devices();
  _singleton = nullptr;
}

AJAVideoSystems *AJAVideoSystems::get_singleton() { return _singleton; }

void AJAVideoSystems::_bind_methods() {
  ClassDB::bind_method(D_METHOD("refresh"), &AJAVideoSystems::refresh);
  ClassDB::bind_method(D_METHOD("get_device_count"),
                       &AJAVideoSystems::get_device_count);
  ClassDB::bind_method(D_METHOD("get_devices"), &AJAVideoSystems::get_devices);
  ClassDB::bind_method(D_METHOD("get_device", "device_index"),
                       &AJAVideoSystems::get_device);
  ClassDB::bind_method(D_METHOD("get_video_formats", "device_index"),
                       &AJAVideoSystems::get_video_formats);
  ClassDB::bind_method(D_METHOD("get_pixel_formats", "device_index"),
                       &AJAVideoSystems::get_pixel_formats);

  BIND_ENUM_CONSTANT(PIXEL_FORMAT_AUTO);
  BIND_ENUM_CONSTANT(PIXEL_FORMAT_8BIT_YCBCR);
  BIND_ENUM_CONSTANT(PIXEL_FORMAT_ABGR);
}

void AJAVideoSystems::_clear_devices() { _devices.clear(); }

void AJAVideoSystems::refresh() {
  _clear_devices();

  for (ULWord i = 0;; i++) {
    CNTV2Card card;
    if (!CNTV2DeviceScanner::GetDeviceAtIndex(i, card)) {
      break;
    }
    Ref<AJADevice> device;
    device.instantiate();
    device->setup(i);
    _devices.push_back(device);
  }
}

int AJAVideoSystems::get_device_count() const { return _devices.size(); }

Array AJAVideoSystems::get_devices() const {
  Array result;
  for (int i = 0; i < _devices.size(); ++i) {
    Dictionary d = _devices[i]->to_dictionary();
    d["index"] = i;
    result.push_back(d);
  }
  return result;
}

Ref<AJADevice> AJAVideoSystems::get_device(int p_index) const {
  if (p_index < 0 || p_index >= _devices.size()) {
    return Ref<AJADevice>();
  }
  return _devices[p_index];
}

Array AJAVideoSystems::get_video_formats(int p_device_index) const {
  Ref<AJADevice> device = get_device(p_device_index);
  return device.is_valid() ? device->get_video_formats() : Array();
}

Array AJAVideoSystems::get_pixel_formats(int p_device_index) const {
  Ref<AJADevice> device = get_device(p_device_index);
  return device.is_valid() ? device->get_pixel_formats() : Array();
}
