extends SceneTree
var frame := 0
var ch
var masks := [0, 1, 2, 4, 8, 16, 32, 63]
func _init() -> void:
	var main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	ch = main.get_node("Character")
func _process(_dt: float) -> bool:
	frame += 1
	var mi := (frame - 60) / 40
	if frame >= 60 and mi < masks.size():
		ch.debug_disable = masks[mi]
		if (frame - 60) % 40 == 39:
			var hx: Transform3D = ch.anim.get_bone_cs_ue("head")
			var px: Transform3D = ch.anim.get_bone_cs_ue("pelvis")
			var st: Dictionary = ch.anim.get_state()
			print("mask=%d head=%s pelvis=%s anim=%s t=%.2f" % [masks[mi], str(hx.origin), str(px.origin), st.anim, st.anim_time])
	if frame > 60 + 40 * masks.size():
		quit()
	return false
