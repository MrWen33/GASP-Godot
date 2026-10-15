class_name DebugDraw
extends MeshInstance3D
## Minimal DrawDebugLine/Sphere/Capsule equivalent (UE-space inputs, cm). Lines live for `duration` seconds
## (0 = one frame) and are drawn on top of the scene.

static var instance: DebugDraw

var _items: Array = [] # [a, b, color, time_left]
var _mesh := ImmediateMesh.new()
var _texts: Array = [] # [pos, text, color, time_left] (DrawDebugString)
var _labels: Array[Label3D] = []
var _messages: Array = [] # [text, color, time_left] (PrintString to screen, newest first)
var _screen: VBoxContainer

func _ready() -> void:
	instance = self
	top_level = true
	mesh = _mesh
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.vertex_color_use_as_albedo = true
	m.no_depth_test = true
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.render_priority = 10
	material_override = m
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF

func _exit_tree() -> void:
	if instance == self:
		instance = null

static func line(a: Vector3, b: Vector3, color := Color.WHITE, duration := 0.0) -> void:
	if instance:
		instance._items.append([UE.to_godot_pos(a), UE.to_godot_pos(b), color, duration])

static func sphere(c: Vector3, r: float, color := Color.WHITE, duration := 0.0, segments := 12) -> void:
	for axis in 3:
		var prev := Vector3.ZERO
		for i in segments + 1:
			var t := TAU * i / segments
			var p := Vector3.ZERO
			match axis:
				0: p = Vector3(cos(t), sin(t), 0)
				1: p = Vector3(cos(t), 0, sin(t))
				2: p = Vector3(0, cos(t), sin(t))
			p = c + p * r
			if i > 0:
				line(prev, p, color, duration)
			prev = p

static func capsule(c: Vector3, half_height: float, r: float, color := Color.WHITE, duration := 0.0) -> void:
	var top := c + Vector3(0, 0, half_height - r)
	var bot := c - Vector3(0, 0, half_height - r)
	for i in 16:
		var t0 := TAU * i / 16.0
		var t1 := TAU * (i + 1) / 16.0
		var d0 := Vector3(cos(t0), sin(t0), 0) * r
		var d1 := Vector3(cos(t1), sin(t1), 0) * r
		line(top + d0, top + d1, color, duration)
		line(bot + d0, bot + d1, color, duration)
		if i % 4 == 0:
			line(top + d0, bot + d0, color, duration)
	for k in 2:
		var dir := Vector3(1, 0, 0) if k == 0 else Vector3(0, 1, 0)
		for i in 8:
			var a0 := PI * i / 8.0
			var a1 := PI * (i + 1) / 8.0
			line(top + (dir * cos(a0) + Vector3(0, 0, sin(a0))) * r, top + (dir * cos(a1) + Vector3(0, 0, sin(a1))) * r, color, duration)
			line(bot + (dir * cos(a0) - Vector3(0, 0, sin(a0))) * r, bot + (dir * cos(a1) - Vector3(0, 0, sin(a1))) * r, color, duration)

## DrawDebugString (UE-space location)
static func text(pos: Vector3, t: String, color := Color.WHITE, duration := 0.0) -> void:
	if instance:
		instance._texts.append([UE.to_godot_pos(pos), t, color, duration])

## KismetSystemLibrary::PrintString (bPrintToScreen): newest message on top
static func print_string(t: String, color := Color(0.0, 0.66, 1.0), duration := 2.0) -> void:
	if instance:
		instance._messages.push_front([t, color, duration])

static func arrow(a: Vector3, b: Vector3, color := Color.WHITE, size := 10.0, duration := 0.0) -> void:
	line(a, b, color, duration)
	var d := b - a
	if d.length() < 0.01:
		return
	var f := d.normalized()
	var side := f.cross(Vector3(0, 0, 1))
	if side.length() < 0.01:
		side = Vector3(0, 1, 0)
	side = side.normalized()
	line(b, b - f * size + side * size * 0.5, color, duration)
	line(b, b - f * size - side * size * 0.5, color, duration)

func _process(dt: float) -> void:
	_mesh.clear_surfaces()
	if not _items.is_empty():
		_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
		for it in _items:
			_mesh.surface_set_color(it[2])
			_mesh.surface_add_vertex(it[0])
			_mesh.surface_set_color(it[2])
			_mesh.surface_add_vertex(it[1])
		_mesh.surface_end()
	var keep: Array = []
	for it in _items:
		it[3] -= dt
		if it[3] > 0.0:
			keep.append(it)
	_items = keep
	_update_texts(dt)
	_update_messages(dt)

func _update_texts(dt: float) -> void:
	while _labels.size() < _texts.size():
		var l := Label3D.new()
		l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		l.no_depth_test = true
		l.fixed_size = true
		l.pixel_size = 0.0012
		l.font_size = 14
		l.outline_size = 0
		l.render_priority = 11
		l.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		add_child(l)
		_labels.append(l)
	for i in _labels.size():
		var l := _labels[i]
		l.visible = i < _texts.size()
		if l.visible:
			l.global_position = _texts[i][0]
			l.text = _texts[i][1]
			l.modulate = _texts[i][2]
	var keep: Array = []
	for it in _texts:
		it[3] -= dt
		if it[3] > 0.0:
			keep.append(it)
	_texts = keep

func _update_messages(dt: float) -> void:
	if _screen == null:
		if _messages.is_empty():
			return
		var layer := CanvasLayer.new()
		layer.layer = 6
		_screen = VBoxContainer.new()
		_screen.position = Vector2(10, 40)
		layer.add_child(_screen)
		add_child(layer)
	while _screen.get_child_count() < _messages.size():
		var l := Label.new()
		l.add_theme_color_override("font_shadow_color", Color.BLACK)
		l.add_theme_constant_override("shadow_offset_x", 1)
		l.add_theme_constant_override("shadow_offset_y", 1)
		_screen.add_child(l)
	for i in _screen.get_child_count():
		var l: Label = _screen.get_child(i)
		l.visible = i < _messages.size()
		if l.visible:
			l.text = _messages[i][0]
			l.add_theme_color_override("font_color", _messages[i][1])
	var keep: Array = []
	for it in _messages:
		it[2] -= dt
		if it[2] > 0.0:
			keep.append(it)
	_messages = keep
