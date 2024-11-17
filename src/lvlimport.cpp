#include "lvlimport.hpp"
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <LibSWBF2.h>

using namespace LibSWBF2;

namespace godot {

class WorldImporter {
	Container *container;
	HashMap<String, String> entity_class_scenes;
	HashMap<String, String> images;

	Node *import_world(const World &world, const String &scene_dir) {
		String world_name = String::utf8(world.GetName().Buffer());
		printdebug("Importing world ", world_name);

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
			printdebug("Importing instance ", i, "/", instances.Size(), " ", instance_name);
			Node3D *instance = import_entity_class(entity_class_name, scene_dir);
			if (instance) {
				printdebug("Attaching ", instance_name, " to world");
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


		// Import skydome
		if (Node *skydome = import_skydome(world, scene_dir)) {
			make_parent_and_owner(world_root, skydome);
		}

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

	Node3D *import_skydome(const World &world, const String &scene_dir) {
		printdebug("Importing skydome");
		const LibSWBF2::Config *skydome_config = container->FindConfig(EConfigType::Skydome, FNV::Hash(world.GetSkyName()));
		if (skydome_config == nullptr) {
			return nullptr;
		}

		Node3D *skydome = memnew(Node3D);
		if (skydome == nullptr) {
			UtilityFunctions::printerr("Failed to create skydome node");
			return nullptr;
		}
		skydome->set_name("skydome");
		skydome->set_scale(Vector3(300, 300, 300));

		// TODO: This LibSWBF2 code may throw an exception! But Godot does not compile with exceptions!
		const LibSWBF2::Field &dome_info = skydome_config->GetField(FNV::Hash("DomeInfo"));
		LibSWBF2::List<const LibSWBF2::Field *> dome_models = dome_info.m_Scope.GetFields(FNV::Hash("DomeModel"));
		printdebug("Skydome has ", dome_models.Size(), " dome models");
		for (size_t i = 0; i < dome_models.Size(); ++ i) {
			String model_name = String::utf8(dome_models[i]->m_Scope.GetField(FNV::Hash("Geometry")).GetString().Buffer());
			// This alternate behavior mimicks that of the .NET Scope wrapper from LibSWBF2 definition of GetString
			if (model_name == "") {
				model_name = String::utf8(dome_models[i]->GetString(FNV::Hash("Geometry")).Buffer());
			}
			printdebug("Importing skydome model ", i, "/",  dome_models.Size(), " ", model_name);
			populate_model(skydome, model_name, "", scene_dir);
		}

		// Create sky objects
		LibSWBF2::List<const LibSWBF2::Field *> sky_objects = skydome_config->GetFields(FNV::Hash("SkyObject"));
		printdebug("Skydome has ", sky_objects.Size(), " sky objects");
		for (size_t i = 0; i < sky_objects.Size(); ++ i) {
			String model_name = String::utf8(sky_objects[i]->m_Scope.GetField(FNV::Hash("Geometry")).GetString().Buffer());
			// This alternate behavior mimicks that of the .NET Scope wrapper from LibSWBF2 definition of GetString
			if (model_name == "") {
				model_name = String::utf8(sky_objects[i]->GetString(FNV::Hash("Geometry")).Buffer());
			}
			printdebug("Importing sky object ", i, "/",  sky_objects.Size(), " ", model_name);
			populate_model(skydome, model_name, "", scene_dir);
		}

		return skydome;
	}

	MeshInstance3D *import_terrain(const World &world, const String &scene_dir) {
		const LibSWBF2::Terrain *terrain = world.GetTerrain();
		if (terrain == nullptr) {
			return nullptr;
		}

		String terrain_name = String::utf8(world.GetTerrainName().Buffer());

		// Create the terrain mesh
		MeshInstance3D *terrain_mesh = memnew(MeshInstance3D);
		terrain_mesh->set_name(terrain_name);

		Ref<ArrayMesh> array_mesh;
		array_mesh.instantiate();

		Array mesh_data;
		mesh_data.resize(Mesh::ArrayType::ARRAY_MAX);

		PackedVector3Array vertex;
		PackedVector3Array normal;
		PackedVector2Array tex_uv;
		PackedVector2Array blend_uv;
		PackedInt32Array index;

		uint32_t index_count = 0;
		uint32_t vertex_count = 0;
		uint32_t tex_uv_count = 0;
		uint32_t *index_buffer = nullptr;
		LibSWBF2::Vector3 *vertex_buffer = nullptr;
		LibSWBF2::Vector2 *tex_uv_buffer = nullptr;

		terrain->GetIndexBuffer(ETopology::TriangleList, index_count, index_buffer);
		terrain->GetVertexBuffer(vertex_count, vertex_buffer);
		terrain->GetUVBuffer(tex_uv_count, tex_uv_buffer);

		float minx = FLT_MAX;
		float minz = FLT_MAX;
		float maxx = FLT_MIN;
		float maxz = FLT_MIN;

		for (uint32_t i = 0; i < vertex_count; ++ i) {
			const LibSWBF2::Vector3 &zzz = vertex_buffer[i];
			vertex.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			minx = MIN(minx, zzz.m_X);
			minz = MIN(minz, zzz.m_Z);
			maxx = MAX(maxx, zzz.m_X);
			maxz = MAX(maxz, zzz.m_Z);
		}

		for (uint32_t i = 0; i < vertex_count; ++ i) {
			const LibSWBF2::Vector3 &zzz = vertex_buffer[i];
			blend_uv.push_back(Vector2((zzz.m_X - minx) / (maxx - minx), (zzz.m_Z - minz) / (maxz - minz)));
		}

		for (uint32_t i = 0; i < tex_uv_count; ++ i) {
			const LibSWBF2::Vector2 &zzz = tex_uv_buffer[i];
			tex_uv.push_back(Vector2(zzz.m_X, zzz.m_Y));
		}

		// Calculate normals because what comes out of LibSWBF2 is junk
		normal.resize(vertex_count);
		normal.fill(Vector3());

		// Yeah... If we don't do this we can't calculate normals
		// correctly. I know it's bad, you know it's bad, let's just move on.
		printdebug("Brute forcing terrain indices... This will take a minute");
		PackedByteArray visited;
		visited.resize(index_count);
		visited.fill(0);
		for (uint32_t i = 0; i < index_count; ++ i) {
			const Vector3 &iii = vertex[index_buffer[i]];
			if (visited[i] == 0) {
				for (uint32_t j = i + 1; j < index_count; ++ j) {
					const Vector3 &jjj = vertex[index_buffer[j]];
					if (iii.distance_squared_to(jjj) < 0.01) {
						index_buffer[j] = index_buffer[i];
						visited[j] = 1;
					}
				}
			}
		}

		// Note the reversed index orders
		for (uint32_t i = 0; i < index_count - 2; i += 3) {
			int v0 = index_buffer[i+2];
			int v1 = index_buffer[i+1];
			int v2 = index_buffer[i+0];
			index.push_back(v0);
			index.push_back(v1);
			index.push_back(v2);

			Vector3 n0 = vertex[v1] - vertex[v0];
			Vector3 n1 = vertex[v2] - vertex[v0];
			Vector3 fn = n0.cross(n1).normalized();
			normal[v0] -= fn;
			normal[v1] -= fn;
			normal[v2] -= fn;
		}

		for (uint32_t i = 0; i < vertex_count; ++ i) {
			normal[i].normalize();
		}

		mesh_data[Mesh::ArrayType::ARRAY_VERTEX] = vertex;
		mesh_data[Mesh::ArrayType::ARRAY_NORMAL] = normal;
		mesh_data[Mesh::ArrayType::ARRAY_TEX_UV] = tex_uv;
		mesh_data[Mesh::ArrayType::ARRAY_TEX_UV2] = blend_uv;
		mesh_data[Mesh::ArrayType::ARRAY_INDEX] = index;

		array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data);

		// Create the terrain material
		Ref<ShaderMaterial> terrain_material;
		terrain_material.instantiate();
		terrain_material->set_shader(ResourceLoader::get_singleton()->load("res://terrain_shader.gdshader"));

		// Blend Maps
		uint32_t blend_map_dim = 0;
		uint32_t blend_map_layers = 0; // Do we care? Right now I ignore layers
		uint8_t *blend_map_buffer = nullptr;
		terrain->GetBlendMap(blend_map_dim, blend_map_layers, blend_map_buffer);

		for (int i = 0; i < 4; ++ i) {
			PackedByteArray packed_buffer;
			size_t size = blend_map_dim * blend_map_dim * sizeof(*blend_map_buffer) * 4;
			packed_buffer.resize(size);
			uint8_t *ptrw = packed_buffer.ptrw();
			for (int h = 0; h < blend_map_dim; ++ h) {
			for (int w = 0; w < blend_map_dim; ++ w) {
				// Wtf is this indexing scheme???
				for (int j = 0; j < 4; ++ j) {
					if (i * 4 + j >= blend_map_layers) {
						continue;
					}
					ptrw[(blend_map_dim * h + w) * 4 + j] = blend_map_buffer[blend_map_layers * (blend_map_dim * h + w) + i * 4 + j];
				}
			}}

			Ref<Image> image = Image::create_from_data(blend_map_dim, blend_map_dim, false, Image::Format::FORMAT_RGBA8, packed_buffer);
			image->save_png(scene_dir + String("/") + String("terrain_blend_map_") + itos(i) + String(".png"));
			terrain_material->set_shader_parameter("BlendMap" + itos(i), ImageTexture::create_from_image(image));
		}

		// Blend Layers
		// TODO: Does SWBF2 have terrain bump mapping?
		const LibSWBF2::List<LibSWBF2::String> &blend_layer_texture_names = terrain->GetLayerTextures();
		for (size_t i = 0; i < blend_layer_texture_names.Size(); ++ i) {
			const LibSWBF2::String &texture_name_z = blend_layer_texture_names[i];
			String texture_name = String::utf8(texture_name_z.Buffer());
			const LibSWBF2::Texture *texture = container->FindTexture(texture_name_z);
			if (texture) {
				Ref<Texture2D> albedo_texture = import_texture(*texture, scene_dir);
				terrain_material->set_shader_parameter("BlendLayer" + itos(i), albedo_texture);
			} else {
				UtilityFunctions::printerr("Failed to find terrain layer image ", texture_name);
			}
		}

		array_mesh->surface_set_material(0, terrain_material);
		terrain_mesh->set_mesh(array_mesh);

		return terrain_mesh;
	}

	Ref<Image> maybe_load_image(const String &image_name) {
		if (images.has(image_name)) {
			String image_path = images.get(image_name);
			Ref<Image> image;
			image.instantiate();
			Error e = image->load(image_path);
			if (e == Error::OK) {
				return image;
			} else {
				UtilityFunctions::printerr("Failed to import image ", image_name);
			}
		}
		return Ref<Image>{};
	}

	Ref<Texture2D> import_texture(const LibSWBF2::Texture &texture, const String &scene_dir) {
		String texture_name = String::utf8(texture.GetName().Buffer());

		String image_path = scene_dir + String("/") + String(texture_name) + String(".png");

		Ref<Image> image = maybe_load_image(texture_name);
		if (!image.is_valid()) {
			printdebug("Importing texture ", texture_name);

			uint16_t width = 0;
			uint16_t height = 0;
			const uint8_t *buffer = nullptr;
			texture.GetImageData(ETextureFormat::R8_G8_B8_A8, 0, width, height, buffer);

			PackedByteArray packed_buffer;
			size_t size = width * height * sizeof(*buffer) * 4;
			packed_buffer.resize(size);
			memcpy(packed_buffer.ptrw(), buffer, size);

			image = Image::create_from_data(width, height, false, Image::Format::FORMAT_RGBA8, packed_buffer);
			Error e = image->save_png(image_path);
			if (e == Error::OK) {
				images.insert(texture_name, image_path);
			} else {
				UtilityFunctions::printerr("Failed to save ", texture_name, " as ", image_path);
			}
		}

		return ImageTexture::create_from_image(image);
	}

	Ref<StandardMaterial3D> import_material(const LibSWBF2::Material &material, const String &scene_dir) {
		Ref<StandardMaterial3D> standard_material;
		standard_material.instantiate();

		// All material types have an albedo texture
		if (const LibSWBF2::Texture *texture = material.GetTexture(0)) {
			Ref<Texture2D> albedo_texture = import_texture(*texture, scene_dir);
			standard_material->set_texture(BaseMaterial3D::TextureParam::TEXTURE_ALBEDO, albedo_texture);
		} else {
			UtilityFunctions::printerr("Failed to get albedo map texture for material");
		}

		// TODO: Other textures? Specular?
		// I'm assuming this matches the order of textures used by XSI as documented here:
		// https://sites.google.com/site/swbf2modtoolsdocumentation/misc_documentation
		// I am not confident LibSWBF2 correctly sets the BumpMap flag, so attempt to load
		// the second image of every material as normal map image.
		EMaterialFlags material_flags = material.GetFlags();
		//if ((uint32_t)material_flags & (uint32_t)EMaterialFlags::BumpMap) {
			if (const LibSWBF2::Texture *texture = material.GetTexture(1)) {
				Ref<Texture2D> normal_texture = import_texture(*texture, scene_dir);
				standard_material->set_texture(BaseMaterial3D::TextureParam::TEXTURE_NORMAL, normal_texture);
			} else if ((uint32_t)material_flags & (uint32_t)EMaterialFlags::BumpMap) {
				UtilityFunctions::printerr("Failed to get normal map texture for material");
			}
		//}

		if ((uint32_t)material_flags & (uint32_t)EMaterialFlags::Transparent) {
			standard_material->set_transparency(BaseMaterial3D::Transparency::TRANSPARENCY_ALPHA);
		}

		// This is pretty basic...
		standard_material->set_specular(0);
		standard_material->set_metallic(0);

		return standard_material;
	}

	void segments_to_mesh(MeshInstance3D *mesh_instance, const LibSWBF2::List<Segment> &segments, const String &override_texture, const String &scene_dir) {
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

			if (topology == ETopology::PointList ||
			    topology == ETopology::LineList ||
			    topology == ETopology::LineStrip)
			{
				UtilityFunctions::printerr("Skipping mesh segment with unsupported topology");
				continue;
			} else if (topology == ETopology::TriangleList) {
				for (uint32_t i = 0; i < index_count; ++ i) {
					index.push_back(index_buffer[i]);
				}
			} else if (topology == ETopology::TriangleStrip) {
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
			} else if (topology == ETopology::TriangleFan) {
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
			} else {
				UtilityFunctions::printerr("Skipping mesh segment with unknown topology ", (int32_t)topology);
			}

			mesh_data[Mesh::ArrayType::ARRAY_VERTEX] = vertex;
			mesh_data[Mesh::ArrayType::ARRAY_NORMAL] = normal;
			mesh_data[Mesh::ArrayType::ARRAY_TEX_UV] = tex_uv;
			mesh_data[Mesh::ArrayType::ARRAY_INDEX] = index;

			array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data);

			array_mesh->surface_set_material(surface_index, import_material(segment.GetMaterial(), scene_dir));

			surface_index += 1;
		}

