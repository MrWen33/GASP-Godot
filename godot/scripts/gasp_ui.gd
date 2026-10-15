extends CanvasLayer
## GASP in-game UI: the level buttons (View Controls / Game Animation Widget / Read-Me) open the editor utility
## widgets as floating panels (EUB_OpenWidget), plus stat fps and the WB_SimpleController gamepad overlay.

const PANELS := {
	"View Controls": ["Controls", Vector2i(460, 330)],
	"Game Animation Widget": ["GameAnimationWidget", Vector2i(560, 640)],
	"Read-Me": ["ReadMe", Vector2i(640, 560)],
}

@export var character_path: NodePath
@export var camera_path: NodePath

var widgets: Dictionary
var builder := UMGBuilder.new()
var windows: Dictionary = {}
var fps_label: Label
var controller: Control
var controller_images: Dictionary = {}
var refresh_timer := 0.0
var character
var camera_rig

func _ready() -> void:
	var f := FileAccess.open("res://data/widgets.json", FileAccess.READ)
	widgets = JSON.parse_string(f.get_as_text())
	builder.on_widget_button = _on_widget_button
	character = get_node_or_null(character_path)
	camera_rig = get_node_or_null(camera_path)
	fps_label = Label.new()
	fps_label.add_theme_color_override("font_color", Color(0.3, 1.0, 0.3))
	fps_label.add_theme_color_override("font_outline_color", Color.BLACK)
	fps_label.add_theme_constant_override("outline_size", 4)
	fps_label.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	fps_label.offset_left = -180
	fps_label.offset_top = 40
	fps_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	fps_label.visible = false
	add_child(fps_label)
	_build_controller_overlay()

func on_level_button(label: String) -> void:
	var key := label.replace("\n", " ").strip_edges()
	if PANELS.has(key):
		open_panel(key)

func open_panel(key: String) -> void:
	if windows.has(key) and is_instance_valid(windows[key]):
		windows[key].grab_focus()
		return
	var info: Array = PANELS[key]
	var tree: Dictionary = widgets.get(info[0], {})
	if tree.is_empty():
		return
	var w := Window.new()
	w.title = key.replace("View ", "")
	w.size = info[1]
	w.position = Vector2i(40 + 30 * windows.size(), 60 + 30 * windows.size())
	w.wrap_controls = false
	w.transient = false
	var root := builder.build(tree)
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	w.add_child(root)
	w.close_requested.connect(func():
		windows.erase(key)
		w.queue_free()
		if windows.is_empty():
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED)
	add_child(w)
	windows[key] = w
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func close_all() -> void:
	for k in windows.keys():
		if is_instance_valid(windows[k]):
			windows[k].queue_free()
	windows.clear()

func _on_widget_button(name: String) -> void:
	match name:
		"ControllerWidgetToggle":
			controller.visible = not controller.visible
		"StillCam_Enable", "StillCam_Follow", "StillCam_Rotate":
			if camera_rig and camera_rig.has_method("still_cam"):
				camera_rig.still_cam(name.trim_prefix("StillCam_"))
		_ when name.begins_with("SpawnCharacter:"):
			var main := get_parent()
			if main and main.has_method("spawn_character"):
				main.spawn_character(name.trim_prefix("SpawnCharacter:"))

# ------------------------------------------------------------------ WB_SimpleController
func _build_controller_overlay() -> void:
	var tree: Dictionary = widgets.get("WB_SimpleController", {})
	controller = Control.new()
	controller.mouse_filter = Control.MOUSE_FILTER_IGNORE
	controller.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	# ScaleBox anchored bottom-right, offset -10; images are 1000x700 layers scaled down
	var size := Vector2(1000, 700) * 0.3
	controller.offset_left = -size.x - 10
	controller.offset_top = -size.y - 10
	controller.offset_right = -10
	controller.offset_bottom = -10
	controller.visible = false
	add_child(controller)
	_controller_images(tree, controller)

