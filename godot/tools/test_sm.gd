extends SceneTree
## Experimental state machine test (DDCVar.ExperimentalStateMachine.Enable 1, Debug 1): idle, start forward, run loop,
## pivot (W -> S), stop, idle break wait, jump while running, crouch walk, strafe left. Logs SM state / blend stack anim
## changes and the foot heights, screenshots key moments.

var frame := 0
var main: Node
var last := ""
var shots := {60: "sm_idle", 130: "sm_start", 200: "sm_loop", 245: "sm_pivot", 330: "sm_stop", 560: "sm_idlebreak", 640: "sm_jump", 760: "sm_crouch", 860: "sm_strafe"}

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	CVars.set_value("DDCVar.ExperimentalStateMachine.Enable", 1)
	CVars.set_value("DDCVar.ExperimentalStateMachine.Debug", 1)
	CVars.set_value("a.AnimNode.MotionMatching.DebugDrawQuery", 1)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _process(dt: float) -> bool:
	frame += 1
	var ch = main.get_node("Character")
	match frame:
		100: _key(KEY_W, true)
		230: _key(KEY_W, false); _key(KEY_S, true)
		300: _key(KEY_S, false)
		600: _key(KEY_W, true)
		630: _key(KEY_SPACE, true)
		634: _key(KEY_SPACE, false)
		700: _key(KEY_W, false); _key(KEY_C, true)
		702: _key(KEY_C, false)
		710: _key(KEY_W, true)
		800: _key(KEY_W, false); _key(KEY_A, true)
		900: _key(KEY_A, false)
	if ch.use_experimental_state_machine and ch.sm != null:
		var st: Dictionary = ch.anim.get_state()
		var s := "%s | %s" % [ch.sm.NODE_NAMES[ch.sm.node].strip_edges(), st.get("sm_anim", "")]
		if s != last:
			print("f%d %s t=%.2f rate=%.2f dir=%s nva=%s speed=%.0f tags=%s" % [frame, s, st.get("sm_time", 0.0), st.get("sm_play_rate", 1.0),
				ch.sm.DIR_NAMES[ch.sm.MovementDirection], ch.sm.NoValidAnim, ch.Speed2D, str(ch.sm.BlendStackInputs["Tags"])])
			last = s
	if shots.has(frame) and DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://%s.png" % shots[frame])
	if frame == 60:
		print("use_sm=", ch.use_experimental_state_machine)
	return frame >= 960
