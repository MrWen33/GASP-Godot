extends SceneTree
## Traversal test: for several traversable blocks, teleport the character in front of a ledge, run at it,
## press jump and report the chosen action/montage and where the character ends up.

var frame := 0
var main: Node
var ch
var cases := []
var case_idx := -1
var case_frame := 0
var pressed := false
var press_frame := 0
var shots := false
var screen_log: FileAccess
var jerk_hist := []  # last two frames of skeleton-space bone positions (TRAV_JERK)
var jerk_stats := []
var grab_logged := false

func _init() -> void:
	var ps: PackedScene = load("res://scenes/main.tscn")
	main = ps.instantiate()
	root.add_child(main)
	ch = main.get_node("Character")

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _setup_cases() -> void:
	var level = main.get_node("Level")
	var all := []
	for tb in level.traversable_blocks:
		var ledges: Array = tb["ledges"]
		if ledges.size() < 4:
			continue
		for li in 4:
			var l: Dictionary = ledges[li]
			var o: Dictionary = ledges[li ^ 1]
			var length: float = (l["b"] - l["a"]).length()
			if length < 80.0:
				continue
			var mid: Vector3 = (l["a"] + l["b"]) * 0.5
			var n: Vector3 = l["normal"]
			var ground := Vector3(mid.x, mid.y, 0.0) + n * 60.0
			var start := Vector3(mid.x, mid.y, 0.0) + n * 450.0
			# ground below both ends, clear path at capsule height
			var gs: Dictionary = ch._trace_ue(start + Vector3(0, 0, 500), start - Vector3(0, 0, 500))
			var ge: Dictionary = ch._trace_ue(ground + Vector3(0, 0, 500), ground - Vector3(0, 0, 500))
			if not gs["hit"] or not ge["hit"] or absf(gs["point"].z - ge["point"].z) > 5.0 or ge["point"].z > mid.z - 40.0:
				continue
			var gz: float = gs["point"].z
			var path: Dictionary = ch._capsule_trace(start + Vector3(0, 0, gz + 92.0), ground + Vector3(0, 0, gz + 92.0), 30.0, 86.0, 1)
			if path["hit"]:
				continue
			var depth: float = (UE.closest_on_segment(o["a"], o["b"], mid)[0] - mid).length()
			all.append({"h": mid.z - gz, "mid": mid, "n": n, "depth": depth, "gz": gz})
	all.sort_custom(func(a, b): return a["h"] < b["h"])
	var seen := {}
	for c in all:
		var key := "%d_%d" % [int(c["h"] / 20.0), int(c["depth"] / 60.0)]
		if not seen.has(key):
			seen[key] = true
			cases.append(c)

func _start_case(i: int) -> void:
	var c: Dictionary = cases[i]
	# TRAV_ANGLE: approach this many degrees off the ledge normal (start rotated around the ledge midpoint)
	var ang := deg_to_rad(float(OS.get_environment("TRAV_ANGLE")) if OS.get_environment("TRAV_ANGLE") != "" else 0.0)
	var dir: Vector3 = c["n"]
	dir = Vector3(dir.x * cos(ang) - dir.y * sin(ang), dir.x * sin(ang) + dir.y * cos(ang), 0.0)
	var start: Vector3 = c["mid"] + dir * (float(OS.get_environment("TRAV_DIST")) if OS.get_environment("TRAV_DIST") != "" else 450.0)
	start.z = c["gz"]
	var yaw := UE.vec_to_rotator_yaw(-dir)
	ch.control_yaw = yaw
	ch.wants_to_walk = OS.get_environment("TRAV_WALK") != ""
	grab_logged = false
	ch.spawn_at_ue(start + Vector3(0, 0, 90.0), yaw)
	pressed = false
	case_frame = 0
	print("case %d: ledge h=%.0f depth=%.0f at %s normal %s" % [i, c["h"], c["depth"], str(c["mid"]), str(c["n"])])

