extends SceneTree
## Foley test: walk, run, sprint, jump; print which foley events were played and when.

var frame := 0
var main: Node
var ch

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
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
		30: _key(KEY_W, true)
		200: _key(KEY_SPACE, true)
		203: _key(KEY_SPACE, false)
		300: _key(KEY_W, false)
		400:
			var counts := {}
			for e in ch.foley.played_log:
				counts[e[1]] = counts.get(e[1], 0) + 1
			print("foley events: ", counts)
			var t0: float = ch.foley.played_log[0][0] if ch.foley.played_log.size() > 0 else 0.0
			var s := ""
			for e in ch.foley.played_log:
				s += "%s@%.2f(%.2f) " % [e[1], e[0] - t0, e[2]]
			print(s)
			print("ambient players: ", main.get_children().filter(func(n): return n is AudioStreamPlayer).map(func(n): return [n.name, n.playing]))
			quit()
	return false
