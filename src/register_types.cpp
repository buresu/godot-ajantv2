#include "register_types.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "ajantv2.hpp"
#include "ajantv2_device.hpp"
#include "ajantv2_input.hpp"
#include "ajantv2_output.hpp"

using namespace godot;

static AJAVideoSystems *aja_singleton = nullptr;

void initialize_godot_ajantv2_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  GDREGISTER_CLASS(AJAVideoSystems);
  GDREGISTER_CLASS(AJADevice);
  GDREGISTER_CLASS(AJAInput);
  GDREGISTER_CLASS(AJAOutput);

  aja_singleton = memnew(AJAVideoSystems);
  Engine::get_singleton()->register_singleton("AJAVideoSystems", aja_singleton);
}

void uninitialize_godot_ajantv2_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  if (aja_singleton) {
    Engine::get_singleton()->unregister_singleton("AJAVideoSystems");
    memdelete(aja_singleton);
    aja_singleton = nullptr;
  }
}

extern "C" {
GDExtensionBool GDE_EXPORT
godot_ajantv2_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                   GDExtensionClassLibraryPtr p_library,
                   GDExtensionInitialization *r_initialization) {
  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);
  init_obj.register_initializer(initialize_godot_ajantv2_module);
  init_obj.register_terminator(uninitialize_godot_ajantv2_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}
}
