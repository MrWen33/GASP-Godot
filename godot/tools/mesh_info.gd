extends SceneTree
func _init() -> void:
	for p in ["res://assets/LevelPrototyping/Meshes/SM_Cube.fbx", "res://assets/LevelPrototyping/Meshes/SM_Cylinder.fbx"]:
		var s = load(p).instantiate()
		for m in s.find_children("*", "MeshInstance3D", true, false):
			print(p, " ", m.name, " aabb=", m.get_aabb(), " xform=", m.transform, " parent=", m.get_parent().transform, " surfaces=", m.mesh.get_surface_count(), " mat=", m.mesh.surface_get_material(0))
	quit()
