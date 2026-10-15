extends SceneTree
## FootPlacement/LegIK test: stand on a slope of the landscape (side-on), then walk up it. Prints foot/pelvis
## offsets and the ground height under each foot; screenshots with FootPlacement on and off.

var frame := 0
var main: Node
var ch
var spot := Vector3.ZERO
var yaw := 0.0
var jit := []

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	ch = main.get_node("Character")

func _shot(name: String) -> void:
	if DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://feet_%s.png" % name)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _find_slope() -> void:
	var f := FileAccess.open("res://data/level.json", FileAccess.READ)
	var d: Dictionary = JSON.parse_string(f.get_as_text())
	for a in d["actors"]:
		if String(a["class"]).contains("Landscape"):
			var hf: Dictionary = a["heightfield"]
			var H: Array = hf["heights"]
			var step: float = hf["step"]
			var best := 1e9
			for j in range(2, int(hf["ny"]) - 2):
				for i in range(2, int(hf["nx"]) - 2):
					if H[j][i] == null or H[j][i + 1] == null or H[j + 1][i] == null:
						continue
					var gx := (float(H[j][i + 1]) - float(H[j][i])) / step
					var gy := (float(H[j + 1][i]) - float(H[j][i])) / step
					var slope := rad_to_deg(atan(Vector2(gx, gy).length()))
					var score := absf(slope - 20.0)
					if score < best:
						best = score
						spot = Vector3(hf["x0"] + i * step, hf["y0"] + j * step, float(H[j][i]))
						# face across the slope (perpendicular to the gradient)
						yaw = rad_to_deg(atan2(gy, gx)) + 90.0
			print("slope spot ", spot, " yaw ", yaw, " err ", best)

func _report(tag: String) -> void:
	var fp: Dictionary = ch.anim.get_foot_placement_debug()
	var s := tag
	for l in fp["legs"]:
		var out: Vector3 = l["output"]
		var g: Dictionary = ch._trace_ue(out + Vector3(0, 0, 100), out - Vector3(0, 0, 200))
		s += " | foot out z=%.1f in z=%.1f ground=%.1f plant=%d speed=%.0f" % [out.z, l["input"].z, g.get("point", Vector3.ZERO).z, l["plant"], l["speed"]]
	s += " | pelvis dz=%.1f" % (fp["pelvis_out"].z - fp["pelvis_in"].z)
	var lf: Vector3 = UE.from_godot_pos(ch.skeleton.global_transform * ch.skeleton.get_bone_global_pose(ch.skeleton.find_bone("foot_l")).origin)
	var rf: Vector3 = UE.from_godot_pos(ch.skeleton.global_transform * ch.skeleton.get_bone_global_pose(ch.skeleton.find_bone("foot_r")).origin)
	s += " | skel foot_l z=%.1f foot_r z=%.1f" % [lf.z, rf.z]
	print(s)

func _process(_dt: float) -> bool:
	frame += 1
	if frame >= 100 and frame < 130:
		var fp: Dictionary = ch.anim.get_foot_placement_debug()
		jit.append([fp["pelvis_out"].z - fp["pelvis_in"].z, fp["legs"][0]["output"].z, fp["legs"][1]["output"].z])
	if frame == 130:
		var mx := [0.0, 0.0, 0.0]
		for i in range(1, jit.size()):
			for k in 3:
				mx[k] = maxf(mx[k], absf(jit[i][k] - jit[i - 1][k]))
		print("idle max per-frame delta: pelvis %.3f footR %.3f footL %.3f" % mx)
	match frame:
		5:
			_find_slope()
			ch.control_yaw = yaw - 60.0
			ch.spawn_at_ue(spot + Vector3(0, 0, 100), yaw)
			CVars.set_value("a.AnimNode.FootPlacement.Debug", 1)
		90:
			_report("idle FP on ")
			_shot("idle_on")
			CVars.set_value("a.AnimNode.FootPlacement.Enable", 0)
		130:
			_report("idle FP off")
			_shot("idle_off")
			CVars.set_value("a.AnimNode.FootPlacement.Enable", 1)
		170:
			_report("idle FP on2")
			_key(KEY_W, true)
		200, 215, 230:
			_report("walk %d" % frame)
			if frame == 215:
				_shot("walk")
		260:
			_key(KEY_W, false)
		330:
			_report("stopped   ")
			_shot("stopped")
			ch.spawn_at_ue(main.get_node("Level").player_start["pos"], main.get_node("Level").player_start["yaw"])
		420:
			_report("flat idle ")
			_shot("flat")
			quit()
	return false