		mesh_instance->set_mesh(array_mesh);
	}

	void populate_model(Node3D *root, const String &model_name, const String &override_texture, const String &scene_dir) {
		printdebug("Populating model ", model_name);

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
			if (mesh == nullptr) {
				UtilityFunctions::printerr("memnew failed to allocate a MeshInstance3D");
				continue;
			}
			String mesh_name = String(model_name) + String("_") + bone_name + String("_") + "mesh";
			mesh->set_name(mesh_name);
			// TODO: LVLImport only applies override_texture to skinned meshes (bone_name == ""). Why?
			segments_to_mesh(mesh, segments, override_texture, scene_dir);

			// Parent our mesh to the bone node
			Node *bone_node = root->find_child(bone_name, true, true);
			if (bone_node) {
				printdebug("Attaching mesh ", mesh_name, " to ", bone_name);
				make_parent_and_owner(bone_node, mesh);
			} else {
				make_parent_and_owner(root, mesh);
				UtilityFunctions::printerr("Could not find bone node ", bone_name, "; attaching ", mesh_name, " to model root");
			}
		}

		// Create collision bodies
		// Note: we use the Ordnance collision bodies for everything
		// TODO This still returns some weird primitives, such as the rock barricades on geo1
		LibSWBF2::List<LibSWBF2::CollisionPrimitive> collision_primitives = model->GetCollisionPrimitives(ECollisionMaskFlags::Ordnance);
		for (size_t i = 0; i < collision_primitives.Size(); ++ i) {
			const LibSWBF2::CollisionPrimitive &collision_primitive = collision_primitives[i];
			String parent_name = String::utf8(collision_primitive.GetParentName().Buffer());

			StaticBody3D *static_body = memnew(StaticBody3D);
			if (static_body == nullptr) {
				UtilityFunctions::printerr("memnew failed to allocate a StaticBody3D");
				continue;
			}
			static_body->set_name(parent_name + "_collision_primitive");

			LibSWBF2::Vector3 pz = collision_primitive.GetPosition();
			LibSWBF2::Vector4 rz = collision_primitive.GetRotation();
			static_body->set_position(Vector3(pz.m_X, pz.m_Y, pz.m_Z));
			static_body->set_quaternion(Quaternion(rz.m_X, rz.m_Y, rz.m_Z, rz.m_W));

			Node *parent_node = root->find_child(parent_name, true, true);
			if (parent_node) {
				printdebug("Attaching collision primitive to ", parent_name);
				make_parent_and_owner(parent_node, static_body);
			} else {
				make_parent_and_owner(root, static_body);
				UtilityFunctions::printerr("Could not find parent node ", parent_name, "; attaching collision primitive to model root");
			}

			CollisionShape3D *collision_shape = memnew(CollisionShape3D);
			if (collision_shape == nullptr) {
				UtilityFunctions::printerr("memnew failed to allocate a CollisionShape3D");
				continue;
			}
			collision_shape->set_name(parent_name + "_collision_shape");

			ECollisionPrimitiveType pt = collision_primitive.GetPrimitiveType();
			switch (pt) {
				case ECollisionPrimitiveType::Cube: {
					float sx = 0.0f, sy = 0.0f, sz = 0.0f;
					collision_primitive.GetCubeDims(sx, sy, sz);
					Ref<BoxShape3D> shape;
					shape.instantiate();
					shape->set_size(Vector3(sx * 2, sy * 2, sz * 2));
					collision_shape->set_shape(shape);
					break;
				}
				case ECollisionPrimitiveType::Cylinder: {
					float sr = 0.0f, sh = 0.0f;
					collision_primitive.GetCylinderDims(sr, sh);
					Ref<CylinderShape3D> shape;
					shape.instantiate();
					shape->set_radius(sr);
					shape->set_height(sh);
					collision_shape->set_shape(shape);
					break;
				}
				case ECollisionPrimitiveType::Sphere: {
					float sr = 0.0f;
					collision_primitive.GetSphereRadius(sr);
					Ref<SphereShape3D> shape;
					shape.instantiate();
					shape->set_radius(sr);
					collision_shape->set_shape(shape);
					break;
				}
				default:
					UtilityFunctions::printerr("Skipping unsupported collision primitive type ", (int)pt);
					break;
			}
			make_parent_and_owner(static_body, collision_shape);
		}

