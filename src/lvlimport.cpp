#include "lvlimport.hpp"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
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
		String world_name = String::utf8(world.GetName().Buffer());
		UtilityFunctions::print("Importing world ", world_name);

		Node *world_root = memnew(Node);
		if (world_root == nullptr) {
			UtilityFunctions::printerr("memnew failed to allocate a Node");
			return nullptr;
		}
		world_root->set_name(world_name);

		// Import instances
		const LibSWBF2::List<Instance> &instances = world.GetInstances();
		for (size_t i = 0; i < instances.Size(); ++ i) {
			String instance_name = String::utf8(instances[i].GetName().Buffer());
			String entity_class_name = String::utf8(instances[i].GetEntityClassName().Buffer());
			UtilityFunctions::print("Importing instance ", i, "/", instances.Size(), " ", instance_name);
			Node3D *instance = import_entity_class(entity_class_name, scene_dir);
			if (instance) {
				UtilityFunctions::print("Attaching ", instance_name, " to world");
				instance->set_name(instance_name);
				LibSWBF2::Vector3 pz = instances[i].GetPosition();
				LibSWBF2::Vector4 rz = instances[i].GetRotation();
				instance->set_position(Vector3(pz.m_X, pz.m_Y, pz.m_Z));
				instance->set_quaternion(Quaternion(rz.m_X, rz.m_Y, rz.m_Z, rz.m_W));
				make_parent_and_owner(world_root, instance);
			} else {
				UtilityFunctions::printerr("Failed to import ", instance_name);
			}
		}

		// Import terrain
		if (Node *terrain = import_terrain(world, scene_dir)) {
			make_parent_and_owner(world_root, terrain);
		}


		// XXX: Import skydome

		return world_root;
	}

	Node3D *maybe_instantiate_entity_class(const String &entity_class_name) {
		if (entity_class_scenes.has(entity_class_name)) {
			String scene_path = entity_class_scenes.get(entity_class_name);
			Ref<PackedScene> scene = ResourceLoader::get_singleton()->load(scene_path);
			if (scene->can_instantiate()) {
				Node3D *instance = Node::cast_to<Node3D>(scene->instantiate());
				if (instance == nullptr) {
					UtilityFunctions::printerr("Entity class ", entity_class_name, " scene instantiation failed");
					return nullptr;
				}
				return instance;
			} else {
				UtilityFunctions::printerr("Entity class ", entity_class_name, " scene cannot be instantiated");
			}
		}
		return nullptr;
	}

	MeshInstance3D *import_terrain(const World &world, const String &scene_dir) {
		const LibSWBF2::Terrain *terrain = world.GetTerrain();
		if (terrain) {
			String terrain_name = String::utf8(world.GetTerrainName().Buffer());
			MeshInstance3D *terrain_mesh = memnew(MeshInstance3D);
			terrain_mesh->set_name(terrain_name);

			Ref<ArrayMesh> array_mesh;
			array_mesh.instantiate();

			Array mesh_data;
			mesh_data.resize(Mesh::ArrayType::ARRAY_MAX);

			PackedVector3Array vertex;
			PackedVector3Array normal;
			PackedVector2Array tex_uv;
			PackedInt32Array index;

			uint32_t index_count = 0;
			uint32_t vertex_count = 0;
			uint32_t tex_uv_count = 0;
			uint32_t normal_count = 0;
			uint32_t *index_buffer = nullptr;
			LibSWBF2::Vector3 *vertex_buffer = nullptr;
			LibSWBF2::Vector2 *tex_uv_buffer = nullptr;
			LibSWBF2::Vector3 *normal_buffer = nullptr;

			terrain->GetIndexBuffer(ETopology::TriangleList, index_count, index_buffer);
			terrain->GetVertexBuffer(vertex_count, vertex_buffer);
			terrain->GetUVBuffer(tex_uv_count, tex_uv_buffer);
			terrain->GetNormalBuffer(normal_count, normal_buffer);

			for (uint32_t i = 0; i < vertex_count; ++ i) {
				const LibSWBF2::Vector3 &zzz = vertex_buffer[i];
				vertex.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}

			for (uint32_t i = 0; i < normal_count; ++ i) {
				const LibSWBF2::Vector3 &zzz = normal_buffer[i];
				normal.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}
			for (uint32_t i = 0; i < tex_uv_count; ++ i) {
				const LibSWBF2::Vector2 &zzz = tex_uv_buffer[i];
				tex_uv.push_back(Vector2(zzz.m_X, zzz.m_Y));
			}

			// Note the reversed index orders
			for (uint32_t i = 0; i < index_count - 2; i += 3) {
				index.push_back(index_buffer[i+2]);
				index.push_back(index_buffer[i+1]);
				index.push_back(index_buffer[i+0]);
			}

			mesh_data[Mesh::ArrayType::ARRAY_VERTEX] = vertex;
			mesh_data[Mesh::ArrayType::ARRAY_NORMAL] = normal;
			mesh_data[Mesh::ArrayType::ARRAY_TEX_UV] = tex_uv;
			mesh_data[Mesh::ArrayType::ARRAY_INDEX] = index;

			array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data);

			// XXX Create material
			Ref<StandardMaterial3D> material;
			material.instantiate();
			// material.set_texture(...)
			array_mesh->surface_set_material(0, material);
			terrain_mesh->set_mesh(array_mesh);

			return terrain_mesh;
		}

		return nullptr;
	}

	void segments_to_mesh(MeshInstance3D *mesh_instance, const LibSWBF2::List<Segment> &segments) {
		Ref<ArrayMesh> array_mesh;
		array_mesh.instantiate();

		int surface_index = 0;

		for (size_t si = 0; si < segments.Size(); ++ si) {
			const Segment &segment = segments[si];

			Array mesh_data;
			mesh_data.resize(Mesh::ArrayType::ARRAY_MAX);

			PackedVector3Array vertex;
			PackedVector3Array normal;
			PackedVector2Array tex_uv;
			PackedInt32Array index;

			uint32_t index_count = 0;
			uint32_t vertex_count = 0;
			uint32_t tex_uv_count = 0;
			uint32_t normal_count = 0;
			uint16_t *index_buffer = nullptr;
			LibSWBF2::Vector3 *vertex_buffer = nullptr;
			LibSWBF2::Vector2 *tex_uv_buffer = nullptr;
			LibSWBF2::Vector3 *normal_buffer = nullptr;

			segment.GetIndexBuffer(index_count, index_buffer);
			segment.GetVertexBuffer(vertex_count, vertex_buffer);
			segment.GetUVBuffer(tex_uv_count, tex_uv_buffer);
			segment.GetNormalBuffer(normal_count, normal_buffer);

			ETopology topology = segment.GetTopology();

			if (topology == ETopology::PointList ||
			    topology == ETopology::LineList ||
			    topology == ETopology::LineStrip)
			{
				UtilityFunctions::printerr("Skipping mesh segment with unsupported topology");
				continue;
			}

			if (vertex_count != normal_count || normal_count != tex_uv_count) {
				UtilityFunctions::printerr("Skipping mesh with invalid vertex, normal, tex_uv count: ", vertex_count, " ", normal_count, " ", tex_uv_count);
				continue;
			}

			for (uint32_t i = 0; i < vertex_count; ++ i) {
				const LibSWBF2::Vector3 &zzz = vertex_buffer[i];
				vertex.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}

			for (uint32_t i = 0; i < normal_count; ++ i) {
				const LibSWBF2::Vector3 &zzz = normal_buffer[i];
				normal.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}
			for (uint32_t i = 0; i < tex_uv_count; ++ i) {
				const LibSWBF2::Vector2 &zzz = tex_uv_buffer[i];
				tex_uv.push_back(Vector2(zzz.m_X, zzz.m_Y));
			}

			if (topology == ETopology::TriangleList) {
				for (uint32_t i = 0; i < index_count; ++ i) {
					index.push_back(index_buffer[i]);
				}
			}

			if (topology == ETopology::TriangleStrip) {
				// Convert strip to list
				for (uint32_t i = 0; i < index_count - 2; ++ i) {
					if (i % 2 == 1) {
						index.push_back(index_buffer[i+0]);
						index.push_back(index_buffer[i+1]);
						index.push_back(index_buffer[i+2]);
					} else {
						index.push_back(index_buffer[i+0]);
						index.push_back(index_buffer[i+2]);
						index.push_back(index_buffer[i+1]);
					}
				}
			}

			if (topology == ETopology::TriangleFan) {
				if (index_count < 3) {
					UtilityFunctions::printerr("Skipping mesh segment triangle fan with only ", index_count, " indices");
				}
				// Convert fan to list
				uint16_t hub = index_buffer[0];
				for (uint32_t i = 1; i < index_count - 1; i += 2) {
					index.push_back(hub);
					index.push_back(index_buffer[i+0]);
					index.push_back(index_buffer[i+1]);
				}
			}

			UtilityFunctions::printerr("Skipping mesh segment with unknown topology ", (int32_t)topology);

			mesh_data[Mesh::ArrayType::ARRAY_VERTEX] = vertex;
			mesh_data[Mesh::ArrayType::ARRAY_NORMAL] = normal;
			mesh_data[Mesh::ArrayType::ARRAY_TEX_UV] = tex_uv;
			mesh_data[Mesh::ArrayType::ARRAY_INDEX] = index;

			array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data);

			// XXX Create material
			Ref<StandardMaterial3D> material;
			material.instantiate();
			// material.set_texture(...)
			array_mesh->surface_set_material(surface_index, material);

			surface_index += 1;
		}

		mesh_instance->set_mesh(array_mesh);
	}

	void populate_model(Node3D *root, const String &model_name, const String &override_texture, const String &scene_dir) {
		UtilityFunctions::print("Populating model ", model_name);

		// Load the SWBF2 Model representation
		const Model *model = container->FindModel(model_name.utf8().get_data());
		if (model == nullptr) {
			UtilityFunctions::printerr("Could not find model ", model_name);
			return;
		}

		// Convert the SWBF2 skeleton to something we can work with.
		// Since we do not import animations I've chosen to convert
		// all bones to Node3D. The original LVLImport does this with
		// static meshes but handles skinned meshes with real Unity
		// skeletons
		LibSWBF2::List<Bone> bones;
		if (model->GetSkeleton(bones)) {
			HashMap<String, Node3D *> named_bones;
			// Create bone nodes
			for (size_t i = 0; i < bones.Size(); ++ i) {
				String bone_name = String::utf8(bones[i].m_BoneName.Buffer());
				Node3D *bone_node = memnew(Node3D);
				if (bone_node == nullptr) {
					UtilityFunctions::printerr("Failed to create bone node");
					continue;
				}
				bone_node->set_name(bone_name);
				named_bones.insert(bone_name, bone_node);
				make_parent_and_owner(root, bone_node);
			}
			// Organize bone hierarchy 
			for (size_t i = 0; i < bones.Size(); ++ i) {
				String bone_name = String::utf8(bones[i].m_BoneName.Buffer());
				Node3D *bone_node = named_bones.get(bone_name);
				if (bone_node == nullptr) {
					UtilityFunctions::printerr("Failed to find bone ", bone_name);
					continue;
				}
				String parent_name = String::utf8(bones[i].m_Parent.Buffer());
				if (parent_name.length() > 0) {
					Node3D *parent_bone = named_bones.get(parent_name);
					if (parent_bone == nullptr) {
						UtilityFunctions::printerr("Bone ", bone_name, " references a parent ", parent_name, " that does not exist");
					} else {
						make_parent_and_owner(parent_bone, bone_node);
					}
				}
				// Set local position and rotation
				// Note the inversion of the X axis
				LibSWBF2::Vector3 pz = bones[i].m_Position;
				LibSWBF2::Vector4 rz = bones[i].m_Rotation;
				bone_node->set_position(Vector3(pz.m_X, pz.m_Y, pz.m_Z));
				bone_node->set_quaternion(Quaternion(rz.m_X, rz.m_Y, rz.m_Z, rz.m_W));
			}
		}

		// Organize SWBF2 mesh segments by bone
		LibSWBF2::List<Segment> model_segments = model->GetSegments();
		HashMap<String, LibSWBF2::List<Segment>> bone_segments;
		for (size_t i = 0; i < model_segments.Size(); ++ i) {
			const Segment &segment = model_segments[i];
			String segment_bone = String::utf8(segment.GetBone().Buffer());
			if (!bone_segments.has(segment_bone)) {
				bone_segments.insert(segment_bone, LibSWBF2::List<Segment>());
			}
			LibSWBF2::List<Segment> *bsl = bone_segments.getptr(segment_bone);
			bsl->Add(segment);
		}

		// Create mesh nodes
		for (const auto &key_pair : bone_segments) {
			const String &bone_name = key_pair.key;
			const LibSWBF2::List<Segment> &segments = key_pair.value;
			MeshInstance3D *mesh = memnew(MeshInstance3D);
			String mesh_name = String(model_name) + String("_") + bone_name + String("_") + "mesh";
			mesh->set_name(mesh_name);
			segments_to_mesh(mesh, segments);

			// Parent our mesh to the bone node
			Node *bone_node = root->find_child(bone_name, true, true);
			if (bone_node) {
				UtilityFunctions::printerr("Attaching mesh ", mesh_name, " to ", bone_name);
				make_parent_and_owner(bone_node, mesh);
			} else {
				make_parent_and_owner(root, mesh);
				UtilityFunctions::printerr("Could not find bone node ", bone_name, "; attaching ", mesh_name, " to model root");
			}
		}

		// XXX: ModelLoader.AddCollision
	}

	Node3D *import_entity_class(const String &entity_class_name, const String &scene_dir) {
		UtilityFunctions::print("Importing EntityClass ", entity_class_name);

		// If we've already loaded this entity class, return an instantiation
		if (Node3D *instance = maybe_instantiate_entity_class(entity_class_name)) {
			return instance;
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
		const EntityClass *entity_class = container->FindEntityClass(FNV::Hash(entity_class_name.utf8().get_data()));
		String base_class_name = "NONE";
		if (entity_class) {
			base_class_name = String::utf8(entity_class->GetBaseName().Buffer());
			for (const char *valid_base_class : valid_base_classes) {
				if (strcmp(base_class_name.utf8(), valid_base_class) == 0) {
					is_valid_base_class = true;
					break;
				}
			}
		}
		if (!is_valid_base_class) {
			UtilityFunctions::printerr("Cannot import entity class ", entity_class_name, " of unknown base class ", base_class_name);
			return nullptr;
		}

		// Perform the actual scene creation
		UtilityFunctions::print("Creating entity class ", entity_class_name, " scene");
		LibSWBF2::List<LibSWBF2::String> property_values;
		LibSWBF2::List<uint32_t> property_hashes;
		entity_class->GetAllProperties(property_hashes, property_values);
		String scene_path = scene_dir + String("/") + String(entity_class_name) + String(".tscn");
		String next_attach_entity_class = "";
		Node3D *root = memnew(Node3D);
		if (root == nullptr) {
			UtilityFunctions::printerr("memnew failed to allocate a Node3D");
			return nullptr;
		}
		root->set_name(entity_class_name); // Attachments seem to have no name, so we need a default
		for (size_t pi = 0; pi < property_hashes.Size(); ++ pi) {
			uint32_t property_hash = property_hashes[pi];
			String property_value = String::utf8(property_values[pi].Buffer());
			switch (property_hash) {
				case 1204317002: { // GeometryName
					UtilityFunctions::print("Attaching model ", property_value, " to ", entity_class_name);
					// Determine our texture, which is a separate property
					String override_texture = "";
					for (size_t i = 0; i < property_hashes.Size(); ++ i) {
						if (property_hashes[i] == 165377196) {
							override_texture = String::utf8(property_values[i].Buffer());
							break;
						}
					}
					populate_model(root, property_value, override_texture, scene_dir);
					break;
				}
				case 2849035403: // AttachODF
					next_attach_entity_class = property_value;
					break;
				case 1005041674: // AttachToHardpoint
					if (next_attach_entity_class.length() > 0) {
						UtilityFunctions::print("Attaching ", next_attach_entity_class, " to hardpoint ", property_value);
						// Find the child we are attaching to. Default to the root
						Node *attach_to = root;
						if (Node *attach_to_child = root->find_child(property_value, true, true)) {
							attach_to = attach_to_child;
						} else {
							UtilityFunctions::printerr("AttachToHardpoint child ", property_value, " not found; attaching to root");
						}
						Node *child = import_entity_class(next_attach_entity_class, scene_dir);
						if (child) {
							make_parent_and_owner(attach_to, child);
						}
						next_attach_entity_class = "";
					} else {
						UtilityFunctions::printerr("Skipping AttachToHardpoint ", property_value, " not preceeded by an AttachODF");
					}
					break;
				case 2555738718: // AnimationName
				case 3779456605: // Animation
					UtilityFunctions::print("Skipping animation ", property_value);
					break;
				case 1576910975: // SoldierCollision
				case 4213956359: // OrdnanceCollision
					// Silently ignore collision masking
					break;
				case 165377196: // OverrideTexture
					// Silently ignore, only relevant in GeometryName
					break;
				default:
					UtilityFunctions::print("Skipping unknown property ", property_hash, " (hash) = ", property_value);
					break;
			}
		}
		if (next_attach_entity_class != "") {
			UtilityFunctions::printerr("Skipping AttachODF property ", next_attach_entity_class, " not followed by an AttachToHardpoint property");
		}

		// Save the node as a scene
		Error save_err = save_as_scene(root, scene_path);
		if (save_err == Error::OK) {
			entity_class_scenes.insert(entity_class_name, scene_path);
		}

		// We still want to return an instantiation of the scene rather than our node
		memdelete(root);
		return maybe_instantiate_entity_class(entity_class_name);
	}

	static void take_ownership(Node *parent, Node *child) {
		Node *scene_owner = parent->get_owner();
		if (scene_owner == nullptr) {
			scene_owner = parent;
		}
		// Do NOT re-parent nodes owned by a packed scene. This would
		// lift that scene's children out, as if they had been made local.
		Node *current_owner = child->get_owner();
		if (current_owner == nullptr || current_owner->get_scene_file_path().length() == 0) {
			child->set_owner(scene_owner);
		}
		for (size_t i = 0; i < child->get_child_count(); ++ i) {
			take_ownership(scene_owner, child->get_child(i));
		}
	}

	static void make_parent_and_owner(Node *parent, Node *child) {
		if (Node *old_parent = child->get_parent()) {
			old_parent->remove_child(child);
			child->set_owner(nullptr);
		}
		parent->add_child(child);
		take_ownership(parent, child);
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
			UtilityFunctions::print("Importing level ", String::utf8(level->GetLevelName().Buffer()));
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
			if (lvl_root == nullptr) {
				UtilityFunctions::printerr("memnew failed to allocate a Node");
				return;
			}
			lvl_root->set_name(lvl_filename.get_file());
			const LibSWBF2::List<World> &worlds = level->GetWorlds();
			for (size_t i = 0; i < worlds.Size(); ++ i) {
				const World &world = worlds[i];
				Node *world_node = import_world(world, scene_dir);
				if (world_node) {
					make_parent_and_owner(lvl_root, world_node);
					UtilityFunctions::print("Adding world to lvl scene");
				} else {
					UtilityFunctions::printerr("World ", String::utf8(world.GetName().Buffer()), " failed to import");
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
