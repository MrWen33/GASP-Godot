class_name CharacterMaterials
extends RefCounted
## Builds Godot materials for the retargeted character meshes from the UE material data exported to data/characters/meshes.json
## (texture / vector parameters and the textures each material samples). The UE master materials are approximated by
## shaders/ue_character.gdshader; translucent overlays (tear lines, eye occlusion, lacrimal fluid) and M_hide are hidden.

const SHADER := preload("res://shaders/ue_character.gdshader")
const ALBEDO_KEYS := ["Albedo", "Base Texture", "BaseColor", "Color", "Diffuse", "Iris_BaseColor"]
const NORMAL_KEYS := ["Normal", "NormalMap"]
const HIDDEN_BASES := ["M_hide", "M_TearLine", "M_Eye_Occlusion", "M_EyeOcclusion", "M_lacrimal_fluid", "M_Lacrimal", "M_Cloth_Scarf", "M_Cloth_Skirt"]

static var _cache := {}

static func _tex_res(ue_path: String) -> String:
	var base := "res://assets/" + ue_path.trim_prefix("/Game/").get_slice(".", 0)
	for ext in [".png", ".exr"]:
		if ResourceLoader.exists(base + ext):
			return base + ext
	return ""

static func _name(ue_path: String) -> String:
	return ue_path.get_slice(".", 1) if ue_path.contains(".") else ue_path.get_file()

static func _suffix_is(n: String, suffixes: Array) -> bool:
	var l := n.to_lower()
	for s in suffixes:
		if l.ends_with(s):
			return true
	return false

## Picks a texture: explicit parameter names first, then the sampled textures by naming convention, preferring ones that
## belong to the character (token) over shared detail textures.
static func _pick(mat: Dictionary, keys: Array, suffixes: Array, token: String, exclude: Array = []) -> String:
	var tex: Dictionary = mat.get("tex", {})
	for k in keys:
		if tex.has(k) and _tex_res(tex[k]) != "" and not _excluded(_name(tex[k]), exclude):
			return tex[k]
	var best := ""
	var best_score := -1
	for u in mat.get("used", []):
		var n := _name(u)
		if not _suffix_is(n, suffixes) or _excluded(n, exclude) or _tex_res(u) == "":
			continue
		var score := 1 + (2 if token != "" and n.to_lower().contains(token) else 0)
		if score > best_score:
			best_score = score
			best = u
	return best

static func _excluded(n: String, exclude: Array) -> bool:
	var l := n.to_lower()
	for e in exclude:
		if l.contains(e):
			return true
	return false

static func _vec_color(mat: Dictionary) -> Color:
	var v: Dictionary = mat.get("vec", {})
	for k in ["Tint", "BodyColor", "BaseColor_ColorCorrect", "DyeColor", "TipColor"]:
		if v.has(k):
			var a: Array = v[k]
			return Color(a[0], a[1], a[2], 1.0)
	return Color.WHITE

