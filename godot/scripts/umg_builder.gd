class_name UMGBuilder
extends RefCounted
## Builds Godot Controls from the UMG widget trees in data/widgets.json (GASP editor utility widgets:
## ReadMe, Controls, GameAnimationWidget and their WidgetData helpers). Console buttons/spin boxes drive CVars.

const PT_TO_PX := 96.0 / 72.0
const TEXT_GRAY := Color(0.5, 0.5, 0.5)

var defaults: Dictionary
var pawn_override := ""  # GameAnimationWidget.PawnOverride (retargeted character name, "" = none)
var on_widget_button: Callable  # (name: String) for plain EditorUtilityButtons (Still Cam / Controller toggle)
var refreshers: Array = []  # [owner Control, Callable]; skipped once the owner is freed (its captures are gone)

func _init() -> void:
	var f := FileAccess.open("res://data/widget_defaults.json", FileAccess.READ)
	defaults = JSON.parse_string(f.get_as_text()) if f else {}

# ---------------------------------------------------------------- UE struct text helpers
static func fnum(s: String, key: String, def := 0.0) -> float:
	var re := RegEx.create_from_string("\\b" + key + "=(-?[0-9.]+)")
	var m := re.search(s)
	return m.get_string(1).to_float() if m else def

static func color(s: String, def := Color.WHITE) -> Color:
	if s == "" or not s.contains("R="):
		return def
	var c := Color(fnum(s, "R", def.r), fnum(s, "G", def.g), fnum(s, "B", def.b), fnum(s, "A", def.a))
	var a := c.a
	c = c.linear_to_srgb()
	c.a = a
	return c

static func margin(s: String) -> Array:
	return [fnum(s, "Left"), fnum(s, "Top"), fnum(s, "Right"), fnum(s, "Bottom")]

static func text_of(v) -> String:
	if v is Dictionary:
		return String(v.get("text", "")).replace("��", "'").replace("�", "'")
	return String(v).trim_prefix("\"").trim_suffix("\"")

static func ue_var(defaults_entry: Dictionary, props: Dictionary, key: String, def: String = "") -> String:
	if props.has(key):
		return text_of(props[key])
	return String(defaults_entry.get(key, def))

static func cdo_vec(s: String, def: Vector2) -> Vector2:
	# "<Struct 'Vector2D' ... {x: 100.000000, y: 25.000000}>" or "(X=..,Y=..)"
	var re := RegEx.create_from_string("[xX][:=] ?(-?[0-9.]+),? ?[yY][:=] ?(-?[0-9.]+)")
	var m := re.search(s)
	return Vector2(m.get_string(1).to_float(), m.get_string(2).to_float()) if m else def

static func cdo_color(s: String, def: Color) -> Color:
	if s.contains("R="):
		return color(s, def)
	var re := RegEx.create_from_string("r: ([0-9.]+), g: ([0-9.]+), b: ([0-9.]+), a: ([0-9.]+)")
	var m := re.search(s)
	if not m:
		return def
	var c := Color(m.get_string(1).to_float(), m.get_string(2).to_float(), m.get_string(3).to_float()).linear_to_srgb()
	c.a = m.get_string(4).to_float()
	return c

static func flat(bg: Color, pad := [0.0, 0.0, 0.0, 0.0]) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.content_margin_left = pad[0]
	sb.content_margin_top = pad[1]
	sb.content_margin_right = pad[2]
	sb.content_margin_bottom = pad[3]
	return sb

static func make_label(text: String, size_pt: float, col: Color, bold := false, italic := false) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", int(round(size_pt * PT_TO_PX)))
	l.add_theme_color_override("font_color", col)
	if bold or italic:
		var fv := FontVariation.new()
		fv.base_font = ThemeDB.fallback_font
		if bold:
			fv.variation_embolden = 0.8
		if italic:
			fv.variation_transform = Transform2D(Vector2(1, 0), Vector2(0.2, 1), Vector2.ZERO)
		l.add_theme_font_override("font", fv)
	return l

