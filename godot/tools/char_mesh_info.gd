extends SceneTree
## Prints skeleton / surface material names of the retargeted character FBX files.

const MESHES := ["res://assets/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.fbx",
	"res://assets/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple.fbx",
	"res://assets/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin.fbx",
	"res://assets/Characters/Echo/Meshes/SKM_Echo.fbx", "res://assets/Characters/Echo/Meshes/SKM_Echo_Hair.fbx",
	"res://assets/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero.fbx"]

func _init() -> void:
	for p in MESHES:
		var s: Node = load(p).instantiate()
		var sk: Skeleton3D = s.find_children("*", "Skeleton3D", true, false)[0]
		var line := "%s bones=%d root=%s xf=%s" % [p.get_file(), sk.get_bone_count(), sk.get_bone_name(0), str(sk.get_parent().transform)]
		print(line)
		var bad := 0
		for b in sk.get_bone_count():
			if sk.get_bone_parent(b) >= b:
				bad += 1
		print("  parent-order violations: ", bad, " pelvis rest=", sk.get_bone_rest(sk.find_bone("pelvis")).origin if sk.find_bone("pelvis") >= 0 else "none")
		for m in s.find_children("*", "MeshInstance3D", true, false):
			var names := []
			for i in m.mesh.get_surface_count():
				var mat: Material = m.mesh.surface_get_material(i)
				names.append(mat.resource_name if mat else "null")
			print("  mesh ", m.name, " surfaces ", names)
		s.free()
	quit()
