#include "lvlimport.hpp"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <LibSWBF2.h>

using namespace LibSWBF2;

namespace godot {

class WorldImporter {
	Container *container;
	HashMap<String, String> entity_class_scenes;

	Node *import_world(const World &world, const String &scene_dir) {
		const char *world_name_cstr = world.GetName().Buffer();
		UtilityFunctions::print("Importing world ", world_name_cstr);

		Node *world_root = memnew(Node);
		world_root->set_name(world_name_cstr);

		// Import instances
		const LibSWBF2::List<Instance> &instances = world.GetInstances();
		for (size_t i = 0; i < instances.Size(); ++ i) {
			UtilityFunctions::print("Importing instance ", i, "/", instances.Size());
			String instance_scene = import_instance(instances[i], scene_dir);
			if (instance_scene.length() > 0) {
				UtilityFunctions::print("Instantiating ", instance_scene);
				// XXX Instantiate the scene
			}
		}

		// XXX: Import terrain
		// XXX: Import skydome
		return world_root;
	}

	String import_instance(const Instance &instance, const String &scene_dir) {
		const char *entity_class_name_cstr = instance.GetEntityClassName().Buffer();
		UtilityFunctions::print("Instance ", instance.GetName().Buffer(), " of EntityClass ", entity_class_name_cstr);

		// If we've already loaded this entity, return the scene name
		if (entity_class_scenes.has(entity_class_name_cstr)) {
			return entity_class_scenes.get(entity_class_name_cstr);
		}

		// Assert this instance is a type we understand
		const char *valid_base_classes[] = {
			"door",
			"animatedprop",
			"prop",
			"building",
			"destructablebuilding",
			"armedbuilding",
			"animatedbuilding",
			"commandpost"
		};
		bool is_valid_base_class = false;
		const EntityClass *entity_class = container->FindEntityClass(FNV::Hash(entity_class_name_cstr));
		if (entity_class) {
			const char *base_class_name_cstr = entity_class->GetBaseName().Buffer();
			for (const char *valid_base_class : valid_base_classes) {
				if (strcmp(base_class_name_cstr, valid_base_class) == 0) {
					is_valid_base_class = true;
					break;
				}
			}
		}
		if (!is_valid_base_class) {
			UtilityFunctions::printerr("Cannot import instance ", instance.GetName().Buffer(), " of unknown type ", entity_class_name_cstr);
			return "";
		}

		// Perform the actual scene creation
		UtilityFunctions::print("Creating entity class ", entity_class_name_cstr, " scene");
		LibSWBF2::List<LibSWBF2::String> property_values;
		LibSWBF2::List<uint32_t> property_hashes;
		entity_class->GetAllProperties(property_hashes, property_values);
		String scene_path = scene_dir + String("/") + String(entity_class_name_cstr) + String(".tscn");
		String next_attach_name = "";
		Node *root = memnew(Node);
		root->set_name(entity_class_name_cstr);
		for (size_t pi = 0; pi < property_hashes.Size(); ++ pi) {
			uint32_t property_hash = property_hashes[pi];
			const char *property_value = property_values[pi].Buffer();
			UtilityFunctions::print("DEBUG Property ", property_hash, " (hash) = ", property_value);
			switch (property_hash) {
				case 1204317002: // GeometryName
					// XXX: Load model
					break;
				case 2849035403: // AttachODF
					next_attach_name = property_value;
					break;
				case 1005041674: // AttachToHardpoint
					// XXX: Attachment
					break;
				case 2555738718: // AnimationName
				case 3779456605: // Animation
					UtilityFunctions::print("Skipping animation ", property_value);
					break;
				case 0x5dfdc07f: // SoldierCollision
				case 0xfb2bdf07: // OrdnanceCollision
					// Silently ignore collision masking
					break;
				default:
					UtilityFunctions::print("Skipping unknown property ", property_hash, " (hash) = ", property_value);
					break;
			}
		}
		if (next_attach_name != "") {
			UtilityFunctions::printerr("Skipping AttachODF property ", next_attach_name, " not followed by an AttachToHardpoint property");
		}

		// Save the node as a scene
		Error save_err = save_as_scene(root, scene_path);
		memdelete(root);
		if (save_err) {
			return "";
		}
		entity_class_scenes.insert(entity_class_name_cstr, scene_path);
		return scene_path;
	}

