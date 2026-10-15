extends SceneTree
## UI test: level buttons open the GASP widget panels, console buttons drive cvars, debug draws, camera modes.

var frame := 0
var main: Node
var ch
var ui
var level

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	ch = main.get_node("Character")
	level = main.get_node("Level")

func _shot(name: String) -> void:
	if DisplayServer.get_name() != "headless":
		root.get_viewport().get_texture().get_image().save_png("user://ui_%s.png" % name)

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _find_button(w: Node, text: String) -> Button:
	for n in w.find_children("*", "Button", true, false):
		if (n as Button).text == text:
			return n
	return null

func _process(_dt: float) -> bool:
	frame += 1
	match frame:
		10:
			ui = main.get_node("GaspUI")
			# walk-on level buttons (LevelButton Trigger overlap -> ButtonPressed -> EUB_OpenWidget)
			level._on_button(ch, "Read-Me")
		20:
			_shot("readme")
			print("windows after Read-Me: ", ui.windows.keys())
			ui.close_all()
			level._on_button(ch, "View\nControls")
		30:
			_shot("controls")
			ui.close_all()
			level._on_button(ch, "Game\nAnimation\nWidget")
		40:
			_shot("gaw")
			var w = ui.windows["Game Animation Widget"]
			var b := _find_button(w, "Bones")
			print("Bones button found: ", b != null)
			b.pressed.emit()
			_find_button(w, "Trajectory").pressed.emit()
			_find_button(w, "Movement").pressed.emit()
			_find_button(w, "Collision").pressed.emit()
			_find_button(w, "Traversal").pressed.emit()
			_find_button(w, "Show FPS").pressed.emit()
			_find_button(w, "Controller").pressed.emit()
			_find_button(w, ".5").pressed.emit()
			print("cvars: bones=%s traj=%s move=%s coll=%s trav=%d fps=%s slomo=%.2f timescale=%.2f" % [CVars.get_b("show bones"),
				CVars.get_b("a.AnimNode.PoseHistory.DebugDrawTrajectory"), CVars.get_b("p.VisualizeMovement"), CVars.get_b("show collision"),
				CVars.get_i("DDCvar.Traversal.DrawDebugLevel"), CVars.get_b("stat fps"), CVars.get_f("slomo"), Engine.time_scale])
		50:
			_shot("gaw_toggled")
			_find_button(ui.windows["Game Animation Widget"], "1").pressed.emit()
			ui.close_all()
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			_key(KEY_W, true)
		110:
			_shot("debug_draw")
			_key(KEY_W, false)
			# camera system off -> SpringArm fallback
			CVars.set_value("DDCVar.NewGameplayCameraSystem.Enable", 0)
		150:
			_shot("legacy_cam")
			CVars.set_value("DDCVar.NewGameplayCameraSystem.Enable", 1)
			CVars.set_value("GameplayCameras.Debug.Enable", 1)
			for k in ["show bones", "show collision"]:
				CVars.set_value(k, 0)
		190:
			_shot("cam_debug")
			main.get_node("CameraRig").still_cam("Enable")
			main.get_node("CameraRig").still_cam("Rotate")
			_key(KEY_D, true)
		260:
			_key(KEY_D, false)
			_shot("still_cam")
			print("fps label visible: ", ui.fps_label.visible, " controller visible: ", ui.controller.visible)
			quit()
	return false