func _process(_dt: float) -> bool:
	frame += 1
	if frame == 3 and OS.get_environment("VARIANT") != "":
		ch = main.spawn_character(OS.get_environment("VARIANT"))
	if frame == 5:
		shots = DisplayServer.get_name() != "headless" and OS.get_environment("TRAV_SCREEN") == ""
		print("display: ", DisplayServer.get_name(), " shots=", shots)
		_setup_cases()
		print("cases: %d" % cases.size())
		case_idx = int(OS.get_environment("TRAV_CASE")) if OS.get_environment("TRAV_CASE") != "" else 0
		_start_case(case_idx)
		return false
	if case_idx < 0:
		return false
	if OS.get_environment("TRAV_SCREEN") != "" and screen_log == null:
		screen_log = FileAccess.open(OS.get_environment("TRAV_SCREEN"), FileAccess.WRITE)
		RenderingServer.frame_pre_draw.connect(_screen_sample) # after all _process callbacks: what is actually drawn
	case_frame += 1
	match case_frame:
		30:
			_key(KEY_W, OS.get_environment("TRAV_STILL") == "")
			if OS.get_environment("TRAV_SPRINT") != "":
				_key(KEY_SHIFT, true)
		170:
			if OS.get_environment("TRAV_HOLD") == "":
				_key(KEY_W, false)
	if frame == 6 and OS.get_environment("TRAV_NOFP") != "":
		CVars.set_value("a.AnimNode.FootPlacement.Enable", 0)
	if OS.get_environment("TRAV_JERK") != "" and case_frame > 30:
		_jerk()
	if OS.get_environment("TRAV_BONES") != "" and case_frame > 30:
		_log_bones()
	var c: Dictionary = cases[case_idx]
	if OS.get_environment("TRAV_GRAB") != "":
		_log_grab(c)
	# TRAV_SIDE: orbit the camera this many degrees (control yaw) 40 frames after the jump, for side-view screenshots
	if OS.get_environment("TRAV_SIDE") != "" and pressed and case_frame - press_frame == 40:
		ch.control_yaw = UE.normalize_axis(ch.control_yaw + float(OS.get_environment("TRAV_SIDE")))
		_key(KEY_W, false)
	var dist := UE.size_2d(ch._actor_location_ue() - c["mid"])
	if not pressed and case_frame > 30 and dist < 170.0:
		pressed = true
		press_frame = case_frame
		_key(KEY_SPACE, true)
	if pressed and case_frame == press_frame + 2:
		_key(KEY_SPACE, false)
	if OS.get_environment("TRAV_Q") != "" and pressed and str(case_frame - press_frame) in OS.get_environment("TRAV_Q").split(","):
		_dump_query()
	if OS.get_environment("TRAV_TRACE") != "" and pressed and case_frame - press_frame == 3:
		var tr: Dictionary = ch.traversal_result
		for k in ["front_ledge_location", "front_ledge_normal", "back_ledge_location", "back_ledge_normal", "back_floor_location", "obstacle_height", "obstacle_depth"]:
			print("  TR %s = %s" % [k, str(tr.get(k))])
		print("  TR dist back ledge=%s back floor=%s" % [ch.animated_dist_front_to_back_ledge, ch.animated_dist_front_to_back_floor])
		for wn in ["FrontLedge", "BackLedge", "BackFloor"]:
			if tr.has("montage"):
				print("  WIN %s %s" % [wn, str(ch.anim.get_montage_warp_windows(tr["montage"], wn))])
	if OS.get_environment("TRAV_TRACE") != "" and pressed and case_frame - press_frame <= int(OS.get_environment("TRAV_TRACE_LEN") if OS.get_environment("TRAV_TRACE_LEN") != "" else "110") and (case_frame - press_frame) % int(OS.get_environment("TRAV_TRACE_STEP") if OS.get_environment("TRAV_TRACE_STEP") != "" else "2") == 0:
		var ms2: Dictionary = ch.anim.montage_state()
		var st2: Dictionary = ch.anim.get_state()
		var ro: Transform3D = ch.anim.get_root_offset_ue()
		var pel: Transform3D = ch.anim.get_bone_cs_ue("pelvis")
		print("  +%d pos=%s yaw=%.0f vel=%s mode=%d trav=%s mont=%s t=%.2f w=%.2f | db=%s anim=%s t=%.2f c=%.3f cc=%.3f adv=%s | ro=%s rq=%s | pelvis=%s" % [case_frame - press_frame,
			str(ch._actor_location_ue().round()), ch.actor_yaw, str(ch.velocity_ue.round()) if "velocity_ue" in ch else "?", ch.MovementMode, ch.doing_traversal,
			String(ms2.get("montage", "")).get_file().replace("AM_M_Neutral_Traversal_", ""), ms2.get("position", 0.0), ms2.get("weight", 0.0),
			String(st2.database).replace("PSD_Dense_", ""), st2.anim, st2.anim_time, st2.cost, minf(st2.continuing_cost, 99.0), st2.can_advance, str(ro.origin.round()), str(ro.basis.get_rotation_quaternion().get_euler() * 57.3).pad_decimals(0), str(pel.origin.round())])
	if (pressed and (case_frame - press_frame) in [1, 40, 80, 120, 160, 200, 240]) or case_frame == 419:
		var ms: Dictionary = ch.anim.montage_state()
		for inst in ch.extra_instances:
			var ems: Dictionary = inst["fields"]["anim"].montage_state()
			print("    extra %s montage=%s t=%.2f w=%.2f" % [inst["fields"]["component_tags"], String(ems.get("montage", "")).get_file(), ems.get("position", 0.0), ems.get("weight", 0.0)])
		var r: Dictionary = ch.last_traversal_check
		print("  f%d pos=%s yaw=%.0f trav=%s fly=%s fall=%s montage=%s t=%.2f w=%.2f | action=%s h=%.0f d=%.0f backfloor=%s blh=%.0f" % [case_frame,
			str(ch._actor_location_ue().round()), ch.actor_yaw, ch.doing_traversal, ch.is_flying, ch.is_falling, String(ms.get("montage", "")).get_file(),
			ms.get("position", 0.0), ms.get("weight", 0.0), r.get("action", -1), r.get("obstacle_height", 0.0), r.get("obstacle_depth", 0.0), r.get("has_back_floor", false), r.get("back_ledge_height", 0.0)])
	var step := int(OS.get_environment("TRAV_STEP")) if OS.get_environment("TRAV_STEP") != "" else 0
	if shots and pressed and ((step == 0 and (case_frame - press_frame) in [10, 30, 50, 80]) or (step > 0 and case_frame - press_frame <= (int(OS.get_environment("TRAV_STEP_END")) if OS.get_environment("TRAV_STEP_END") != "" else 100) and (case_frame - press_frame) % step == 0)):
		root.get_viewport().get_texture().get_image().save_png("user://trav_%d_%03d.png" % [case_idx, case_frame - press_frame])
	if case_frame >= 420:
		if not jerk_stats.is_empty():
			var srt := jerk_stats.duplicate(); srt.sort()
			print("  JERKSUM case%d median=%.2f p95=%.2f max=%.2f" % [case_idx, srt[srt.size() / 2], srt[int(srt.size() * 0.95)], srt[-1]])
			jerk_stats.clear(); jerk_hist.clear()
		case_idx += 1
		if case_idx >= cases.size() or (shots and case_idx > 8) or OS.get_environment("TRAV_CASE") != "":
			quit()
			return false
		_start_case(case_idx)
	return false

