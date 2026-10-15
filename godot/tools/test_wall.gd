extends SceneTree
## Same protocol as tools/ue/pie_wall_probe.py (UE PIE): place the character, idle SETTLE s, then hold forward
## (control yaw = case yaw) for PIE_SECONDS; log per frame t, dt, location, velocity, database, anim and
## component-space bone positions in the probe's format so scratchpad/analyze_pie.py can compare both engines.
## Env: PIE_CASES="x,y,z,yaw,dirx,diry;..." (UE units), PIE_OUT=<log path>, PIE_SECONDS (default 4).

const BONES := ["pelvis", "spine_03", "head", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "foot_l", "foot_r", "calf_l", "calf_r"]
const SETTLE := 1.5

var main: Node
var ch
var frame := 0
var cases := []
var case_idx := -1
var t := 0.0
var seconds := 4.0
var log_file: FileAccess
var held := false
var jumped := false
var jump_release := -1

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	ch = main.get_node("Character")
	var spec := OS.get_environment("PIE_CASES")
	for c in (spec if spec != "" else "3400,0,110,180,-1,0").split(";"):
		var v := []
		for s in c.split(","):
			v.append(float(s))
		cases.append(v)
	if OS.get_environment("PIE_SECONDS") != "":
		seconds = float(OS.get_environment("PIE_SECONDS"))
	log_file = FileAccess.open(OS.get_environment("PIE_OUT") if OS.get_environment("PIE_OUT") != "" else "user://wall.txt", FileAccess.WRITE)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _start(i: int) -> void:
	case_idx = i
	var c: Array = cases[i]
	ch.spawn_at_ue(Vector3(c[0], c[1], c[2]), c[3])
	ch.control_yaw = c[3]
	t = 0.0
	jumped = false
	log_file.store_line("CASE %d %s" % [i, str(c)])

func _process(dt: float) -> bool:
	frame += 1
	if frame == 3 and OS.get_environment("PIE_VARIANT") != "":
		ch = main.spawn_character(OS.get_environment("PIE_VARIANT"))
	if frame == 5:
		if OS.get_environment("PIE_NOFP") != "":
			CVars.set_value("a.AnimNode.FootPlacement.Enable", 0)
		_start(0)
		return false
	if case_idx < 0:
		return false
	t += dt
	if t > SETTLE and not held:
		held = true
		_key(KEY_W, true)
		# PIE_SPRINT / PIE_HOLD_JUMP: hold Shift / Space from the start of the run (as pie_wall_probe.py injects them every frame)
		if OS.get_environment("PIE_SPRINT") != "":
			_key(KEY_SHIFT, true)
		if OS.get_environment("PIE_HOLD_JUMP") != "":
			_key(KEY_SPACE, true)
	var cs: Array = cases[case_idx]
	if jump_release == frame:
		_key(KEY_SPACE, false)
	if cs.size() >= 8 and not jumped and t > SETTLE and UE.size_2d(ch._actor_location_ue() - Vector3(cs[6], cs[7], 0)) < 170.0:
		jumped = true
		_key(KEY_SPACE, true)
		jump_release = frame + 2
		log_file.store_line("JUMP t=%.4f" % t)
	var st: Dictionary = ch.anim.get_state()
	var sk: Skeleton3D = ch.skeleton
	var parts := []
	for b in BONES:
		var p: Vector3 = sk.get_bone_global_pose(sk.find_bone(b)).origin * 100.0
		parts.append("%.2f,%.2f,%.2f" % [p.x, p.y, p.z])
	var loc: Vector3 = ch._actor_location_ue()
	var vel: Vector3 = ch.ue_velocity
	var ms: Dictionary = ch.anim.montage_state()
	var mt := "%s@%.3f" % [String(ms.get("montage", "")).get_file(), ms.get("position", 0.0)] if String(ms.get("montage", "")) != "" and ms.get("weight", 0.0) > 0.0 else ""
	log_file.store_line("F t=%.4f dt=%.4f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f db=%s bones=%s mont=%s anim=%s at=%.3f" % [t, dt, loc.x, loc.y, loc.z,
		vel.x, vel.y, vel.z, st.database, ";".join(parts), mt, st.anim, st.anim_time])
	if OS.get_environment("PIE_HANDS") != "":
		var hsk: Skeleton3D = ch.skeleton
		if ch.visual != null:  # retargeted character: the visible mesh
			hsk = ch.visual.find_children("*", "Skeleton3D", true, false)[0]
		var hw := []
		for bn in ["hand_l", "hand_r", "pelvis"]:
			var wp: Vector3 = (hsk.global_transform * hsk.get_bone_global_pose(hsk.find_bone(bn))).origin
			hw.append("%s=%s" % [bn, str(GaspAnimator.godot_to_ue_pos(wp).round())])
		log_file.store_line("  HANDS t=%.4f %s mont=%s" % [t, " ".join(hw), mt])
	if OS.get_environment("PIE_FOLEY") != "":
		for n in ch.anim.get_fired_notifies():
			if String(n["class"]).contains("Foley"):
				log_file.store_line("  FOLEY t=%.4f %s%s w=%.2f %s mode=%s trav=%s mont=%s" % [t, String(n["class"]).replace("BP_AnimNotify_FoleyEvent_", "").replace("_C", ""),
					n["side"], n["weight"], "MONTAGE" if n["montage"] else "MM", "fly" if ch.is_flying else ("fall" if ch.is_falling else "walk"), ch.doing_traversal, mt])
	if OS.get_environment("PIE_FLAGS") != "":
		log_file.store_line("  FLAGS t=%.4f starting=%s pivoting=%s traversed=%s landL=%s landH=%s spin=%s tags=%s vel=%s fut=%s turn=%.1f" % [t, ch.IsStarting, ch.IsPivoting,
			ch.JustTraversed, ch.JustLanded_Light, ch.JustLanded_Heavy, ch.ShouldSpinTransition, ch.CurrentDatabaseTags if "CurrentDatabaseTags" in ch else "?",
			ch.abp_velocity.round(), ch.trj_future_velocity.round(), ch._trajectory_turn_angle()])
		log_file.store_line("  COSTS cont=%.3f %s" % [st.get("continuing_cost", 0.0), st.get("db_costs", "")])
	if t > SETTLE + seconds:
		_key(KEY_W, false)
		held = false
		if case_idx + 1 >= cases.size():
			log_file.close()
			quit()
			return false
		_start(case_idx + 1)
	return false
