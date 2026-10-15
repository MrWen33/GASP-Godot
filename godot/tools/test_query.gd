extends SceneTree
## Motion matching query dump: runs forward, then sprints, and prints the MM query vs. the selected pose's features per
## schema channel at fixed game times. Run with and without --fixed-fps to compare frame rate dependence.
## Env: QT = comma separated game times (s) to dump (default 4.2,4.4,4.6).

var main: Node
var ch
var t := 0.0
var times: Array = []
var schemas := {}
var dumped := {}

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	ch = main.get_node("Character")
	var mm: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/mm/mm.json"))
	schemas = mm["schemas"]
	var dbs: Dictionary = mm["databases"]
	for k in dbs:
		schemas[k] = schemas[dbs[k]["schema"]] if schemas.has(dbs[k]["schema"]) else null
	var qt := OS.get_environment("QT")
	for s in (qt if qt != "" else "4.2,4.4,4.6").split(","):
		times.append(float(s))

func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func _process(dt: float) -> bool:
	t += dt
	if t > 1.0 and not dumped.has("w"):
		dumped["w"] = true
		_key(KEY_W, true)
	if t > 4.0 and not dumped.has("shift"):
		dumped["shift"] = true
		_key(KEY_SHIFT, true)
	for qt in times:
		if t >= qt and not dumped.has(qt):
			dumped[qt] = true
			_dump(qt)
	if t > times.max() + 0.1:
		quit()
	return false

func _dump(qt: float) -> void:
	var st: Dictionary = ch.anim.get_state()
	print("t=%.2f dt=%.4f speed=%.0f db=%s anim=%s at %.2f cost=%.2f" % [qt, get_root().get_process_delta_time(), ch.Speed2D, st.database, st.anim, st.anim_time, st.cost])
	var sch = schemas.get(st.database)
	var q: PackedFloat32Array = st.query
	var p: PackedFloat32Array = st.pose_features
	if sch == null or q.is_empty():
		return
	for c in sch["channels"]:
		var o: int = c["offset"]
		var n: int = c["cardinality"]
		var qs := []
		var ps := []
		for i in n:
			qs.append("%.1f" % q[o + i])
			ps.append("%.1f" % p[o + i])
		var lab := "%s %s %s %.2f" % [c["type"], c.get("bone", ""), c.get("origin_bone", ""), c.get("sample_time_offset", 0.0)]
		print("   %-44s q=[%s] p=[%s]" % [lab, ", ".join(qs), ", ".join(ps)])
