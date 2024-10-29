#include "lvlimport.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void LVLImport::_bind_methods() {
	godot::ClassDB::bind_static_method("LVLImport", godot::D_METHOD("import_lvl"), &LVLImport::import_lvl);
}

void LVLImport::import_lvl() {
	UtilityFunctions::print("I'm helping!");
}

}
