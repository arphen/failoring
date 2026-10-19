#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "crochet_bindings.h"
#include "failoring_sim.h"
#include "yarn_bindings.h"
#include "yarn_sim_bindings.h"

using namespace godot;

static void initialize_failoring(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(FailoringSim);
    GDREGISTER_CLASS(CrochetPattern);
    GDREGISTER_CLASS(CrochetScene);
    GDREGISTER_CLASS(CrochetCalibration);
    GDREGISTER_CLASS(YarnSession);
    GDREGISTER_CLASS(YarnSimSession);
}
static void uninitialize_failoring(ModuleInitializationLevel level) { (void)level; }

extern "C" {
GDExtensionBool GDE_EXPORT failoring_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library,
                                          GDExtensionInitialization* r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_failoring);
    init_obj.register_terminator(uninitialize_failoring);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