func _controller_images(n: Dictionary, parent: Control) -> void:
	for ch in n.get("children", []):
		if ch["class"] == "Image":
			var brush: String = ch["props"].get("Brush", "")
			var m := RegEx.create_from_string("Images/([A-Za-z_]+)\\.").search(brush)
			if not m:
				continue
			var tr := TextureRect.new()
			tr.texture = load("res://assets/Widgets/Images/%s.png" % m.get_string(1))
			tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			tr.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			tr.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			tr.mouse_filter = Control.MOUSE_FILTER_IGNORE
			var tint := UMGBuilder.color(brush, Color.WHITE) if brush.contains("TintColor") else Color.WHITE
			var c := UMGBuilder.color(ch["props"].get("ColorAndOpacity", ""), Color.WHITE)
			tr.modulate = Color(tint.r * c.r, tint.g * c.g, tint.b * c.b, c.a)
			tr.set_meta("base_alpha", c.a)
			parent.add_child(tr)
			controller_images[ch["name"]] = tr
		else:
			var holder := Control.new()
			holder.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
			parent.add_child(holder)
			controller_images[ch["name"]] = holder
			_controller_images(ch, holder)

func _update_controller() -> void:
	var on := {
		"A_On": Input.is_joy_button_pressed(0, JOY_BUTTON_A), "B_On": Input.is_joy_button_pressed(0, JOY_BUTTON_B),
		"X_On": Input.is_joy_button_pressed(0, JOY_BUTTON_X), "Y_On": Input.is_joy_button_pressed(0, JOY_BUTTON_Y),
		"LB_On": Input.is_joy_button_pressed(0, JOY_BUTTON_LEFT_SHOULDER), "RB_On": Input.is_joy_button_pressed(0, JOY_BUTTON_RIGHT_SHOULDER),
		"LT_On": Input.get_joy_axis(0, JOY_AXIS_TRIGGER_LEFT) > 0.1, "RT_On": Input.get_joy_axis(0, JOY_AXIS_TRIGGER_RIGHT) > 0.1,
		"DU": Input.is_joy_button_pressed(0, JOY_BUTTON_DPAD_UP), "DD": Input.is_joy_button_pressed(0, JOY_BUTTON_DPAD_DOWN),
		"DL": Input.is_joy_button_pressed(0, JOY_BUTTON_DPAD_LEFT), "DR": Input.is_joy_button_pressed(0, JOY_BUTTON_DPAD_RIGHT),
		"Select_On": Input.is_joy_button_pressed(0, JOY_BUTTON_BACK), "Start_On": Input.is_joy_button_pressed(0, JOY_BUTTON_START),
		"LS_Down": Input.is_joy_button_pressed(0, JOY_BUTTON_LEFT_STICK), "RS_Down": Input.is_joy_button_pressed(0, JOY_BUTTON_RIGHT_STICK),
	}
	var ls := Vector2(Input.get_joy_axis(0, JOY_AXIS_LEFT_X), Input.get_joy_axis(0, JOY_AXIS_LEFT_Y))
	var rs := Vector2(Input.get_joy_axis(0, JOY_AXIS_RIGHT_X), Input.get_joy_axis(0, JOY_AXIS_RIGHT_Y))
	on["LS_On"] = ls.length() > 0.1
	on["RS_On"] = rs.length() > 0.1
	for k in on:
		var tr: Control = controller_images.get(k)
		if tr:
			tr.modulate.a = 1.0 if on[k] else 0.0
	var px := controller.size.x * 0.03
	if controller_images.has("LS_Position"):
		controller_images["LS_Position"].position = ls * px
	if controller_images.has("RS_Position"):
		controller_images["RS_Position"].position = rs * px

func _process(dt: float) -> void:
	fps_label.visible = CVars.get_b("stat fps")
	if fps_label.visible:
		fps_label.text = "%.2f FPS\n%.2f ms" % [Engine.get_frames_per_second(), 1000.0 / maxf(Engine.get_frames_per_second(), 1.0)]
	if controller.visible:
		_update_controller()
	refresh_timer -= dt
	if refresh_timer <= 0.0:
		refresh_timer = 0.1
		builder.refresh_all()