# ---------------------------------------------------------------- tree builder
func build(node: Dictionary) -> Control:
	var c := _build_node(node)
	_apply_slot(c, node.get("slot", {}))
	return _wrap_padding(c, node.get("slot", {}))

func _wrap_padding(c: Control, slot: Dictionary) -> Control:
	var pad_s: String = slot.get("Padding", "")
	if pad_s == "" or slot.get("class", "") in ["SizeBoxSlot", "ButtonSlot", "BorderSlot", "HeaderContent", "BodyContent"]:
		return c
	var m := margin(pad_s)
	if m == [0.0, 0.0, 0.0, 0.0]:
		return c
	var mc := MarginContainer.new()
	mc.add_theme_constant_override("margin_left", int(m[0]))
	mc.add_theme_constant_override("margin_top", int(m[1]))
	mc.add_theme_constant_override("margin_right", int(m[2]))
	mc.add_theme_constant_override("margin_bottom", int(m[3]))
	mc.size_flags_horizontal = c.size_flags_horizontal
	mc.size_flags_vertical = c.size_flags_vertical
	mc.add_child(c)
	return mc

func _apply_slot(c: Control, slot: Dictionary) -> void:
	var cls: String = slot.get("class", "")
	var size_s: String = slot.get("Size", "")
	if cls == "HorizontalBoxSlot":
		c.size_flags_horizontal = Control.SIZE_EXPAND_FILL if size_s.contains("Fill") else Control.SIZE_FILL
		c.size_flags_vertical = _valign(slot.get("VerticalAlignment", "VAlign_Fill"))
	elif cls == "VerticalBoxSlot" or cls == "ScrollBoxSlot":
		c.size_flags_vertical = Control.SIZE_EXPAND_FILL if size_s.contains("Fill") else Control.SIZE_FILL
		c.size_flags_horizontal = _halign(slot.get("HorizontalAlignment", "HAlign_Fill"))
	elif cls == "OverlaySlot" or cls == "BorderSlot":
		c.size_flags_horizontal = _halign(slot.get("HorizontalAlignment", "HAlign_Fill" if cls == "BorderSlot" else "HAlign_Left"))
		c.size_flags_vertical = _valign(slot.get("VerticalAlignment", "VAlign_Fill" if cls == "BorderSlot" else "VAlign_Top"))

static func _halign(s: String) -> int:
	match s:
		"HAlign_Left": return Control.SIZE_SHRINK_BEGIN
		"HAlign_Center": return Control.SIZE_SHRINK_CENTER
		"HAlign_Right": return Control.SIZE_SHRINK_END
	return Control.SIZE_FILL

static func _valign(s: String) -> int:
	match s:
		"VAlign_Top": return Control.SIZE_SHRINK_BEGIN
		"VAlign_Center": return Control.SIZE_SHRINK_CENTER
		"VAlign_Bottom": return Control.SIZE_SHRINK_END
	return Control.SIZE_FILL

func _children_into(parent: Control, node: Dictionary) -> void:
	for ch in node["children"]:
		parent.add_child(build(ch))

