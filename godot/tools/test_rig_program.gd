extends SceneTree
## Compares the compiled Control Rig program (rigvm_compile.py) of one post-process control_rig node with its hand ported
## op list (rigs.py) on random poses. Args: -- <mesh fbx> <post process json> <rig name>
var sk_a: Skeleton3D
var sk_b: Skeleton3D
var pp_a := GaspPostProcess.new()
var pp_b := GaspPostProcess.new()
var f := 0
var worst := 0.0

func _init() -> void:
	var a := OS.get_cmdline_user_args()
	var sa: Node = (load(a[0]) as PackedScene).instantiate()
	var sb: Node = (load(a[0]) as PackedScene).instantiate()
	root.add_child(sa)
	root.add_child(sb)
	sk_a = sa.find_children("*", "Skeleton3D", true, false)[0]
	sk_b = sb.find_children("*", "Skeleton3D", true, false)[0]
	var prog: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(a[1]))
	var node_a: Dictionary
	for n in prog["nodes"]:
		if n["type"] == "control_rig" and n["rig"] == a[2]:
			node_a = n
	var node_b := node_a.duplicate()
	node_b.erase("program")
	pp_a.setup({"nodes": [node_a]}, sk_a)
	pp_b.setup({"nodes": [node_b]}, sk_b)

func _process(_dt: float) -> bool:
	seed(f)
	for sk in [sk_a, sk_b]:
		sk.reset_bone_poses()
	for b in sk_a.get_bone_count():
		if randf() < 0.5:
			var q := Quaternion(Vector3(randf() - 0.5, randf() - 0.5, randf() - 0.5).normalized(), randf() * 1.2)
			var r := sk_a.get_bone_rest(b).basis.get_rotation_quaternion() * q
			sk_a.set_bone_pose_rotation(b, r)
			sk_b.set_bone_pose_rotation(b, r)
	pp_a.update(sk_a, 1.0 / 60.0, Vector3.ZERO, 0.0)
	pp_b.update(sk_b, 1.0 / 60.0, Vector3.ZERO, 0.0)
	for b in sk_a.get_bone_count():
		var d := sk_a.get_bone_global_pose(b).origin.distance_to(sk_b.get_bone_global_pose(b).origin) * 100.0
		var qa := sk_a.get_bone_global_pose(b).basis.get_rotation_quaternion()
		var qb := sk_b.get_bone_global_pose(b).basis.get_rotation_quaternion()
		var ang := rad_to_deg(qa.angle_to(qb))
		if d > 0.01 or ang > 0.05:
			if f < 3:
				print("f%d %s dpos=%.3f cm dang=%.3f deg" % [f, sk_a.get_bone_name(b), d, ang])
			worst = maxf(worst, ang)
	f += 1
	if f >= 20:
		print("worst angle diff: %.4f deg" % worst)
		return true
	return false
