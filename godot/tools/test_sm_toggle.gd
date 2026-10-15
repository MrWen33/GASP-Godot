extends SceneTree
## Game Animation Widget "State Machine Setup" Enable / Debug buttons toggled while running (MM -> SM -> MM).

var frame := 0
var main: Node

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)

func _button(tip: String) -> Button:
	for b in main.get_node("GaspUI").find_children("*", "Button", true, false):
		if String(b.tooltip_text).contains(tip):
			return b
	return null

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _process(_dt: float) -> bool:
	frame += 1
	var ch = main.get_node("Character")
	match frame:
		5: main.get_node("GaspUI").open_panel("Game Animation Widget")
		10:
			print("buttons: enable=%s debug=%s" % [_button("ExperimentalStateMachine.Enable") != null, _button("ExperimentalStateMachine.Debug") != null])
			_key(KEY_W, true)
		60:
			_button("ExperimentalStateMachine.Enable").pressed.emit()
			_button("ExperimentalStateMachine.Debug").pressed.emit()
		160:
			_button("ExperimentalStateMachine.Enable").pressed.emit()
		220: _key(KEY_W, false)
	if frame % 10 == 0:
		var st: Dictionary = ch.anim.get_state()
		print("f%d sm_cvar=%d use_sm=%s %s pelvis_z=%.1f" % [frame, CVars.get_i("DDCVar.ExperimentalStateMachine.Enable"), ch.use_experimental_state_machine,
			("SM " + ch.sm.NODE_NAMES[ch.sm.node].strip_edges() + " " + st.get("sm_anim", "")) if ch.use_experimental_state_machine else ("MM " + st.get("database", "") + " " + st.get("anim", "")),
			UE.from_godot_pos(ch.skeleton.global_transform * ch.skeleton.get_bone_global_pose(ch.skeleton.find_bone("pelvis")).origin).z - UE.from_godot_pos(ch.global_position).z])
	return frame >= 260