var _schemas := {}
func _dump_query() -> void:
	if _schemas.is_empty():
		var mm: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/mm/mm.json"))
		for k in mm["databases"]:
			_schemas[k] = mm["schemas"][mm["databases"][k]["schema"]]
	var st: Dictionary = ch.anim.get_state()
	var tr: Dictionary = ch.last_traversal_check
	print("  Q +%d db=%s anim=%s t=%.2f cost=%.3f z=%.0f vz=%s time_to_land=%s" % [case_frame - press_frame, st.database, st.anim, st.anim_time, st.cost, ch._actor_location_ue().z, str(ch.get("ue_velocity")), str(ch.get("TrajectoryCollision"))])
	var sch = _schemas.get(String(st.database).get_file())
	if sch == null:
		for k in _schemas:
			if String(st.database).ends_with(k):
				sch = _schemas[k]
	var q: PackedFloat32Array = st.query
	var pf: PackedFloat32Array = st.pose_features
	if sch == null or q.is_empty():
		return
	for c in sch["channels"]:
		var o: int = c["offset"]
		var qs := []
		var ps := []
		for i in c["cardinality"]:
			qs.append("%.0f" % q[o + i])
			ps.append("%.0f" % pf[o + i])
		print("     %-9s %-8s %5.2f w=%.1f q=[%s] p=[%s]" % [c["type"], c.get("bone", ""), c.get("sample_time_offset", 0.0), c.get("weight", 1.0), ", ".join(qs), ", ".join(ps)])