func _build_node(n: Dictionary) -> Control:
	var p: Dictionary = n["props"]
	match n["class"]:
		"Border":
			var pc := PanelContainer.new()
			pc.add_theme_stylebox_override("panel", flat(color(p.get("BrushColor", ""), Color.WHITE), margin(p.get("Padding", "(Left=4,Top=2,Right=4,Bottom=2)"))))
			_children_into(pc, n)
			return pc
		"EditorUtilityScrollBox", "ScrollBox":
			var sc := ScrollContainer.new()
			sc.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
			sc.size_flags_vertical = Control.SIZE_EXPAND_FILL
			var vb := VBoxContainer.new()
			vb.add_theme_constant_override("separation", 0)
			vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			sc.add_child(vb)
			_children_into(vb, n)
			return sc
		"VerticalBox":
			var v := VBoxContainer.new()
			v.add_theme_constant_override("separation", 0)
			_children_into(v, n)
			return v
		"HorizontalBox":
			var h := HBoxContainer.new()
			h.add_theme_constant_override("separation", 0)
			_children_into(h, n)
			return h
		"Overlay":
			var o := MarginContainer.new()
			_children_into(o, n)
			return o
		"WrapBox":
			var fc := HFlowContainer.new()
			var ip := Vector2(fnum(p.get("InnerSlotPadding", ""), "X"), fnum(p.get("InnerSlotPadding", ""), "Y"))
			fc.add_theme_constant_override("h_separation", int(ip.x))
			fc.add_theme_constant_override("v_separation", int(ip.y))
			_children_into(fc, n)
			return fc
		"SizeBox":
			var sb := MarginContainer.new()
			sb.custom_minimum_size = Vector2(float(p.get("WidthOverride", "0")), float(p.get("HeightOverride", "0")))
			sb.clip_contents = true
			_children_into(sb, n)
			return sb
		"TextBlock":
			var font: String = p.get("Font", "")
			var l := make_label(text_of(p.get("Text", "")), fnum(font, "Size", 10.0), color(p.get("ColorAndOpacity", ""), Color.WHITE), font.contains("Bold"), font.contains("Italic"))
			if p.get("AutoWrapText", "") == "True":
				l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
				l.custom_minimum_size.x = 100
			match p.get("Justification", ""):
				"Center": l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
				"Right": l.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
			var mg := margin(p.get("Margin", ""))
			if mg != [0.0, 0.0, 0.0, 0.0]:
				var mc := MarginContainer.new()
				mc.add_theme_constant_override("margin_left", int(mg[0]))
				mc.add_theme_constant_override("margin_top", int(mg[1]))
				mc.add_theme_constant_override("margin_right", int(mg[2]))
				mc.add_theme_constant_override("margin_bottom", int(mg[3]))
				mc.add_child(l)
				l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
				return mc
			return l
		"WB_SimpleTextBlock_C":
			return _simple_text_block(p)
		"EUW_SimpleConsoleButton_C":
			return _console_button(p)
		"EUW_SimpleConsoleSpinBox_C":
			return _console_spinbox(p)
		"EditorUtilityButton", "Button":
			var b := Button.new()
			b.custom_minimum_size = Vector2(75, 25)
			b.add_theme_font_size_override("font_size", int(8 * PT_TO_PX))
			b.add_theme_stylebox_override("normal", flat(Color(0.14, 0.14, 0.14)))
			b.add_theme_stylebox_override("hover", flat(Color(0.25, 0.25, 0.25)))
			b.add_theme_stylebox_override("pressed", flat(Color(0.35, 0.35, 0.35)))
			for ch in n["children"]:
				if ch["class"] == "TextBlock":
					b.text = text_of(ch["props"].get("Text", ""))
			var nm: String = n["name"]
			b.pressed.connect(func(): if on_widget_button.is_valid(): on_widget_button.call(nm))
			return b
		"EditorUtilityExpandableArea":
			return _expandable(n)
		"EUW_CharacterSelectButton_C":
			# MetaHuman pawns are outside this port (motion matching / locomotion animation only): hidden entries are skipped by containers
			if String(p.get("Object", "")).contains("Metahuman"):
				var skipped := Control.new()
				skipped.visible = false
				return skipped
			return _character_button(p)
	var fallback := Control.new()
	fallback.name = String(n["class"])
	return fallback