static func build(mat: Dictionary, token: String) -> Material:
	var key := String(mat.get("path", "")) + "|" + token
	if _cache.has(key):
		return _cache[key]
	var base := _name(String(mat.get("base", "")))
	var blend := String(mat.get("blend", ""))
	var m: Material
	if base in HIDDEN_BASES or blend.contains("TRANSLUCENT") or blend.contains("ADDITIVE"):
		var hide := StandardMaterial3D.new()
		hide.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		hide.albedo_color = Color(0, 0, 0, 0)
		hide.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m = hide
	else:
		var s := ShaderMaterial.new()
		s.shader = SHADER
		var detail := ["default", "flat", "blank", "tiling", "noise", "flakes", "bump", "micro", "pore", "fabric", "_ml_", "honeycomb", "nylon", "rubber", "leather", "metal", "wood", "wool", "aluminum", "highpass", "skin_0"]
		var albedo := _pick(mat, ALBEDO_KEYS, ["_d", "_basecolor", "_albedo", "_color", "_bc"], token, detail + ["sclera", "veins", "decal"])
		var normal := _pick(mat, NORMAL_KEYS, ["_n", "_normal", "_normals", "__normals"], token, detail + ["wm1", "wm2", "wm3", "_bn"])
		var tex: Dictionary = mat.get("tex", {})
		var tint := _vec_color(mat)
		if albedo != "":
			s.set_shader_parameter("use_albedo", true)
			s.set_shader_parameter("albedo_tex", load(_tex_res(albedo)))
		elif not mat.get("vec", {}).has("Tint") and not mat.get("vec", {}).has("BodyColor"):
			# Paragon layered materials (RGB masks blend tiling layers): neutral base
			tint = Color(0.32, 0.32, 0.33)
		s.set_shader_parameter("tint", tint)
		if normal != "":
			s.set_shader_parameter("use_normal", true)
			s.set_shader_parameter("normal_tex", load(_tex_res(normal)))
		# packed masks: Echo ORM (R occlusion, G roughness, B metallic), Mannequin MSR (R metallic, G specular, B roughness),
		# UE4 mannequin occlusion map
		var packed := ""
		if tex.has("ORM") and _tex_res(tex["ORM"]) != "":
			packed = tex["ORM"]
			s.set_shader_parameter("occlusion_channel", Vector4(1, 0, 0, 0))
			s.set_shader_parameter("roughness_channel", Vector4(0, 1, 0, 0))
			s.set_shader_parameter("metallic_channel", Vector4(0, 0, 1, 0))
		elif tex.has("MSR_tex") and _tex_res(tex["MSR_tex"]) != "":
			packed = tex["MSR_tex"]
			s.set_shader_parameter("metallic_channel", Vector4(1, 0, 0, 0))
			s.set_shader_parameter("roughness_channel", Vector4(0, 0, 1, 0))
		elif tex.has("AOMap") and _tex_res(tex["AOMap"]) != "":
			packed = tex["AOMap"]
			s.set_shader_parameter("occlusion_channel", Vector4(1, 0, 0, 0))
		else:
			var orm := _pick(mat, [], ["_orm"], token)
			if orm != "":
				packed = orm
				s.set_shader_parameter("occlusion_channel", Vector4(1, 0, 0, 0))
				s.set_shader_parameter("roughness_channel", Vector4(0, 1, 0, 0))
				s.set_shader_parameter("metallic_channel", Vector4(0, 0, 1, 0))
		if packed != "":
			s.set_shader_parameter("use_packed", true)
			s.set_shader_parameter("packed_tex", load(_tex_res(packed)))
		# opacity masks of hair cards / eyelashes (masked blend mode)
		if blend.contains("MASKED"):
			var mask := ""
			for k in ["Alpha", "Opacity", "OpacityMask", "Eyelash Texture", "Texture"]:
				if tex.has(k) and _tex_res(tex[k]) != "":
					mask = tex[k]
					break
			if mask == "":
				mask = _pick(mat, [], ["_alpha", "_a", "_m", "_mask_a"], token, ["matid", "_msk"])
			if mask != "" and (base.contains("Hair") or base.contains("Lash") or base.contains("Eyelash")):
				s.set_shader_parameter("use_mask", true)
				s.set_shader_parameter("mask_tex", load(_tex_res(mask)))
				if base.contains("Hair_Cards"):
					s.set_shader_parameter("use_albedo", false)
					var v: Dictionary = mat.get("vec", {})
					if v.has("ColorStraitions"):
						var a: Array = v["ColorStraitions"]
						s.set_shader_parameter("tint", Color(a[0], a[1], a[2]))
		if bool(mat.get("two_sided", false)):
			s.render_priority = 0
			# two-sided materials (hair cards, clothes)
			var sh := SHADER.code.replace("cull_back", "cull_disabled")
			var two := Shader.new()
			two.code = sh
			s.shader = two
		m = s
	m.resource_name = _name(String(mat.get("path", "")))
	_cache[key] = m
	return m

