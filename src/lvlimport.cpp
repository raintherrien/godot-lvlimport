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
#include <LibSWBF2/API.h>
#include <float.h>

using namespace LibSWBF2;

namespace godot {

class WorldImporter {
	Container_Owned *container;
	HashMap<String, String> entity_class_scenes;
	HashMap<String, Ref<ImageTexture>> textures;
	HashMap<String, Ref<StandardMaterial3D>> materials; // key = albedo texture name

	template<typename Fn, typename ...Args>
	static String api_str_to_godot(Fn &&fn, Args && ...args)
	{
		size_t len = fn(args..., nullptr, 0);
		std::string buffer(len + 1, '\0');
		fn(args..., buffer.data(), len + 1);
		return String::utf8(buffer.c_str());
	}

	Node *import_world(const World *world, const String &scene_dir) {
		String world_name = api_str_to_godot(World_GetName, world);
		printdebug("Importing world ", world_name);

		Node *world_root = memnew(Node);
		if (world_root == nullptr) {
			UtilityFunctions::printerr("memnew failed to allocate a Node");
			return nullptr;
		}
		world_root->set_name(world_name);

		// Import instances
		TList<const Instance> instances = World_GetInstancesT(world);
		for (size_t i = 0; i < instances.size(); ++ i) {
			const Instance *instance = instances.at(i);
			String instance_name = api_str_to_godot(Instance_GetName, instance);
			String entity_class_name = api_str_to_godot(Instance_GetEntityClassName, instance);
			printdebug("Importing instance ", i, "/", instances.size(), " ", instance_name);
			Node3D *instance_node = import_entity_class(entity_class_name, scene_dir);
			if (instance_node) {
				printdebug("Attaching ", instance_name, " to world");
				instance_node->set_name(instance_name);
				LibSWBF2::Vector3 pz = Instance_GetPosition(instance);
				LibSWBF2::Vector4 rz = Instance_GetRotation(instance);
				instance_node->set_position(Vector3(pz.m_X, pz.m_Y, pz.m_Z));
				instance_node->set_quaternion(Quaternion(rz.m_X, rz.m_Y, rz.m_Z, rz.m_W));
				make_parent_and_owner(world_root, instance_node);
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

	Node3D *import_skydome(const World *world, const String &scene_dir) {
		printdebug("Importing skydome");
		const Config *skydome_config = Container_FindConfig(
				container,
				EConfigType::Skydome,
				FNVHashString(api_str_to_godot(World_GetSkyName, world).utf8())
		);
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
		const Field *dome_info = Config_GetField(skydome_config, FNVHashString("DomeInfo"));
		TList<const Field *> dome_models = Scope_GetFieldsT(Field_GetScope(dome_info), FNVHashString("DomeModel"));
		printdebug("Skydome has ", dome_models.size(), " dome models");
		for (size_t i = 0; i < dome_models.size(); ++ i) {
			const Field *f = *dome_models.at(i);
			String model_name = api_str_to_godot(Field_GetString, Scope_GetField(Field_GetScope(f), FNVHashString("Geometry")), 0);
			printdebug("Importing skydome model ", i, "/",  dome_models.size(), " ", model_name);
			populate_model(skydome, model_name, "", scene_dir);
		}

		// Create sky objects
		TList<const Field *> sky_objects = Config_GetFieldsT(skydome_config, FNVHashString("SkyObject"));
		printdebug("Skydome has ", sky_objects.size(), " sky objects");
		for (size_t i = 0; i < sky_objects.size(); ++ i) {
			const Field *f = *sky_objects.at(i);
			String model_name = api_str_to_godot(Field_GetString, Scope_GetField(Field_GetScope(f), FNVHashString("Geometry")), 0);
			// This alternate behavior mimicks that of the .NET Scope wrapper from LibSWBF2 definition of GetString
			if (model_name == "") {
				model_name = api_str_to_godot(Field_GetString, f, FNVHashString("Geometry"));
			}
			printdebug("Importing sky object ", i, "/",  sky_objects.size(), " ", model_name);
			populate_model(skydome, model_name, "", scene_dir);
		}

		return skydome;
	}

	MeshInstance3D *import_terrain(const World *world, const String &scene_dir) {
		const Terrain *terrain = World_GetTerrain(world);
		if (terrain == nullptr) {
			return nullptr;
		}

		String terrain_name = api_str_to_godot(World_GetTerrainName, world);

		// Create the terrain mesh
		MeshInstance3D *terrain_mesh = memnew(MeshInstance3D);
		if (terrain_mesh == nullptr) {
			UtilityFunctions::printerr("Failed to create terrain mesh");
			return nullptr;
		}
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

		TList<uint32_t> index_buffer = Terrain_GetIndexBufferT(terrain);
		TList<const LibSWBF2::Vector3> vertex_buffer = Terrain_GetVertexBufferT(terrain);
		TList<const LibSWBF2::Vector2> tex_uv_buffer = Terrain_GetUVBufferT(terrain);

		float minx = FLT_MAX;
		float minz = FLT_MAX;
		float maxx = FLT_MIN;
		float maxz = FLT_MIN;

		for (uint32_t i = 0; i < vertex_buffer.size(); ++ i) {
			const LibSWBF2::Vector3 &zzz = *vertex_buffer.at(i);
			vertex.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			minx = MIN(minx, zzz.m_X);
			minz = MIN(minz, zzz.m_Z);
			maxx = MAX(maxx, zzz.m_X);
			maxz = MAX(maxz, zzz.m_Z);
		}

		for (uint32_t i = 0; i < vertex_buffer.size(); ++ i) {
			const LibSWBF2::Vector3 &zzz = *vertex_buffer.at(i);
			blend_uv.push_back(Vector2((zzz.m_X - minx) / (maxx - minx), (zzz.m_Z - minz) / (maxz - minz)));
		}

		for (uint32_t i = 0; i < tex_uv_buffer.size(); ++ i) {
			const LibSWBF2::Vector2 &zzz = *tex_uv_buffer.at(i);
			tex_uv.push_back(Vector2(zzz.m_X, zzz.m_Y));
		}


		// Calculate normals because what comes out of LibSWBF2 is junk
		normal.resize(vertex_buffer.size());
		normal.fill(Vector3());

		// Yeah... If we don't do this we can't calculate normals
		// correctly. I know it's bad, you know it's bad, let's just move on.
		printdebug("Brute forcing terrain indices... This will take a minute");
		PackedByteArray visited;
		visited.resize(index_buffer.size());
		visited.fill(0);

		if (false) // XXX Disabled for debugging
		for (uint32_t i = 0; i < index_buffer.size(); ++ i) {
			size_t ii = *index_buffer.at(i);
			if (ii >= vertex.size()) {
				UtilityFunctions::printerr("Index ", ii, " is beyond the size of this vertex array (", vertex.size(), ")");
				continue;
			}
			const Vector3 &iii = vertex[ii];
			if (visited[i] == 0) {
				for (uint32_t j = i + 1; j < index_buffer.size(); ++ j) {
					size_t ij = *index_buffer.at(j);
					if (ij >= vertex.size()) {
						UtilityFunctions::printerr("Index ", ij, " is beyond the size of this vertex array (", vertex.size(), ")");
						continue;
					}
					const Vector3 &jjj = vertex[ij];
					if (iii.distance_squared_to(jjj) < 0.01) {
						*index_buffer.at(j) = *index_buffer.at(i);
						visited[j] = 1;
					}
				}
			}
		}

		// Note the reversed index orders
		for (uint32_t i = 0; i < index_buffer.size() - 2; i += 3) {
			int v0 = *index_buffer.at(i+2);
			int v1 = *index_buffer.at(i+1);
			int v2 = *index_buffer.at(i+0);

			int max_v = std::max(v0, std::max(v1, v2));

			if (max_v >= vertex.size()) {
				UtilityFunctions::printerr("Index ", max_v, " is beyond the size of this vertex array (", vertex.size(), ")");
				continue;
			}

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

		for (uint32_t i = 0; i < normal.size(); ++ i) {
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
		TList<uint8_t> blend_map_buffer = Terrain_GetBlendMap(terrain, &blend_map_dim, &blend_map_layers);

		for (int i = 0; i < 4; ++ i) {
			PackedByteArray packed_buffer;
			size_t size = blend_map_dim * blend_map_dim * sizeof(*blend_map_buffer.at(0)) * 4;
			packed_buffer.resize(size);
			uint8_t *ptrw = packed_buffer.ptrw();
			for (int h = 0; h < blend_map_dim; ++ h) {
			for (int w = 0; w < blend_map_dim; ++ w) {
				// Wtf is this indexing scheme???
				for (int j = 0; j < 4; ++ j) {
					if (i * 4 + j >= blend_map_layers) {
						continue;
					}
					ptrw[(blend_map_dim * h + w) * 4 + j] = *blend_map_buffer.at(blend_map_layers * (blend_map_dim * h + w) + i * 4 + j);
				}
			}}

			Ref<Image> image = Image::create_from_data(blend_map_dim, blend_map_dim, false, Image::Format::FORMAT_RGBA8, packed_buffer);
			image->save_png(scene_dir + String("/") + String("terrain_blend_map_") + itos(i) + String(".png"));
			terrain_material->set_shader_parameter("BlendMap" + itos(i), ImageTexture::create_from_image(image));
		}

		// Blend Layers
		// TODO: Does SWBF2 have terrain bump mapping?
		TList<LibSWBF2::Texture> layer_textures = Terrain_GetLayerTextures(terrain, container);
		for (size_t i = 0; i < layer_textures.size(); ++ i) {
			LibSWBF2::Texture *texture = layer_textures.at(i);
			if (texture) {
				Ref<ImageTexture> albedo_texture = import_texture(texture, scene_dir);
				terrain_material->set_shader_parameter("BlendLayer" + itos(i), albedo_texture);
			} else {
				UtilityFunctions::printerr("Failed to find terrain layer image ", api_str_to_godot(Texture_GetName, texture));
			}
		}

		array_mesh->surface_set_material(0, terrain_material);
		terrain_mesh->set_mesh(array_mesh);

		return terrain_mesh;
	}

	Ref<ImageTexture> maybe_load_texture(const String &image_name) {
		if (textures.has(image_name)) {
			return textures.get(image_name);
		}
		return Ref<ImageTexture>{};
	}

	Ref<ImageTexture> import_texture(const LibSWBF2::Texture *texture, const String &scene_dir) {
		uint16_t width = 0;
		uint16_t height = 0;

		TList<uint8_t> buffer = Texture_GetData(texture, &width, &height);

		if (width == 0 || height == 0) {
			UtilityFunctions::printerr("Failed to load SWBF2 texture");
			return {};
		}

		String texture_name = api_str_to_godot(Texture_GetName, texture);

		String resource_path = scene_dir + String("/") + String(texture_name) + String("_tex.tres");

		Ref<ImageTexture> texture2d = maybe_load_texture(texture_name);
		if (!texture2d.is_valid()) {
			printdebug("Importing texture ", texture_name);

			PackedByteArray packed_buffer;
			size_t size = width * height * sizeof(*buffer.at(0)) * 4;
			packed_buffer.resize(size);
			memcpy(packed_buffer.ptrw(), buffer.data(), size);

			Ref<Image> image = Image::create_from_data(width, height, false, Image::Format::FORMAT_RGBA8, packed_buffer);
			texture2d = ImageTexture::create_from_image(image);
			if (Error save_err = ResourceSaver::get_singleton()->save(texture2d, resource_path)) {
				UtilityFunctions::printerr("Error saving texture ", save_err);
			} else {
				// This seems unnecessary, but if we don't re-load the resource
				// it won't be referenced by anything that uses this Ref<ImageTexture>
				texture2d = ResourceLoader::get_singleton()->load(resource_path);
				textures.insert(texture_name, texture2d);
			}
		}

		return texture2d;
	}

	Ref<StandardMaterial3D> maybe_load_material(const String &albedo_texture_name) {
		if (materials.has(albedo_texture_name)) {
			return materials.get(albedo_texture_name);
		}
		return Ref<StandardMaterial3D>{};
	}

	Ref<StandardMaterial3D> import_material(const LibSWBF2::Material *material, const String &scene_dir) {
		// All material types have an albedo texture
		// TODO: We assume all materials which share an albedo texture are the same material. This is probably the case?
		const LibSWBF2::Texture *texture = Material_GetTexture(material, 0);
		if (!texture) {
			UtilityFunctions::printerr("Failed to get albedo map texture for material");
			return Ref<StandardMaterial3D>{};
		}

		String albedo_texture_name = api_str_to_godot(Texture_GetName, texture);
		String resource_path = scene_dir + String("/") + albedo_texture_name + String("_mat.tres");

		Ref<StandardMaterial3D> standard_material = maybe_load_material(albedo_texture_name);

		if (!standard_material.is_valid()) {
			standard_material.instantiate();

			Ref<ImageTexture> albedo_texture = import_texture(texture, scene_dir);
			standard_material->set_texture(BaseMaterial3D::TextureParam::TEXTURE_ALBEDO, albedo_texture);

			// TODO: Other textures? Specular?
			// I'm assuming this matches the order of textures used by XSI as documented here:
			// https://sites.google.com/site/swbf2modtoolsdocumentation/misc_documentation
			// I am not confident LibSWBF2 correctly sets the BumpMap flag, so attempt to load
			// the second image of every material as normal map image.
			EMaterialFlags material_flags = Material_GetFlags(material);
			//if ((uint32_t)material_flags & (uint32_t)EMaterialFlags::BumpMap) {
				if (const LibSWBF2::Texture *texture = Material_GetTexture(material, 1)) {
					Ref<ImageTexture> normal_texture = import_texture(texture, scene_dir);
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

			if (Error save_err = ResourceSaver::get_singleton()->save(standard_material, resource_path)) {
				UtilityFunctions::printerr("Error saving material ", save_err);
			} else {
				// This seems unnecessary, but if we don't re-load the resource
				// it won't be referenced by anything that uses this Ref<StandardMaterial>
				standard_material = ResourceLoader::get_singleton()->load(resource_path);
				materials.insert(albedo_texture_name, standard_material);
			}
		}

		return standard_material;
	}

	void segments_to_mesh(MeshInstance3D *mesh_instance, const List<const Segment *> segments, const String &override_texture, const String &scene_dir) {
		Ref<ArrayMesh> array_mesh;
		array_mesh.instantiate();

		int surface_index = 0;

		for (size_t si = 0; si < segments.size(); ++ si) {
			const Segment *segment = segments[si];

			Array mesh_data;
			mesh_data.resize(Mesh::ArrayType::ARRAY_MAX);

			PackedVector3Array vertex;
			PackedVector3Array normal;
			PackedVector2Array tex_uv;
			PackedInt32Array index;

			TList<uint16_t> index_buffer = Segment_GetIndexBufferT(segment);
			TList<const LibSWBF2::Vector3> vertex_buffer = Segment_GetVertexBufferT(segment);
			TList<const LibSWBF2::Vector2> tex_uv_buffer = Segment_GetUVBufferT(segment);
			TList<const LibSWBF2::Vector3> normal_buffer = Segment_GetNormalBufferT(segment);

			ETopology topology = Segment_GetTopology(segment);

			if (vertex_buffer.size() != normal_buffer.size() || normal_buffer.size() != tex_uv_buffer.size()) {
				UtilityFunctions::printerr("Skipping mesh with invalid vertex, normal, tex_uv count: ", vertex_buffer.size(), " ", normal_buffer.size(), " ", tex_uv_buffer.size());
				continue;
			}

			for (uint32_t i = 0; i < vertex_buffer.size(); ++ i) {
				const LibSWBF2::Vector3 &zzz = *vertex_buffer.at(i);
				vertex.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}

			for (uint32_t i = 0; i < normal_buffer.size(); ++ i) {
				const LibSWBF2::Vector3 &zzz = *normal_buffer.at(i);
				normal.push_back(Vector3(zzz.m_X, zzz.m_Y, zzz.m_Z));
			}
			for (uint32_t i = 0; i < tex_uv_buffer.size(); ++ i) {
				const LibSWBF2::Vector2 &zzz = *tex_uv_buffer.at(i);
				tex_uv.push_back(Vector2(zzz.m_X, zzz.m_Y));
			}

			if (topology == ETopology::PointList ||
			    topology == ETopology::LineList ||
			    topology == ETopology::LineStrip)
			{
				UtilityFunctions::printerr("Skipping mesh segment with unsupported topology");
				continue;
			} else if (topology == ETopology::TriangleList) {
				for (uint32_t i = 0; i < index_buffer.size(); ++ i) {
					index.push_back(*index_buffer.at(i));
				}
			} else if (topology == ETopology::TriangleStrip) {
				// Convert strip to list
				for (uint32_t i = 0; i < index_buffer.size() - 2; ++ i) {
					if (i % 2 == 1) {
						index.push_back(*index_buffer.at(i+0));
						index.push_back(*index_buffer.at(i+1));
						index.push_back(*index_buffer.at(i+2));
					} else {
						index.push_back(*index_buffer.at(i+0));
						index.push_back(*index_buffer.at(i+2));
						index.push_back(*index_buffer.at(i+1));
					}
				}
			} else if (topology == ETopology::TriangleFan) {
				if (index_buffer.size() < 3) {
					UtilityFunctions::printerr("Skipping mesh segment triangle fan with only ", index_buffer.size(), " indices");
				}
				// Convert fan to list
				uint16_t hub = *index_buffer.at(0);
				for (uint32_t i = 1; i < index_buffer.size() - 1; i += 2) {
					index.push_back(hub);
					index.push_back(*index_buffer.at(i+0));
					index.push_back(*index_buffer.at(i+1));
				}
			} else {
				UtilityFunctions::printerr("Skipping mesh segment with unknown topology ", (int32_t)topology);
			}

			mesh_data[Mesh::ArrayType::ARRAY_VERTEX] = vertex;
			mesh_data[Mesh::ArrayType::ARRAY_NORMAL] = normal;
			mesh_data[Mesh::ArrayType::ARRAY_TEX_UV] = tex_uv;
			mesh_data[Mesh::ArrayType::ARRAY_INDEX] = index;

			array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data);

			array_mesh->surface_set_material(surface_index, import_material(Segment_GetMaterial(segment), scene_dir));

			surface_index += 1;
		}

		mesh_instance->set_mesh(array_mesh);
	}

	void populate_model(Node3D *root, const String &model_name, const String &override_texture, const String &scene_dir) {
		printdebug("Populating model ", model_name);

		// Load the SWBF2 Model representation
		const Model *model = Container_FindModel(container, FNVHashString(model_name.utf8().get_data()));
		if (model == nullptr) {
			UtilityFunctions::printerr("Could not find model ", model_name);
			return;
		}

		// Convert the SWBF2 skeleton to something we can work with.
		// Since we do not import animations I've chosen to convert
		// all bones to Node3D. The original LVLImport does this with
		// static meshes but handles skinned meshes with real Unity
		// skeletons
		TList<const Bone> bones = Model_GetBonesT(model);
		if (bones.size() > 0) {
			HashMap<String, Node3D *> named_bones;
			// Create bone nodes
			for (size_t i = 0; i < bones.size(); ++ i) {
				String bone_name = api_str_to_godot(Bone_GetName, bones.at(i));
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
			for (size_t i = 0; i < bones.size(); ++ i) {
				const Bone *bone = bones.at(i);
				String bone_name = api_str_to_godot(Bone_GetName, bone);
				Node3D *bone_node = named_bones.get(bone_name);
				if (bone_node == nullptr) {
					UtilityFunctions::printerr("Failed to find bone ", bone_name);
					continue;
				}
				String parent_name = api_str_to_godot(Bone_GetParentName, bone);
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
				LibSWBF2::Vector3 pz = Bone_GetPosition(bone);
				LibSWBF2::Vector4 rz = Bone_GetRotation(bone);
				bone_node->set_position(Vector3(pz.m_X, pz.m_Y, pz.m_Z));
				bone_node->set_quaternion(Quaternion(rz.m_X, rz.m_Y, rz.m_Z, rz.m_W));
			}
		}

		// Organize SWBF2 mesh segments by bone
		TList<const Segment> model_segments = Model_GetSegmentsT(model);
		HashMap<String, List<const Segment *>> bone_segments;
		for (size_t i = 0; i < model_segments.size(); ++ i) {
			const Segment *segment = model_segments.at(i);
			String segment_bone = api_str_to_godot(Segment_GetBoneName, segment);
			if (!bone_segments.has(segment_bone)) {
				bone_segments.insert(segment_bone, List<const Segment *>());
			}
			List<const Segment *> *bsl = bone_segments.getptr(segment_bone);
			bsl->push_back(segment);
		}

		// Create mesh nodes
		for (const auto &key_pair : bone_segments) {
			const String &bone_name = key_pair.key;
			const List<const Segment *> &segments = key_pair.value;
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
		TList<const CollisionPrimitive> collision_primitives = Model_GetCollisionPrimitivesT(model);
		for (size_t i = 0; i < collision_primitives.size(); ++ i) {
			const CollisionPrimitive *collision_primitive = collision_primitives.at(i);
			String parent_name = api_str_to_godot(CollisionPrimitive_GetParentName, collision_primitive);

			StaticBody3D *static_body = memnew(StaticBody3D);
			if (static_body == nullptr) {
				UtilityFunctions::printerr("memnew failed to allocate a StaticBody3D");
				continue;
			}
			static_body->set_name(parent_name + "_collision_primitive");

			LibSWBF2::Vector3 pz = CollisionPrimitive_GetPosition(collision_primitive);
			LibSWBF2::Vector4 rz = CollisionPrimitive_GetRotation(collision_primitive);
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

			ECollisionPrimitiveType pt = CollisionPrimitive_GetType(collision_primitive);
			switch (pt) {
				case ECollisionPrimitiveType::Cube: {
					float sx = 0.0f, sy = 0.0f, sz = 0.0f;
					CollisionPrimitive_GetCubeDims(collision_primitive, &sx, &sy, &sz);
					Ref<BoxShape3D> shape;
					shape.instantiate();
					shape->set_size(Vector3(sx * 2, sy * 2, sz * 2));
					collision_shape->set_shape(shape);
					break;
				}
				case ECollisionPrimitiveType::Cylinder: {
					float sr = 0.0f, sh = 0.0f;
					CollisionPrimitive_GetCylinderDims(collision_primitive, &sr, &sh);
					Ref<CylinderShape3D> shape;
					shape.instantiate();
					shape->set_radius(sr);
					shape->set_height(sh);
					collision_shape->set_shape(shape);
					break;
				}
				case ECollisionPrimitiveType::Sphere: {
					float sr = 0.0f;
					CollisionPrimitive_GetSphereRadius(collision_primitive, &sr);
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
			const CollisionMesh *collision_mesh = Model_GetCollisionMesh(model);
			TList<uint16_t> index_buffer = CollisionMesh_GetIndexBufferT(collision_mesh);
			if (index_buffer.size() > 0) {
				TList<LibSWBF2::Vector3> vertex_buffer = CollisionMesh_GetVertexBuffer(collision_mesh);
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
				mesh_faces.resize(index_buffer.size());

				for (size_t i = 0; i < index_buffer.size(); ++ i) {
					const LibSWBF2::Vector3 &v = *vertex_buffer.at(*index_buffer.at(i));
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
		const EntityClass *entity_class = Container_FindEntityClass(container, FNVHashString(entity_class_name.utf8().get_data()));
		String base_class_name = "NONE";
		if (entity_class) {
			base_class_name = api_str_to_godot(EntityClass_GetBaseName, entity_class);
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
		TList<uint32_t> property_hashes = EntityClass_GetAllPropertyHashes(entity_class);
		String scene_path = scene_dir + String("/") + String(entity_class_name) + String(".tscn");
		String next_attach_entity_class = "";
		Node3D *root = memnew(Node3D);
		if (root == nullptr) {
			UtilityFunctions::printerr("memnew failed to allocate a Node3D");
			return nullptr;
		}
		root->set_name(entity_class_name); // Attachments seem to have no name, so we need a default
		for (size_t pi = 0; pi < property_hashes.size(); ++ pi) {
			uint32_t property_hash = *property_hashes.at(pi);
			String property_value = api_str_to_godot(EntityClass_GetPropertyValue, entity_class, property_hash);
			switch (property_hash) {
				case 1204317002: { // GeometryName
					printdebug("Attaching model ", property_value, " to ", entity_class_name);
					// Determine our texture, which is a separate property
					String override_texture = "";
					for (size_t i = 0; i < property_hashes.size(); ++ i) {
						if (*property_hashes.at(i) == 165377196) {
							override_texture = api_str_to_godot(EntityClass_GetPropertyValue, entity_class, property_hash);
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
		container = Container_Create();
	}

	~WorldImporter() {
		printdebug("Destroying WorldImporter");
		Container_Destroy(container);
	}

	void import_lvl(const String &lvl_filename, const String &scene_dir) {
		printdebug("Importing ", lvl_filename);
		// Load and verify our lvl contains one or more worlds, then import them
		Level_Owned *level = Container_AddLevel(container, lvl_filename.utf8().get_data());
		if (level == nullptr) {
			UtilityFunctions::printerr("Failed to load level");
			return;
		}
		printdebug("Importing level ", api_str_to_godot(Level_GetName, level));
		if (!Level_IsWorldLevel(level)) {
			UtilityFunctions::printerr("Canceling import because ", lvl_filename, " is not a world level");
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
		TList<const World> worlds = Level_GetWorlds(level);
		for (size_t i = 0; i < worlds.size(); ++ i) {
			const World *world = worlds.at(i);
			Node *world_node = import_world(world, scene_dir);
			if (world_node) {
				make_parent_and_owner(lvl_root, world_node);
				printdebug("Adding world to lvl scene");
			} else {
				UtilityFunctions::printerr("World ", api_str_to_godot(World_GetName, world), " failed to import");
			}
		}
		if (save_as_scene(lvl_root, scene_dir + String("/") + lvl_root->get_name() + String(".tscn")) == Error::OK) {
			printdebug("Import successful");
		}
		memdelete(lvl_root);
		Level_Destroy(level);
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