	static void make_parent_and_owner(Node *parent, Node *child) {
		if (Node *old_parent = child->get_parent()) {
			old_parent->remove_child(child);
		}
		parent->add_child(child);
		child->set_owner(parent);
	}

	static Error save_as_scene(Node *node, String scene_path) {
		UtilityFunctions::print("Saving packed scene ", scene_path);
		Ref<PackedScene> scene;
		scene.instantiate();
		Error pack_err = scene->pack(node);
		if (pack_err) {
			UtilityFunctions::printerr("Error packing scene ", pack_err);
			return pack_err;
		}
		if (Error save_err = ResourceSaver::get_singleton()->save(scene, scene_path)) {
			UtilityFunctions::printerr("Error saving scene ", save_err);
			return save_err;
		}
		return Error::OK;
	}

public:
	WorldImporter() {
		UtilityFunctions::print("Creating WorldImporter");
		container = Container::Create();
	}

	~WorldImporter() {
		UtilityFunctions::print("Destroying WorldImporter");
		Container::Delete(container);
	}

	void import_lvl(const String &lvl_filename, const String &scene_dir) {
		UtilityFunctions::print("Importing ", lvl_filename);
		const char *lvl_filename_cstr = lvl_filename.utf8().get_data();

		// Create our container and load our lvl file
		// TODO: I suspect there is a race condition in LibSWBF2 because
		// if we perform any complex work between Container::Create and
		// Container::AddLevel, Container::StartLoading and
		// Container::IsDone work normally but Container::GetLevel returns
		// a nullptr? For now I've moved the directory creation down below...
		uint16_t lvl_handle = container->AddLevel(lvl_filename_cstr);
		container->StartLoading();
		int waited = 0;
		do {
			OS::get_singleton()->delay_msec(1000);
			++ waited;
			UtilityFunctions::print("Waited ", waited, "s for levels to load");
			if (waited >= 60) {
				UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " took longer than 60s to load");
				return;
			}
		} while (!container->IsDone());

		// Verify our lvl contains one or more worlds and import them
		{
			const Level *level = container->GetLevel(lvl_handle);
			if (level == nullptr) {
				UtilityFunctions::printerr("Failed to load level");
				return;
			}
			UtilityFunctions::print("Importing level ", level->GetLevelName().Buffer());
			if (!level->IsWorldLevel()) {
				UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " is not a world level");
				return;
			}

			// Ensure our scene_dir exists
			if(!DirAccess::dir_exists_absolute(scene_dir)) {
				UtilityFunctions::print("Creating scene directory ", scene_dir);
				if (Error e = DirAccess::make_dir_absolute(scene_dir)) {
					UtilityFunctions::printerr("Could not create scene directory ", scene_dir);
					return;
				}
			}

			Node *lvl_root = memnew(Node);
			lvl_root->set_name(lvl_filename.get_file());
			const LibSWBF2::List<World> &worlds = level->GetWorlds();
			for (size_t i = 0; i < worlds.Size(); ++ i) {
				const World &world = worlds[i];
				Node *world_node = import_world(world, scene_dir);
				if (world_node) {
					make_parent_and_owner(lvl_root, world_node);
					UtilityFunctions::print("Adding world to lvl scene");
				} else {
					UtilityFunctions::printerr("World ", world.GetName().Buffer(), " failed to import");
				}
			}
			if (save_as_scene(lvl_root, scene_dir + String("/") + lvl_root->get_name() + String(".tscn")) == Error::OK) {
				UtilityFunctions::print("Import successful");
			}
			memdelete(lvl_root);
		}
	}
};

void LVLImport::_bind_methods() {
	godot::ClassDB::bind_static_method("LVLImport", godot::D_METHOD("import_lvl"), &LVLImport::import_lvl);
}

void LVLImport::import_lvl(const String &lvl_filename, const String &scene_dir) {
	WorldImporter importer;
	importer.import_lvl(lvl_filename, scene_dir);
}

}