## Twitch metric: per bone |p(t) - 2 p(t-1) + p(t-2)| (cm/frame^2) in skeleton space, max over the main body bones.
## Prints frames above TRAV_JERK (threshold) with the MM state, and a per-case summary at the end of the case.
func _jerk() -> void:
	var sk: Skeleton3D = ch.skeleton
	var cur := PackedVector3Array()
	for b in sk.get_bone_count():
		cur.append(sk.get_bone_global_pose(b).origin * 100.0)
	jerk_hist.append(cur)
	if jerk_hist.size() > 3:
		jerk_hist.pop_front()
	if jerk_hist.size() < 3:
		return
	var a: PackedVector3Array = jerk_hist[0]
	var b2: PackedVector3Array = jerk_hist[1]
	var mx := 0.0
	var mb := -1
	for i in cur.size():
		var name := sk.get_bone_name(i)
		if not name in ["pelvis", "spine_03", "head", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "foot_l", "foot_r", "calf_l", "calf_r"]:
			continue
		var j := (cur[i] - 2.0 * b2[i] + a[i]).length()
		if j > mx:
			mx = j; mb = i
	jerk_stats.append(mx)
	if OS.get_environment("TRAV_FP") != "" and pressed and str(case_frame - press_frame) in OS.get_environment("TRAV_FP").split(","):
		print("  FP +%d jerk=%.1f %s" % [case_frame - press_frame, mx, str(ch.anim.get_foot_placement_debug()).left(1500)])
	if mx > float(OS.get_environment("TRAV_JERK")):
		var st: Dictionary = ch.anim.get_state()
		var ms: Dictionary = ch.anim.montage_state()
		print("  JERK case%d +%d %.1f bone=%s | db=%s anim=%s t=%.2f stack=%s mont=%s w=%.2f" % [case_idx, case_frame - press_frame if pressed else -case_frame, mx, sk.get_bone_name(mb),
			String(st.database).replace("PSD_Dense_", ""), st.anim, st.anim_time, st.get("blend_stack", "?"), String(ms.get("montage", "")).get_file(), ms.get("weight", 0.0)])
		if OS.get_environment("TRAV_JERK_FULL") != "":
			for k in st:
				if not (st[k] is PackedFloat32Array):
					print("      %s = %s" % [k, str(st[k]).left(300)])
			print("      mode=%s last=%s state=%s gait=%s interrupt=%s blend=%s" % [ch.MovementMode, ch.movement_mode_last, ch.MovementState, ch.Gait, ch._mm_interrupt_mode() if ch.has_method("_mm_interrupt_mode") else "?", ch._mm_blend_time()])

## Per rendered frame (run windowed without --fixed-fps): pelvis position in camera space, using the transforms
## actually drawn (interpolated). High-frequency wobble here is what the player sees as jitter.
func _screen_sample() -> void:
	var cam := root.get_viewport().get_camera_3d()
	var sk: Skeleton3D = ch.skeleton
	if cam == null or sk == null:
		return
	var pel := sk.get_global_transform_interpolated() * sk.get_bone_global_pose(sk.find_bone("pelvis"))
	var cs := cam.get_global_transform_interpolated().affine_inverse() * pel.origin
	var ms: Dictionary = ch.anim.montage_state()
	var co := cam.get_global_transform_interpolated().origin
	var mr: Vector3 = ch.mesh_root.get_global_transform_interpolated().origin
	screen_log.store_line("%d %d %.5f %.5f %.5f %d %s %.3f cam=%.4f,%.4f,%.4f pel=%.4f,%.4f,%.4f mesh=%.4f,%.4f,%.4f" % [Time.get_ticks_usec(), Engine.get_physics_frames(), cs.x, cs.y, cs.z,
		case_frame, String(ms.get("montage", "")).get_file() if ms.get("weight", 0.0) > 0.0 else "-", ms.get("position", 0.0), co.x, co.y, co.z,
		pel.origin.x, pel.origin.y, pel.origin.z, mr.x, mr.y, mr.z] + " proc=%.1f phys=%.1f nav=%.1f obj=%d draw=%d" % [Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0,
		Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0, Performance.get_monitor(Performance.TIME_NAVIGATION_PROCESS) * 1000.0,
		Performance.get_monitor(Performance.OBJECT_COUNT), Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)])

