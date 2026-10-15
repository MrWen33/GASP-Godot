extends SceneTree
## Running turns: hold forward from the default spawn, run straight, then turn the control yaw at TURN_RATE deg/s
## (default 90) and log every foley notify that reaches FoleyAudio (time, class, side, weight, MM or montage) plus the
## blend stack players, so footstep density can be compared between straight running and turning.
## Env: TURN_RATE, TURN_START (frame, default 150), TURN_TICKS (default 600), TURN_ZIGZAG (frames per turn direction),
## TURN_KEYS (frames per phase of D / none / A / none held together with W, instead of turning the camera).

var frame := 0
var main: Node
var ch

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

func _env_f(n: String, d: float) -> float:
	return float(OS.get_environment(n)) if OS.get_environment(n) != "" else d

func _physics_process(dt: float) -> bool:
	frame += 1
	if frame == 30:
		_key(KEY_W, true)
	var kp := int(_env_f("TURN_KEYS", 0)) # frames per strafe key: alternately hold D / release / A / release
	if kp > 0 and frame >= int(_env_f("TURN_START", 150)):
		var ph := ((frame - int(_env_f("TURN_START", 150))) / kp) % 4
		_key(KEY_D, ph == 0)
		_key(KEY_A, ph == 2)
	elif frame >= int(_env_f("TURN_START", 150)):
		var rate := _env_f("TURN_RATE", 90.0)
		var period := int(_env_f("TURN_ZIGZAG", 0)) # frames per direction, 0 = always the same way
		if period > 0 and ((frame - int(_env_f("TURN_START", 150))) / period) % 2 == 1:
			rate = -rate
		ch.control_yaw = UE.normalize_axis(ch.control_yaw + rate * dt)
	if frame > 30:
		var st: Dictionary = ch.anim.get_state()
		for n in ch.anim.get_fired_notifies():
			if String(n["class"]).contains("Foley"):
				print("FOLEY f=%d name=[%s] %s%s w=%.2f %s turning=%s speed=%.0f db=%s anim=%s t=%.2f players=%s" % [frame, n["name"],
					String(n["class"]).replace("BP_AnimNotify_FoleyEvent_", "").replace("_C", ""), n["side"], n["weight"],
					"MONTAGE" if n["montage"] else "MM", frame >= int(_env_f("TURN_START", 150)), ch.Speed2D,
					String(st.database).replace("PSD_Dense_", ""), String(st.anim).replace("M_Neutral_", ""), st.anim_time, st.players])
	if frame >= int(_env_f("TURN_TICKS", 600)):
		quit()
	return false
