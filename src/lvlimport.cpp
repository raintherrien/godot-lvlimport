#include "lvlimport.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <LibSWBF2.h>

namespace godot {

void LVLImport::_bind_methods() {
	godot::ClassDB::bind_static_method("LVLImport", godot::D_METHOD("import_lvl"), &LVLImport::import_lvl);
}

void LVLImport::import_lvl(const String &lvl_filename) {
	LibSWBF2::Level *lvl = LibSWBF2::Level_FromFile(lvl_filename.utf8().get_data());
	UtilityFunctions::print("I'm helping!");
	UtilityFunctions::print(LibSWBF2::Level_GetName(lvl));
	LibSWBF2::Level_Destroy(lvl);
}

}
