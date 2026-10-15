class_name CharacterVisual
extends Node3D
## Visible meshes of the retargeted sandbox characters (CBP_SandboxCharacter_* children of CBP_SandboxCharacter).
## The hidden UEFN mannequin (Mesh / CharacterMesh0) keeps running ABP_SandboxCharacter (GaspAnimator); each visible mesh runs
## ABP_GenericRetarget (GaspRetargeter with the IK Retargeter named by the component tag), or follows another mesh by bone name
## (CopyPoseFromMesh with bUseAttachedParent / SetLeaderPoseComponent).

const RETARGET_DIR := "res://data/retarget/"

## Character name (EUW_CharacterSelectButton) -> components. "rtg": retargeted from the UEFN mannequin;
## "follow": copies the pose of the named component by bone name. "pp": post-process ModifyBone component space scales.
const CHARACTERS := {
	"Manny": [{"name": "Manny", "mesh": "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple", "rtg": "RTG_UEFN_to_UE5_Mannequin"}],
	"Quinn": [{"name": "SkeletalMesh", "mesh": "/Game/Characters/UE5_Mannequins/Meshes/SKM_Quinn_Simple", "rtg": "RTG_UEFN_to_UE5_Mannequin"}],
	"UE4_Mannequin": [{"name": "SkeletalMesh", "mesh": "/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin", "rtg": "RTG_UEFN_to_UE4_Mannequin"}],
	"Echo": [{"name": "EchoBody", "mesh": "/Game/Characters/Echo/Meshes/SKM_Echo", "rtg": "RTG_UEFN_to_Echo"},
		{"name": "EchoHair", "mesh": "/Game/Characters/Echo/Meshes/SKM_Echo_Hair", "follow": "EchoBody"}],
	"TwinBlast": [{"name": "TwinBlast", "mesh": "/Game/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero", "rtg": "RTG_UEFN_to_TwinBlast",
		"pp": {"weapon_l": Vector3.ZERO, "weapon_r": Vector3.ZERO, "grenade": Vector3.ZERO}}],
	# meshes attached to the (visible) UEFN Mesh component, offsets in its local space (UE cm)
	"RetargetShowcase_Mannequins": [{"name": "UE4_Mannequin", "mesh": "/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin", "rtg": "RTG_UEFN_to_UE4_Mannequin", "offset": Vector3(-100, 0, 0)},
		{"name": "Manny", "mesh": "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple", "rtg": "RTG_UEFN_to_UE5_Mannequin", "offset": Vector3(100, 0, 0)}],
}
## Characters whose UEFN Mesh component stays visible
const KEEP_SOURCE_VISIBLE := ["RetargetShowcase_Mannequins"]

static var _meshes_json := {}
static var _rtg_cache := {}

var character := ""
var parts := []  # [{name, root, skeleton, retargeter, follow, map}]

static func meshes_json() -> Dictionary:
	if _meshes_json.is_empty():
		_meshes_json = JSON.parse_string(FileAccess.get_file_as_string("res://data/characters/meshes.json"))
	return _meshes_json

static func is_retargeted(name: String) -> bool:
	return CHARACTERS.has(name)

static func asset_res(ue_path: String) -> String:
	return "res://assets/" + ue_path.trim_prefix("/Game/").get_slice(".", 0) + ".fbx"

static func load_rtg(rtg: String) -> Dictionary:
	if not _rtg_cache.has(rtg):
		_rtg_cache[rtg] = JSON.parse_string(FileAccess.get_file_as_string(RETARGET_DIR + rtg + ".json"))
	return _rtg_cache[rtg]

func setup(name: String) -> void:
	character = name
	for c in CHARACTERS[name]:
		_add_part(c)

func _add_part(c: Dictionary) -> void:
	var scene: PackedScene = load(asset_res(c["mesh"]))
	var root: Node3D = scene.instantiate()
	add_child(root)
	if c.has("offset"):
		var o: Vector3 = c["offset"]
		root.position = Vector3(o.x, o.z, o.y) * 0.01
	var sk: Skeleton3D = root.find_children("*", "Skeleton3D", true, false)[0]
	CharacterMaterials.apply(root, c["mesh"], c.get("materials", []))
	var part := {"name": c["name"], "root": root, "skeleton": sk, "retargeter": null, "follow": c.get("follow", ""), "map": PackedInt32Array(),
		"pp": _make_post_process(c["mesh"], sk), "curve_targets": [], "pp_frames": 0}
	if part["pp"] != null:
		part["curve_targets"] = _curve_targets(part["pp"], root)
	if c.has("rtg"):
		var rt := GaspRetargeter.new()
		if not rt.setup(load_rtg(c["rtg"]), sk):
			push_error("GaspRetargeter setup failed: %s" % c["rtg"])
		if c.has("pp"):
			rt.set_post_process_scales(c["pp"])
		part["retargeter"] = rt
	parts.append(part)