func _simple_text_block(p: Dictionary) -> Control:
	var d: Dictionary = defaults.get("WB_SimpleTextBlock", {})
	var size := cdo_vec(String(p.get("BoxSize", d.get("BoxSize", ""))), Vector2(100, 25))
	var box := PanelContainer.new()
	box.custom_minimum_size = size
	box.clip_contents = true
	var pad := margin(String(p.get("TextPadding", ""))) if p.has("TextPadding") else [0.0, 0.0, 0.0, 0.0]
	box.add_theme_stylebox_override("panel", flat(cdo_color(String(p.get("BoxColor", d.get("BoxColor", ""))), Color(0.13, 0.13, 0.13)), pad))
	var l := make_label(text_of(p.get("Text", d.get("Text", ""))), 8.0, TEXT_GRAY, p.get("IsBold", "False") == "True")
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	match String(p.get("TextAlignment", "HAlign_Left")):
		"HAlign_Center": l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		"HAlign_Right": l.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	box.add_child(l)
	return box

func _console_button(p: Dictionary) -> Control:
	var d: Dictionary = defaults.get("EUW_SimpleConsoleButton", {})
	var cmd := ue_var(d, p, "Console Command")
	var int_state := ue_var(d, p, "Command Has Int State", "False") == "True"
	var int_value := int(ue_var(d, p, "IntValue", "1"))
	var invert := ue_var(d, p, "Invert Color", "False") == "True"
	var b := Button.new()
	b.text = ue_var(d, p, "ButtonName")
	b.custom_minimum_size = Vector2(float(ue_var(d, p, "Button Width", "75")), 25)
	b.add_theme_font_size_override("font_size", int(8 * PT_TO_PX))
	b.add_theme_stylebox_override("normal", flat(color("(R=0.017642,G=0.017642,B=0.017642,A=1.0)")))
	b.add_theme_stylebox_override("hover", flat(Color(0.22, 0.22, 0.22)))
	b.add_theme_stylebox_override("pressed", flat(Color(0.3, 0.3, 0.3)))
	b.tooltip_text = cmd
	var cmd_name := cmd.split(" ", false)[0] if cmd != "" else ""
	var is_toggle := cmd.to_lower().begins_with("stat ") or cmd.to_lower().begins_with("show ")
	b.pressed.connect(func():
		if int_state:
			# EUW_SimpleConsoleButton: toggles the cvar between 0 and IntValue
			CVars.set_value(cmd_name, 0.0 if CVars.get_i(cmd_name) != 0 else float(int_value))
		else:
			CVars.exec(cmd))
	var refresh := func() -> bool:
		if not is_instance_valid(b):
			return false
		var col := Color(0.8, 0.8, 0.8)
		if int_state or is_toggle:
			var on := CVars.get_b(cmd if is_toggle else cmd_name)
			if invert:
				on = not on
			col = Color(0.333, 1.0, 0.0) if on else Color(1.0, 0.149, 0.149)
		else:
			var parts := cmd.split(" ", false)
			if parts.size() >= 2 and parts[1].is_valid_float() and CVars.has(parts[0]) and is_equal_approx(CVars.get_f(parts[0]), parts[1].to_float()):
				col = Color(1.0, 1.0, 0.149)
		b.add_theme_color_override("font_color", col)
		b.add_theme_color_override("font_hover_color", col)
		return true
	refreshers.append([b, refresh])
	refresh.call()
	return b

func _console_spinbox(p: Dictionary) -> Control:
	var d: Dictionary = defaults.get("EUW_SimpleConsoleSpinBox", {})
	var cmd := ue_var(d, p, "Console Command")
	var sb := SpinBox.new()
	sb.custom_minimum_size = Vector2(float(ue_var(d, p, "Box Width", "75")), 25)
	sb.allow_greater = ue_var(d, p, "Use Max", "False") != "True"
	sb.allow_lesser = ue_var(d, p, "Use Min", "False") != "True"
	sb.min_value = float(ue_var(d, p, "Min Value", "0")) if not sb.allow_lesser else -100000.0
	sb.max_value = float(ue_var(d, p, "Max Value", "0")) if not sb.allow_greater else 100000.0
	var snap := float(ue_var(d, p, "SliderSnapDelta", "0"))
	sb.step = snap if snap > 0.0 else 0.01
	sb.tooltip_text = text_of(p.get("ToolTipText", cmd))
	sb.get_line_edit().add_theme_font_size_override("font_size", int(8 * PT_TO_PX))
	sb.set_value_no_signal(CVars.get_f(cmd))
	var update := ue_var(d, p, "UpdateValueToMatchConsoleVariable", "False") == "True"
	sb.value_changed.connect(func(v: float): CVars.set_value(cmd, v))
	refreshers.append([sb, func() -> bool:
		if not is_instance_valid(sb):
			return false
		if update and not sb.get_line_edit().has_focus():
			sb.set_value_no_signal(CVars.get_f(cmd))
		return true])
	return sb

