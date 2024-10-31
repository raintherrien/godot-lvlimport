#include "lvlimport.hpp"
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <LibSWBF2.h>

using namespace LibSWBF2;

namespace godot {

void LVLImport::_bind_methods() {
	godot::ClassDB::bind_static_method("LVLImport", godot::D_METHOD("import_lvl"), &LVLImport::import_lvl);
}

static int import_world(const World &world) {
	UtilityFunctions::print("Importing world ", world.GetName().Buffer());

	HashSet<String> entity_classes;

	// Import instances
	const LibSWBF2::List<Instance> &instances = world.GetInstances();
	for (size_t i = 0; i < instances.Size(); ++ i) {
		const Instance &instance = instances[i];
		const char *entity_class_name_cstr = instance.GetEntityClassName().Buffer();
		UtilityFunctions::print("Importing instance ", i, "/", instances.Size(), " ", entity_class_name_cstr, " ", instance.GetName().Buffer());
		if (!entity_classes.has(entity_class_name_cstr)) {
			entity_classes.insert(entity_class_name_cstr);
			UtilityFunctions::print("Creating entity class ", entity_class_name_cstr, " scene");
			// XXX Create the scene
		}
		// XXX Instantiate the scene
	}

	// XXX: Import terrain
	// XXX: Import skydome
	return 0;
}

void LVLImport::import_lvl(const String &lvl_filename) {
	const char *lvl_filename_cstr = lvl_filename.utf8().get_data();

	// Create our container and load our lvl file
	Container *container = Container::Create();
	uint16_t lvl_handle = container->AddLevel(lvl_filename_cstr);
	container->StartLoading();
	int waited = 0;
	while (!container->IsDone()) {
		OS::get_singleton()->delay_msec(1000);
		++ waited;
		UtilityFunctions::print("Waited ", waited, "s for levels to load");
		if (waited >= 60) {
			UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " took longer than 60s to load");
			goto delete_container;
		}
	}

	// Verify our lvl contains one or more worlds and import them
	{
		const Level *level = container->GetLevel(lvl_handle);
		UtilityFunctions::print("Importing level ", level->GetLevelName().Buffer());
		if (!level->IsWorldLevel()) {
			UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " is not a world level");
			goto delete_container;
		}
		const LibSWBF2::List<World> &worlds = level->GetWorlds();
		for (size_t i = 0; i < worlds.Size(); ++ i) {
			const World &world = worlds[i];
			if (import_world(world) != 0) {
				UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " world ", world.GetName().Buffer(), " failed to import");
				goto delete_container;
			}
		}
	}

	UtilityFunctions::print("Import complete");

delete_container:
	Container::Delete(container);
}

}