## Skeletal mesh PostProcessAnimBlueprint (meshes.json post_process_abp) ported by tools/build/build_post_process.py
static func _make_post_process(mesh: String, sk: Skeleton3D) -> GaspPostProcess:
	var info: Dictionary = meshes_json().get("meshes", {}).get(mesh.get_slice(".", 0), {})
	var abp = info.get("post_process_abp")
	if abp == null:
		return null
	var path := "res://data/post_process/%s.json" % String(abp).get_file().get_slice(".", 0)
	if not FileAccess.file_exists(path):
		return null
	var pp := GaspPostProcess.new()
	if not pp.setup(JSON.parse_string(FileAccess.get_file_as_string(path)), sk):
		push_error("GaspPostProcess setup failed: " + path)
		return null
	return pp

## curve index -> [[MeshInstance3D, blend shape index], ...] (curves drive morph targets of the same name)
static func _curve_targets(pp: GaspPostProcess, root: Node) -> Array:
	var names := pp.get_curve_names()
	var out := []
	out.resize(names.size())
	var meshes := root.find_children("*", "MeshInstance3D", true, false)
	for i in names.size():
		var l := []
		for m in meshes:
			var bi: int = (m as MeshInstance3D).find_blend_shape_by_name(names[i])
			if bi >= 0:
				l.append([m, bi])
		out[i] = l
	return out

func _post_process(p: Dictionary, dt: float, comp: Array) -> void:
	var pp: GaspPostProcess = p["pp"]
	if pp == null:
		return
	var loc: Vector3 = comp[1] if comp.size() > 1 else Vector3.ZERO
	var yaw: float = UE.quat_yaw(comp[0]) if comp.size() > 1 else 0.0
	# the first frames after spawning still carry the reference / previous pose: keep the physics reset (ResetDynamics)
	if p["pp_frames"] < 3:
		pp.reset()
	p["pp_frames"] += 1
	pp.update(p["skeleton"], dt, loc, yaw)
	var vals := pp.get_curve_values()
	var targets: Array = p["curve_targets"]
	for i in vals.size():
		for t in targets[i]:
			(t[0] as MeshInstance3D).set_blend_shape_value(t[1], vals[i])

func _find_part(n: String) -> Dictionary:
	for p in parts:
		if p["name"] == n:
			return p
	return {}

## comp: mesh component [rotation, location] in UE space (RigidBody nodes read the component motion)
func update_pose(anim: GaspAnimator, dt: float = 1.0 / 60.0, comp: Array = []) -> void:
	for p in parts:
		if p["retargeter"] != null:
			p["retargeter"].update(anim, p["skeleton"])
			_post_process(p, dt, comp)
	for p in parts:
		if p["follow"] != "":
			_copy_pose(_find_part(p["follow"]), p)
			_post_process(p, dt, comp)

## FAnimNode_CopyPoseFromMesh / leader pose: bones matched by name, unmatched bones keep their reference pose
func _copy_pose(src: Dictionary, dst: Dictionary) -> void:
	if src.is_empty():
		return
	var ss: Skeleton3D = src["skeleton"]
	var ds: Skeleton3D = dst["skeleton"]
	var map: PackedInt32Array = dst["map"]
	if map.size() != ds.get_bone_count():
		map.resize(ds.get_bone_count())
		for b in ds.get_bone_count():
			map[b] = ss.find_bone(ds.get_bone_name(b))
		dst["map"] = map
	for b in map.size():
		var s := map[b]
		if s < 0:
			# post-process nodes write these bones every frame: start again from the reference pose
			var r := ds.get_bone_rest(b)
			ds.set_bone_pose_position(b, r.origin)
			ds.set_bone_pose_rotation(b, r.basis.get_rotation_quaternion())
			ds.set_bone_pose_scale(b, r.basis.get_scale())
			continue
		ds.set_bone_pose_position(b, ss.get_bone_pose_position(s))
		ds.set_bone_pose_rotation(b, ss.get_bone_pose_rotation(s))
		ds.set_bone_pose_scale(b, ss.get_bone_pose_scale(s))

func primary_skeleton() -> Skeleton3D:
	return parts[0]["skeleton"] if not parts.is_empty() else null
