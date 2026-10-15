extends SceneTree
## CBP_SandboxCharacter_SetupComparison: spawns it through the Character Select button, runs / pivots / stops / jumps,
## logs both instances (MM database vs SM state) and screenshots.

var frame := 0
var main: Node
var shots := {80: "cmp_idle", 160: "cmp_run", 215: "cmp_pivot", 300: "cmp_stop", 420: "cmp_jump"}

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _process(_dt: float) -> bool:
	frame += 1
	if frame == 5:
		main.get_node("GaspUI").open_panel("Game Animation Widget")
	if frame == 10:
		for b in main.get_node("GaspUI").find_children("*", "Button", true, false):
			if b.text == "SetupComparison" or b.text == "Setup Comparison":
				b.pressed.emit()
	if frame == 15:
		main.get_node("GaspUI").close_all()
	var ch = main.get_node("Character")
	match frame:
		40: ch.control_yaw = ch.actor_yaw + 160.0  # face the camera
		90: ch.control_yaw = ch.actor_yaw - 20.0
		100: _key(KEY_W, true)
		200: _key(KEY_W, false); _key(KEY_S, true)
		260: _key(KEY_S, false)
		380: _key(KEY_W, true)
		400: _key(KEY_SPACE, true)
		404: _key(KEY_SPACE, false)
		460: _key(KEY_W, false)
	if frame % 20 == 0 and frame > 20:
		var s := "f%d variant=%s main_db=%s" % [frame, ch.character_variant, ch.anim.get_state().get("database", "")]
		for inst in ch.extra_instances:
			var f: Dictionary = inst["fields"]
			var st: Dictionary = f["anim"].get_state()
			if f["use_experimental_state_machine"]:
				s += " | SM %s %s" % [f["sm"].NODE_NAMES[f["sm"].node].strip_edges(), st.get("sm_anim", "")]
			else:
				s += " | MM %s %s" % [st.get("database", ""), st.get("anim", "")]
			var sk: Skeleton3D = f["skeleton"]
			s += " foot_z=%.1f" % (UE.from_godot_pos(sk.global_transform * sk.get_bone_global_pose(sk.find_bone("foot_l")).origin).z)
		print(s)
	if shots.has(frame) and DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://%s.png" % shots[frame])
	return frame >= 480
