extends SceneTree
## DDCvar.MMDatabaseLOD 0 / 1 / 2: databases chosen while running forward.
var frame := 0
var main: Node
func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
func _process(_dt: float) -> bool:
	frame += 1
	var ch = main.get_node("Character")
	if frame == 30:
		var e := InputEventKey.new(); e.physical_keycode = KEY_W; e.keycode = KEY_W; e.pressed = true; Input.parse_input_event(e)
	if frame == 100: CVars.set_value("DDCvar.MMDatabaseLOD", 1)
	if frame == 105 or frame == 175:
		var e := InputEventKey.new(); e.physical_keycode = KEY_W; e.keycode = KEY_W; e.pressed = false; Input.parse_input_event(e)
	if frame == 135 or frame == 200:
		var e := InputEventKey.new(); e.physical_keycode = KEY_W; e.keycode = KEY_W; e.pressed = true; Input.parse_input_event(e)
	if frame == 170: CVars.set_value("DDCvar.MMDatabaseLOD", 2)
	if frame % 15 == 0:
		var st: Dictionary = ch.anim.get_state()
		print("f%d lod=%d db=%s anim=%s speed=%.0f" % [frame, CVars.get_i("DDCvar.MMDatabaseLOD"), st.get("database", ""), st.get("anim", ""), ch.Speed2D])
	return frame >= 245
