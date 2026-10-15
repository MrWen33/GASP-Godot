extends SceneTree
# Dumps the imported mannequin skeleton (names, parents, rest) to JSON for pipeline checks.
func _init():
	var ps: PackedScene = load("res://assets/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.fbx")
	var root = ps.instantiate()
	var sk: Skeleton3D = root.find_children("*", "Skeleton3D", true, false)[0]
	var out = {"bones": [], "path": str(root.get_path_to(sk)), "skel_global": str(sk.transform)}
	var n = sk
	var chain = []
	while n != null:
		chain.append([n.name, str(n.transform)])
		n = n.get_parent()
	out["chain"] = chain
	for i in sk.get_bone_count():
		var r := sk.get_bone_rest(i)
		var g := sk.get_bone_global_rest(i)
		out["bones"].append({"name": sk.get_bone_name(i), "parent": sk.get_bone_parent(i),
			"rest_o": [r.origin.x, r.origin.y, r.origin.z], "rest_q": [r.basis.get_rotation_quaternion().x, r.basis.get_rotation_quaternion().y, r.basis.get_rotation_quaternion().z, r.basis.get_rotation_quaternion().w],
			"rest_s": [r.basis.get_scale().x, r.basis.get_scale().y, r.basis.get_scale().z],
			"glob_o": [g.origin.x, g.origin.y, g.origin.z]})
	var meshes = []
	for m in root.find_children("*", "MeshInstance3D", true, false):
		meshes.append([m.name, str(root.get_path_to(m)), m.mesh.get_surface_count() if m.mesh else 0])
	out["meshes"] = meshes
	var f = FileAccess.open("res://data/godot_skeleton.json", FileAccess.WRITE)
	f.store_string(JSON.stringify(out, "  "))
	f.close()
	print("DUMP OK ", sk.get_bone_count())
	quit()
