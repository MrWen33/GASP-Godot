extends SceneTree
## Echo post-process (ABP_Echo_PostProcess + ABP_Echo_Hair): pose driver morph curves, twist bones, constraints, helper
## control rig, rigid bodies (canister, skirt, shoulder pads, pouch, ponytail). Runs, stops, turns and jumps, logs the
## ponytail tip / skirt / canister offsets and the largest morph weights, screenshots key moments.
## Env PP_CHAR selects another character (e.g. TwinBlast).

var frame := 0
var main: Node
var shots := {90: "pp_idle", 170: "pp_run", 215: "pp_stop", 260: "pp_turn", 330: "pp_jump"}
var char_name := "Echo"
var cam: Camera3D
var idle_trace := OS.get_environment("PP_TRACE") != ""

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	if OS.get_environment("PP_CHAR") != "":
		char_name = OS.get_environment("PP_CHAR")

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _local(sk: Skeleton3D, bone: String, rel: String) -> Vector3:
	var b := sk.find_bone(bone)
	var r := sk.find_bone(rel)
	if b < 0 or r < 0:
		return Vector3.INF
	var x := sk.get_bone_global_pose(r).affine_inverse() * sk.get_bone_global_pose(b)
	return x.origin * 100.0

func _process(_dt: float) -> bool:
	frame += 1
	var ch = main.get_node("Character")
	var idle_only := OS.get_environment("PP_IDLE") != ""
	if idle_only and frame > 6:
		pass
	else: match frame:
		5: main.spawn_character(char_name)
		6:
			if OS.get_environment("PP_FLAGS") != "":
				for p in ch.visual.parts:
					if p["pp"] != null:
						p["pp"].set_physics_debug_flags(int(OS.get_environment("PP_FLAGS")))
		40: ch.control_yaw = ch.actor_yaw + 160.0
		100: _key(KEY_W, true)
		200: _key(KEY_W, false)
		230: ch.control_yaw = ch.actor_yaw + 90.0
		280: _key(KEY_W, true)
		300: _key(KEY_SPACE, true)
		304: _key(KEY_SPACE, false)
		360: _key(KEY_W, false)
	if ch.visual and frame > 6:
		# close camera behind / beside the head (PP_CAM=side|back|front)
		if cam == null:
			cam = Camera3D.new()
			root.add_child(cam)
			cam.fov = 40
			cam.make_current()
		var sk0: Skeleton3D = ch.visual.primary_skeleton()
		var tb := OS.get_environment("PP_CAMBONE") if OS.get_environment("PP_CAMBONE") != "" else "spine_04"
		var dist := float(OS.get_environment("PP_DIST")) if OS.get_environment("PP_DIST") != "" else 1.6
		var head: Vector3 = sk0.global_transform * sk0.get_bone_global_pose(sk0.find_bone(tb)).origin
		var fwd := Vector3(sin(-deg_to_rad(ch.actor_yaw - 90.0)), 0, -cos(-deg_to_rad(ch.actor_yaw - 90.0)))
		var mode := OS.get_environment("PP_CAM")
		var off := -fwd * dist + Vector3.UP * 0.25
		if mode == "side":
			off = fwd.cross(Vector3.UP) * dist + Vector3.UP * 0.2
		elif mode == "front":
			off = fwd * dist + Vector3.UP * 0.2
		cam.global_position = head + off
		cam.look_at(head - Vector3.UP * 0.1)
	if idle_only and frame == 5:
		main.spawn_character(char_name)
	if idle_trace and frame > 10 and frame < (380 if idle_only else 100) and ch.visual:
		for p in ch.visual.parts:
			if p["name"] == "EchoHair":
				var sk: Skeleton3D = p["skeleton"]
				print("t%d tip=%s j1=%s" % [frame, str(_local(sk, "C_Ponytail_B_Joint7_Jnt", "head")), str(_local(sk, "C_Ponytail_B_Joint1_Jnt", "head"))])
				var hb := sk.find_bone("head")
				var rb := sk.find_bone("C_Ponytail_B_Root_Jnt")
				if false: print("   head gscale=%s root gpos=%s root grot=%s rootbone=%s" % [str(sk.get_bone_global_pose(hb).basis.get_scale()), str(sk.get_bone_global_pose(rb).origin * 100.0), str(sk.get_bone_global_pose(rb).basis.get_rotation_quaternion()), str(sk.get_bone_global_pose(0).origin * 100.0)])
				if frame % 30 == 0:
					print("   root rel head now=%s ref=%s" % [str(_local(sk, "C_Ponytail_B_Root_Jnt", "head")), str((sk.get_bone_global_rest(sk.find_bone("head")).affine_inverse() * sk.get_bone_global_rest(sk.find_bone("C_Ponytail_B_Root_Jnt"))).origin * 100.0)])
					for j in p["pp"].get_debug_joints():
						print("   %s swing=%s twist=%.1f err=%.1f gap=%.3f w=%.2f" % [sk.get_bone_name(j["child"]), str(j["swing_deg"]), j["twist_deg"], j["drive_err_deg"], j["gap"], j["w"]])
	if frame % 10 == 0 and frame > 10 and ch.visual:
		var s := "f%d" % frame
		for p in ch.visual.parts:
			var sk: Skeleton3D = p["skeleton"]
			var pp = p["pp"]
			if pp == null:
				continue
			s += " | %s" % p["name"]
			var pairs := [["C_Ponytail_B_Joint7_Jnt", "head"], ["R_Canister_A_Joint1_Jnt", "pelvis"], ["C_Skirt_A_FrontRoot_Jnt", "pelvis"], ["L_ShoulderPad_A_Joint1_Jnt", "clavicle_l"], ["upperarm_twist_01_l", "upperarm_l"]]
			if OS.get_environment("PP_PAIRS") != "":
				pairs = []
				for e in OS.get_environment("PP_PAIRS").split(","):
					pairs.append(e.split(":"))
			for pair in pairs:
				var v := _local(sk, pair[0], pair[1])
				if v != Vector3.INF:
					var b0 := sk.find_bone(pair[0])
					var r0 := sk.find_bone(pair[1])
					var rv := (sk.get_bone_global_rest(r0).affine_inverse() * sk.get_bone_global_rest(b0)).origin * 100.0
					s += " %s=(%.1f,%.1f,%.1f)d%.1f" % [pair[0].get_slice("_", 1) + pair[0].get_slice("_", 2), v.x, v.y, v.z, (v - rv).length()]
			var names: PackedStringArray = pp.get_curve_names()
			var vals: PackedFloat32Array = pp.get_curve_values()
			var best := []
			for i in vals.size():
				if vals[i] > 0.05:
					best.append("%s=%.2f" % [names[i].trim_prefix("L_Biped").trim_prefix("R_Biped").trim_prefix("C_Biped"), vals[i]])
			if not best.is_empty():
				s += " curves[" + ", ".join(best.slice(0, 6)) + "]"
		print(s)
	if shots.has(frame) and DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://%s_%s.png" % [shots[frame], char_name])
	return frame >= 380
