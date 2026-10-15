extends SceneTree
## Character Select buttons of the Game Animation Widget: press Echo, press it again (clears PawnOverride, no respawn),
## then Default.

var frame := 0
var main: Node

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)

func _button(text: String) -> Button:
	for b in main.get_node("GaspUI").find_children("*", "Button", true, false):
		if b.text == text:
			return b
	return null

func _key(code: Key) -> void:
	for p in [true, false]:
		var e := InputEventKey.new()
		e.physical_keycode = code
		e.keycode = code
		e.pressed = p
		Input.parse_input_event(e)

func _state(tag: String) -> void:
	var ch = main.get_node("Character")
	var ui = main.get_node("GaspUI")
	var s := "%s: character=%s id=%d override='%s'" % [tag, ch.character_variant, ch.get_instance_id(), ui.builder.pawn_override]
	if ch.visual:
		s += " parts=%d" % ch.visual.parts.size()
	print(s)

func _process(_dt: float) -> bool:
	frame += 1
	match frame:
		5:
			main.get_node("GaspUI").open_panel("Game Animation Widget")
		10:
			var names := []
			for b in main.get_node("GaspUI").find_children("*", "Button", true, false):
				if b.custom_minimum_size == Vector2(75, 75):
					names.append("%s%s" % [b.text, " (disabled)" if b.disabled else ""])
			print("character buttons: ", names)
			_state("start")
			_button("Echo").pressed.emit()
		30:
			_state("after Echo")
			_button("Echo").pressed.emit()
		40:
			_state("after Echo again")
			_button("Default").pressed.emit()
		110:
			_state("after Default")
			print("default mesh visible: ", main.get_node("Character").mesh_root.visible)
			return true
	return false