		// A model always has a collision mesh object, but it may be empty
		do {
			const LibSWBF2::CollisionMesh &collision_mesh = model->GetCollisionMesh();
			uint32_t index_count = 0;
			uint16_t *index_buffer = nullptr;
			collision_mesh.GetIndexBuffer(ETopology::TriangleList, index_count, index_buffer);
			if (index_count > 0) {
				uint32_t vertex_count = 0;
				LibSWBF2::Vector3 *vertex_buffer = nullptr;
				collision_mesh.GetVertexBuffer(vertex_count, vertex_buffer);
				StaticBody3D *static_body = memnew(StaticBody3D);
				if (static_body == nullptr) {
					UtilityFunctions::printerr("memnew failed to allocate a StaticBody3D");
					break;
				}
				static_body->set_name("collision_mesh");
				make_parent_and_owner(root, static_body);

				CollisionShape3D *collision_shape = memnew(CollisionShape3D);
				if (collision_shape == nullptr) {
					UtilityFunctions::printerr("memnew failed to allocate a CollisionShape3D");
					break;
				}
				collision_shape->set_name("collision_mesh_shape");
				make_parent_and_owner(static_body, collision_shape);

				Ref<ConcavePolygonShape3D> mesh_shape;
				mesh_shape.instantiate();

				PackedVector3Array mesh_faces;
				mesh_faces.resize(index_count);

				for (size_t i = 0; i < index_count; ++ i) {
					const LibSWBF2::Vector3 &v = vertex_buffer[index_buffer[i]];
					mesh_faces[i] = Vector3(v.m_X, v.m_Y, v.m_Z);
				}

				mesh_shape->set_faces(mesh_faces);
				mesh_shape->set_backface_collision_enabled(true);

				collision_shape->set_shape(mesh_shape);
			}
		} while (0);
	}

	Node3D *import_entity_class(const String &entity_class_name, const String &scene_dir) {
		printdebug("Importing EntityClass ", entity_class_name);

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
		printdebug("Creating entity class ", entity_class_name, " scene");
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
					printdebug("Attaching model ", property_value, " to ", entity_class_name);
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
						printdebug("Attaching ", next_attach_entity_class, " to hardpoint ", property_value);
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
					printdebug("Skipping animation ", property_value);
					break;
				case 1576910975: // SoldierCollision
				case 4213956359: // OrdnanceCollision
					// Silently ignore collision masking
					break;
				case 165377196: // OverrideTexture
					// Silently ignore, only relevant in GeometryName
					break;
				case 2714356677: // FoleyFXGroups
					break;
				default:
					UtilityFunctions::printerr("Skipping unknown property ", property_hash, " (hash) = ", property_value);
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
		printdebug("Saving packed scene ", scene_path);
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

	template <typename... Args> static void printdebug(const Variant &p_arg1, Args&&... p_args) {
		if (false)
			UtilityFunctions::print(p_arg1, std::forward<Args>(p_args)...);
	}

public:
	WorldImporter() {
		printdebug("Creating WorldImporter");
		container = Container::Create();
	}

	~WorldImporter() {
		printdebug("Destroying WorldImporter");
		Container::Delete(container);
	}

	void import_lvl(const String &lvl_filename, const String &scene_dir) {
		printdebug("Importing ", lvl_filename);
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
			printdebug("Waited ", waited, "s for levels to load");
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
			printdebug("Importing level ", String::utf8(level->GetLevelName().Buffer()));
			if (!level->IsWorldLevel()) {
				UtilityFunctions::printerr("Canceling import because ", lvl_filename_cstr, " is not a world level");
				return;
			}

			// Ensure our scene_dir exists
			if(!DirAccess::dir_exists_absolute(scene_dir)) {
				printdebug("Creating scene directory ", scene_dir);
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
					printdebug("Adding world to lvl scene");
				} else {
					UtilityFunctions::printerr("World ", String::utf8(world.GetName().Buffer()), " failed to import");
				}
			}
			if (save_as_scene(lvl_root, scene_dir + String("/") + lvl_root->get_name() + String(".tscn")) == Error::OK) {
				printdebug("Import successful");
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