## Assigns materials to every surface of the meshes under root. Surfaces are matched to the UE material slots by the imported
## material name (FBX keeps the UE material names, duplicates get a numeric suffix) and fall back to slot order.
static func apply(root: Node, ue_mesh: String, override_materials: Array = []) -> void:
	var data: Dictionary = CharacterVisual.meshes_json()
	var mesh_info: Dictionary = data.get("meshes", {}).get(ue_mesh, {})
	var slots: Array = mesh_info.get("materials", [])
	var overrides: Dictionary = data.get("override_materials", {})
	var token := ue_mesh.get_file().to_lower()
	for t in ["twinblast", "echo", "manny", "quinn", "ue4_mannequin"]:
		if token.contains(t):
			token = t
			break
	var mats := []
	for i in slots.size():
		var md = slots[i].get("mat")
		if i < override_materials.size() and override_materials[i] != "" and overrides.has(override_materials[i]):
			md = overrides[override_materials[i]]
		mats.append(md)
	# LOD 0 section -> material slot (LODInfo.LODMaterialMap; the FBX export names sections by their own material index)
	var section_slots: Array = mesh_info.get("section_slots", [])
	for mi in root.find_children("*", "MeshInstance3D", true, false):
		var mesh: Mesh = mi.mesh
		var by_section := section_slots.size() == mesh.get_surface_count()
		for s in mesh.get_surface_count():
			var imported: Material = mesh.surface_get_material(s)
			var iname := imported.resource_name if imported else ""
			var md = null
			if by_section and int(section_slots[s]) < mats.size():
				md = mats[int(section_slots[s])]
				if md != null and (md is Dictionary):
					mi.set_surface_override_material(s, build(md, token))
				continue
			for j in slots.size():
				if mats[j] == null:
					continue
				var mname := _name(String(mats[j].get("path", "")))
				if iname == mname or iname.trim_suffix("_" + iname.get_slice("_", iname.get_slice_count("_") - 1)) == mname:
					md = mats[j]
					break
			if md == null and s < mats.size():
				md = mats[s]
			if md != null and (md is Dictionary):
				mi.set_surface_override_material(s, build(md, token))

## MI parented to M_UEFN_Mannequin_Masked (e.g. MI_UEFN_Mannequin_Blue): region colours decoded from the colour-ID masks
static func uefn_masked(mi_name: String) -> ShaderMaterial:
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/uefn_masked.gdshader")
	var tex_dir := "res://assets/Characters/UEFN_Mannequin/Textures/"
	var vectors: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/characters/uefn_materials.json")).get("mi_vectors", {})
	var regions: Array = JSON.parse_string(FileAccess.get_file_as_string("res://data/characters/uefn_masked_regions.json"))
	var masks := {"T_UEFN_Mannequin_Mask_LArm": "larm", "T_UEFN_Mannequin_Mask_LLeg": "lleg", "T_UEFN_Mannequin_Mask_RArm": "rarm",
		"T_UEFN_Mannequin_Mask_RLeg": "rleg", "T_UEFN_Mannequin_Mask_Torso": "torso"}
	var cols := {}
	for m in masks.values():
		var arr: Array[Vector3] = []
		arr.resize(7)
		arr.fill(Vector3.ZERO)
		cols[m] = arr
	for r in regions:
		var v: Array = vectors.get(r["param"], [0, 0, 0, 1])
		cols[masks[r["mask"]]][["R", "G", "B", "C", "M", "Y", "W"].find(r["channel"])] = Vector3(v[0], v[1], v[2])
	for t in masks:
		mat.set_shader_parameter("mask_" + masks[t], load(tex_dir + t + ".tga"))
		mat.set_shader_parameter(masks[t], PackedVector3Array(cols[masks[t]]))
	mat.set_shader_parameter("normal_tex", load(tex_dir + "T_UEFN_Mannequin_N.tga"))
	return mat