var bones_log: FileAccess
## Every bone (skeleton space, cm) per tick, for offline zig-zag analysis (scratchpad/analyze_zigzag.py).
func _log_bones() -> void:
	if bones_log == null:
		bones_log = FileAccess.open(OS.get_environment("TRAV_BONES"), FileAccess.WRITE)
		var names := []
		for b in ch.skeleton.get_bone_count():
			names.append(ch.skeleton.get_bone_name(b))
		bones_log.store_line("NAMES " + ",".join(names))
	var sk: Skeleton3D = ch.skeleton
	var parts := PackedStringArray()
	for b in sk.get_bone_count():
		var p := sk.get_bone_global_pose(b).origin * 100.0
		parts.append("%.3f,%.3f,%.3f" % [p.x, p.y, p.z])
	var ms: Dictionary = ch.anim.montage_state()
	bones_log.store_line("%d %s %.3f %s" % [case_frame, String(ms.get("montage", "")).get_file() if ms.get("weight", 0.0) > 0.0 else "-", ms.get("weight", 0.0), ";".join(parts)])


## TRAV_GRAB: once a climb / mantle montage passes TRAV_GRAB seconds, print both hands relative to the ledge: dz (hand
## bone above the ledge top) and dn (along the ledge normal; negative = over the top surface), for the visible mesh.
func _log_grab(c: Dictionary) -> void:
	var ms: Dictionary = ch.anim.montage_state()
	var m := String(ms.get("montage", ""))
	if grab_logged or m == "" or float(ms.get("position", 0.0)) < float(OS.get_environment("TRAV_GRAB")):
		return
	grab_logged = OS.get_environment("TRAV_GRAB_END") == "" or float(ms.get("position", 0.0)) > float(OS.get_environment("TRAV_GRAB_END"))
	var sk: Skeleton3D = ch.skeleton
	if ch.visual != null:
		sk = ch.visual.find_children("*", "Skeleton3D", true, false)[0]
	var out := []
	for bn in ["hand_l", "hand_r"]:
		var wp: Vector3 = GaspAnimator.godot_to_ue_pos((sk.global_transform * sk.get_bone_global_pose(sk.find_bone(bn))).origin)
		var n: Vector3 = c["n"]
		out.append("%s dz=%.1f dn=%.1f" % [bn, wp.z - c["mid"].z, (wp - c["mid"]).dot(n)])
	var tr: Dictionary = ch.traversal_result
	if tr.has("front_ledge_location"):
		var fl: Vector3 = tr["front_ledge_location"]
		var rw: Vector3 = GaspAnimator.godot_to_ue_pos((ch.skeleton.global_transform * ch.skeleton.get_bone_global_pose(0)).origin)
		var al: Vector3 = ch._actor_location_ue()
		var t2 := Vector3(c["n"].y, -c["n"].x, 0.0)
		out.append("root-ledge: dn=%.1f dz=%.1f  actor-ledge: dn=%.1f dt=%.1f dz=%.1f yaw=%.1f" % [(rw - fl).dot(c["n"]), rw.z - fl.z, (al - fl).dot(c["n"]), (al - fl).dot(t2), al.z - fl.z, ch.actor_yaw])
	print("GRAB case %d h=%.0f %s @%.2f: %s" % [case_idx, c["h"], m.get_file().replace("AM_M_Neutral_Traversal_", ""), ms.get("position", 0.0), "  ".join(out)])
