extends SceneTree
## Headless smoke test: loads main.tscn, drives input for a scripted sequence, prints animator state.

var frame := 0
var main: Node
var ch

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

func _process(_dt: float) -> bool:
	frame += 1
	match frame:
		60: _key(KEY_W, true)
		240: _key(KEY_SHIFT, true)
		360: _key(KEY_SHIFT, false); _key(KEY_W, false)
		380: ch.control_pitch = 35.0
		400: ch.control_yaw = 50.0
		419: ch.control_pitch = 0.0; ch.control_yaw = 0.0
		420: _key(KEY_D, true)
		480: _key(KEY_D, false); _key(KEY_S, true)
		540: _key(KEY_S, false)
		600: ch._try_jump()
		700: ch._set_crouched(true); _key(KEY_W, true)
		820: _key(KEY_W, false)
	if frame in [379, 395, 399, 415]:
		var hx: Transform3D = ch.anim.get_bone_cs_ue("head")
		print("f%d head pos=%s fwd(Y)=%s ao_enabled=%s ao=%s" % [frame, str(hx.origin), str(hx.basis.y), str(ch._enable_ao(ch._ao_value())), str(ch._ao_value())])
	if frame % 30 == 0:
		var st: Dictionary = ch.anim.get_state()
		print("f%d speed=%.0f pos=%s yaw=%.0f state=%d mode=%d db=%s anim=%s t=%.2f cost=%.2f stack=%d" % [frame, ch.Speed2D, str(ch.global_position), ch.actor_yaw, ch.MovementState, ch.MovementMode, st.database, st.anim, st.anim_time, st.cost, st.blend_stack])
	if frame in [50, 100, 150, 200, 300, 395, 415, 470, 520, 560, 640, 680, 760, 800] and DisplayServer.get_name() != "headless":
		var img := root.get_viewport().get_texture().get_image()
		img.save_png("user://shot_%04d.png" % frame)
	if frame >= 900:
		quit()
	return false