func _expandable(n: Dictionary) -> Control:
	var p: Dictionary = n["props"]
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 0)
	var header: Control = null
	var body: Control = null
	for ch in n["children"]:
		if ch.get("slot", {}).get("class", "") == "HeaderContent":
			header = build(ch)
		else:
			body = build(ch)
	var hb := Button.new()
	hb.flat = true
	hb.alignment = HORIZONTAL_ALIGNMENT_LEFT
	hb.add_theme_stylebox_override("normal", flat(color(p.get("BorderBrush", ""), Color(0.18, 0.18, 0.18)), [3, 0, 0, 0]))
	hb.add_theme_stylebox_override("hover", flat(Color(0.25, 0.25, 0.25), [3, 0, 0, 0]))
	hb.custom_minimum_size.y = 25
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var arrow := make_label("v", 8.0, TEXT_GRAY)
	arrow.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(arrow)
	if header:
		header.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(header)
	row.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	hb.add_child(row)
	v.add_child(hb)
	if body:
		v.add_child(body)
		body.visible = p.get("bIsExpanded", "False") == "True"
		arrow.text = "v" if body.visible else ">"
		hb.pressed.connect(func():
			body.visible = not body.visible
			arrow.text = "v" if body.visible else ">")
	return v

func _character_button(p: Dictionary) -> Control:
	var obj := String(p.get("Object", ""))
	var label := "Default"
	var m := RegEx.create_from_string("CBP_SandboxCharacter_([A-Za-z0-9_]+)\\.").search(obj)
	if m:
		label = m.get_string(1).replace("_", " ")
	var b := Button.new()
	b.text = label
	b.custom_minimum_size = Vector2(75, 75)
	b.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	b.add_theme_font_size_override("font_size", int(8 * PT_TO_PX))
	b.add_theme_stylebox_override("normal", flat(Color(0.1, 0.1, 0.1)))
	b.add_theme_stylebox_override("hover", flat(Color(0.2, 0.2, 0.2)))
	b.tooltip_text = obj if obj != "" else "Game mode default pawn (CBP_SandboxCharacter)"
	var key := m.get_string(1) if m else ""
	# EUW_CharacterSelectButton: a different pawn -> PawnOverride = Pawn + SpawnCharacter, the active one -> clear PawnOverride
	b.pressed.connect(func():
		if key != pawn_override:
			pawn_override = key
			if on_widget_button.is_valid():
				on_widget_button.call("SpawnCharacter:" + key)
		else:
			pawn_override = "")
	# GetBorderColor: orange border while this pawn is the PawnOverride
	var border := StyleBoxFlat.new()
	border.bg_color = Color(0, 0, 0, 0)
	border.set_border_width_all(3)
	border.draw_center = false
	var frame := Panel.new()
	frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	frame.set_anchors_preset(Control.PRESET_FULL_RECT)
	frame.add_theme_stylebox_override("panel", border)
	b.add_child(frame)
	refreshers.append([b, func() -> bool:
		if not is_instance_valid(b):
			return false
		border.border_color = Color(0.831373, 0.360784, 0.003922, 1.0) if key == pawn_override and key != "" else Color(0, 0, 0, 0)
		return true])
	return b

func refresh_all() -> void:
	var alive: Array = []
	for r in refreshers:
		if is_instance_valid(r[0]) and not (r[0] as Node).is_queued_for_deletion() and r[1].call():
			alive.append(r)
	refreshers = alive
