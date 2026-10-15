extends SceneTree
## Character select test: spawns every retargeted character through GameAnimationWidget.SpawnCharacter (main.spawn_character),
## lets it idle, walks forward, screenshots each, and reports retarget foot heights vs. the UEFN source plus frame time.
## Optional user arg: a single character name (e.g. -- Echo).

const NAMES := ["Manny", "Quinn", "UE4_Mannequin", "Echo", "TwinBlast"]

var frame := 0
var main: Node
var idx := -1
var t0 := 0
var names: Array = NAMES
var start_pos := Vector3.ZERO
var times := []

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	var args := OS.get_cmdline_user_args()
	if not args.is_empty():
		names = args

func _shot(name: String) -> void:
	if DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://char_%s.png" % name)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _world_ue(sk: Skeleton3D, bone: String) -> Vector3:
	var b := sk.find_bone(bone)
	if b < 0:
		return Vector3.INF
	return UE.from_godot_pos(sk.global_transform * sk.get_bone_global_pose(b).origin)

func _report(tag: String) -> void:
	var ch = main.get_node("Character")
	var s := "%s %s" % [tag, ch.character_variant]
	var src: Skeleton3D = ch.skeleton
	s += " | src foot_l z=%.1f foot_r z=%.1f pelvis z=%.1f" % [_world_ue(src, "foot_l").z, _world_ue(src, "foot_r").z, _world_ue(src, "pelvis").z]
	if ch.visual:
		var sk: Skeleton3D = ch.visual.primary_skeleton()
		s += " | dst foot_l z=%.1f foot_r z=%.1f pelvis z=%.1f hand_l=%s" % [_world_ue(sk, "foot_l").z, _world_ue(sk, "foot_r").z, _world_ue(sk, "pelvis").z, str(_world_ue(sk, "hand_l") - _world_ue(src, "hand_l"))]
		var rs := src.get_bone_global_rest(src.find_bone("foot_l")).origin.y * 100.0
		var rd := sk.get_bone_global_rest(sk.find_bone("foot_l")).origin.y * 100.0
		s += " | ref ankle src=%.1f dst=%.1f" % [rs, rd]
	print(s)

func _process(dt: float) -> bool:
	frame += 1
	if frame == 3:
		start_pos = main.get_node("Character").global_position
	var local := frame - 10
	if local < 0:
		return false
	var phase := local % 200
	if phase == 0:
		idx += 1
		if idx >= names.size():
			print("frame times ms: ", times)
			return true
		_key(KEY_W, false)
		var ch = main.spawn_character(names[idx])
		ch.global_position = start_pos
		ch.spawn_at_ue(UE.from_godot_pos(start_pos), 0.0)
	var ch = main.get_node("Character")
	match phase:
		20:
			ch.control_yaw = 160.0
			if OS.get_environment("PITCH") != "":
				ch.control_pitch = float(OS.get_environment("PITCH"))
		58:
			_shot(names[idx] + "_front")
		60:
			_report("idle")
			_shot(names[idx] + "_idle")
			t0 = Time.get_ticks_usec()
		61:
			times.append([names[idx], (Time.get_ticks_usec() - t0) / 1000.0])
		70:
			ch.control_yaw = 0.0
			_key(KEY_W, true)
		150:
			_report("walk")
			_shot(names[idx] + "_walk")
			_key(KEY_W, false)
		190:
			_report("end")
			_shot(names[idx] + "_end")
	return false
