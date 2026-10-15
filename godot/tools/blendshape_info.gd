extends SceneTree
## Blend shape names of a skinned FBX (user arg: res path).
func _init() -> void:
	var s: Node = load(OS.get_cmdline_user_args()[0]).instantiate()
	for m in s.find_children("*", "MeshInstance3D", true, false):
		var names := []
		for i in (m.mesh as ArrayMesh).get_blend_shape_count():
			names.append((m.mesh as ArrayMesh).get_blend_shape_name(i))
		print(m.name, " blend shapes ", names.size(), " ", names.slice(0, 40))
	s.free()
	quit()
