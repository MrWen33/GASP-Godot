extends SceneTree
## Consecutive traversals: run along a row of obstacles holding forward + jump (space) the whole time and log, per
## physics tick, movement mode, montage state, MM database/anim and the largest skeleton-space bone jump (second
## difference, cm) so animation pops after landings show up as spikes.
## Env: CHAIN_SPRINT=1 (hold shift), CHAIN_HANDS=1 (world hand positions during montages), CHAIN_START="x,y,z,yaw" (UE, default the y=0 hurdle row), CHAIN_TICKS (default 480), CHAIN_NOSPACE=1 (forward only).

var frame := 0
var main: Node
var ch
var hist := []

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	ch = main.get_node("Character")

func _key(code: Key, down: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = down
	Input.parse_input_event(e)

func _physics_process(_dt: float) -> bool:
	frame += 1
	var spec := OS.get_environment("CHAIN_START")
	var s := [4100.0, 0.0, 92.0, 180.0] if spec == "" else Array(spec.split(",")).map(func(v): return float(v))
	if frame == 3:
		ch.control_yaw = s[3]
		ch.spawn_at_ue(Vector3(s[0], s[1], s[2]), s[3])
	if frame == 30:
		_key(KEY_W, true)
		if OS.get_environment("CHAIN_NOSPACE") == "":
			_key(KEY_SPACE, true)
		if OS.get_environment("CHAIN_SPRINT") != "":
			_key(KEY_SHIFT, true)
	if frame < 30:
		return false
	var sk: Skeleton3D = ch.skeleton
	var pos := PackedVector3Array()
	for b in sk.get_bone_count():
		pos.append(sk.get_bone_global_pose(b).origin * 100.0)
	hist.append(pos)
	if hist.size() > 3:
		hist.pop_front()
	var jerk := 0.0
	var jb := ""
	if hist.size() == 3:
		for b in sk.get_bone_count():
			var n := sk.get_bone_name(b)
			if n.begins_with("ik_") or n == "root" or n == "attach":
				continue
			var j: float = (hist[2][b] - 2.0 * hist[1][b] + hist[0][b]).length()
			if j > jerk:
				jerk = j
				jb = n
	var ms: Dictionary = ch.anim.montage_state()
	var st: Dictionary = ch.anim.get_state()
	if OS.get_environment("CHAIN_JT") != "":
		print("  JT slot_active=%s moving_traversal=%.3f turn=%.1f just_traversed=%s moving=%s starting=%s pivoting=%s vel=%s fut=%s" % [ms.get("slot_active"), ch.anim.get_curve("MovingTraversal"), ch._trajectory_turn_angle(), ch.JustTraversed, ch.IsMoving if "IsMoving" in ch else "?", ch.IsStarting, ch.IsPivoting, ch.abp_velocity, ch.trj_future_velocity])
	if OS.get_environment("CHAIN_HANDS") != "" and String(ms.get("montage", "")) != "":
		var hs := []
		for bn in ["hand_l", "hand_r"]:
			hs.append(str(GaspAnimator.godot_to_ue_pos((sk.global_transform * sk.get_bone_global_pose(sk.find_bone(bn))).origin).round()))
		var tr: Dictionary = ch.traversal_result
		var ro: Transform3D = ch.anim.get_root_offset_ue()
		print("  HANDS %s %s actor=%s yaw=%.1f ledge=%s action=%s root_offset=%s slot_active=%s" % [hs[0], hs[1], str(ch._actor_location_ue().round()), ch.actor_yaw,
			str(tr.get("front_ledge_location", Vector3.ZERO).round()), tr.get("action", -1), str(ro.origin.round()), ms.get("slot_active")])
	print("T%d pos=%s mode=%s trav=%s mont=%s mt=%.2f w=%.2f stop=%s | db=%s anim=%s t=%.2f | jerk=%.1f %s" % [frame,
		str(ch._actor_location_ue().round()), "fly" if ch.is_flying else ("fall" if ch.is_falling else "walk"), ch.doing_traversal,
		String(ms.get("montage", "")).get_file().replace("AM_M_Neutral_Traversal_", ""), ms.get("position", 0.0), ms.get("weight", 0.0), ms.get("stopping", false),
		String(st.database).replace("PSD_Dense_", ""), String(st.anim).replace("M_Neutral_", ""), st.anim_time, jerk, jb])
	if frame >= int(OS.get_environment("CHAIN_TICKS")) if OS.get_environment("CHAIN_TICKS") != "" else frame >= 480:
		quit()
	return false
