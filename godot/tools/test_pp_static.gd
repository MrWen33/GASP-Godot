extends SceneTree
## GaspPostProcess rigid body on a frozen reference pose (no animation): the chain should settle under gravity.
## Args: -- <mesh fbx res path> <post process json> <tip bone> <rel bone> [flags] [move: 1 = translate the component]
var sk: Skeleton3D
var pp := GaspPostProcess.new()
var tip := -1
var rel := -1
var f := 0
var move := false

func _init() -> void:
	var a := OS.get_cmdline_user_args()
	var scene: Node = (load(a[0]) as PackedScene).instantiate()
	root.add_child(scene)
	sk = scene.find_children("*", "Skeleton3D", true, false)[0]
	pp.setup(JSON.parse_string(FileAccess.get_file_as_string(a[1])), sk)
	if a.size() > 4:
		pp.set_physics_debug_flags(int(a[4]))
	move = a.size() > 5 and a[5] == "1"
	tip = sk.find_bone(a[2])
	rel = sk.find_bone(a[3])

func _process(_dt: float) -> bool:
	sk.reset_bone_poses()
	if OS.get_environment("PP_WIGGLE") != "":
		var hb := sk.find_bone("head")
		sk.set_bone_pose_rotation(hb, sk.get_bone_rest(hb).basis.get_rotation_quaternion() * Quaternion(Vector3(0, 0, 1), deg_to_rad(10.0) * sin(f / 60.0 * TAU)))
	var loc := Vector3.ZERO
	if move and f > 60:
		loc = Vector3(minf(f - 60, 60) * 5.0 + maxf(f - 120, 0) * 0.0, 0, 0)  # 300 cm/s for 1 s, then stop
		if f > 120:
			loc.x = 300.0
	pp.update(sk, 1.0 / 60.0, loc, 0.0)
	if f % 6 == 0:
		var x := sk.get_bone_global_pose(rel).affine_inverse() * sk.get_bone_global_pose(tip)
		print("f%d tip=%s" % [f, str(x.origin * 100.0)])
		if f % 60 == 0:
			for j in pp.get_debug_joints():
				print("   %s<-%s swing=%s twist=%.1f drive_err=%.1f gap=%.3f w=%.2f" % [sk.get_bone_name(j["child"]), sk.get_bone_name(j["parent"]) if j["parent"] >= 0 else "-", str(j["swing_deg"]), j["twist_deg"], j["drive_err_deg"], j["gap"], j["w"]])
	f += 1
	return f >= 240
