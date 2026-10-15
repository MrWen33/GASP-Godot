#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "gasp_animator.h"
#include "gasp_retargeter.h"
#include "post_process.h"
#include "pose_interpolator.h"

using namespace godot;

void initialize_gasp(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(GaspAnimator);
	GDREGISTER_CLASS(GaspRetargeter);
	GDREGISTER_CLASS(GaspPostProcess);
	GDREGISTER_CLASS(GaspPoseInterpolator);
}

void uninitialize_gasp(ModuleInitializationLevel p_level) {}

extern "C" GDExtensionBool GDE_EXPORT gasp_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_gasp);
	init_obj.register_terminator(uninitialize_gasp);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
