extends Node3D
## Builds DefaultLevel from data/level.json (exported from the UE editor by tools/ue/export_level.py):
## level blocks (M_Grid look), traversable blocks + ledge splines, landscape heightfield, text renders,
## teleporters, level buttons and the LevelVisuals style switching.

signal button_pressed(label: String)

const GRID_SHADER := preload("res://shaders/grid.gdshader")

# S_GridMaterialParams defaults
const DEF_GRID_COLOR := Color(1, 1, 1)
const DEF_SURFACE_COLOR := Color(0.121569, 0.121569, 0.121569)
const DEF_GRID_SIZES := Vector3(100, 200, 200)
const DEF_SPECULAR := 0.05

# LevelVisuals.LevelStyles (from DefaultLevel.t3d); StyleIndex = 2 in the level
var LEVEL_STYLES := [
	{"fog_color": Color(0.261349, 0.261177, 0.302083), "fog_density": 0.3, "blocks": {
		"Floor": {"grid": Color(0.3, 0.3, 0.3), "sizes": Vector3(100, 200, 800), "spec": 0.0},
		"Blocks": {"grid": Color(0.121569, 0.121569, 0.121569), "surface": Color(0.3, 0.3, 0.3)},
		"Blocks_Traversable": {"grid": Color(0.3, 0.3, 0.3)},
		"Landscape": {"grid": Color(0.3, 0.3, 0.3), "sizes": Vector3(100, 200, 800), "spec": 0.0}}},
	{"blocks": {
		"Floor": {"grid": Color(0.2, 0.2, 0.2), "surface": Color(0.510417, 0.510417, 0.510417), "sizes": Vector3(100, 200, 800)},
		"Blocks": {"grid": Color(0.2, 0.2, 0.2), "surface": Color(0.510417, 0.510417, 0.510417)},
		"Blocks_Traversable": {"grid": Color(0.5, 0.5, 0.5), "surface": Color(0.25, 0.25, 0.25)},
		"Landscape": {"grid": Color(0.2, 0.2, 0.2), "surface": Color(0.510417, 0.510417, 0.510417), "sizes": Vector3(100, 200, 800)}}},
	{"blocks": {
		"Floor": {"grid": Color(0.5, 0.5, 0.5), "surface": Color(0.258463, 0.236978, 0.541667), "sizes": Vector3(100, 200, 800)},
		"Blocks": {"grid": Color(0.177083, 0.177083, 0.177083), "surface": Color(0.510417, 0.510417, 0.510417)},
		"Blocks_Traversable": {"grid": Color(0.7, 0.7, 0.7), "surface": Color(0.85, 0.264066, 0.132812)},
		"Landscape": {"grid": Color(0.5, 0.5, 0.5), "surface": Color(0.24, 0.4, 0.24), "sizes": Vector3(100, 200, 800)}}},
]

var materials := {}         # color group -> ShaderMaterial
var traversable_blocks := [] # [{body: StaticBody3D, ledges: [[Vector3 UE, Vector3 UE], ...]}]
var player_start := {"pos": Vector3.ZERO, "yaw": 0.0}
var teleport_destinations := {}
var style_index := 2
var sun: DirectionalLight3D
var environment: Environment

func build(path := "res://data/level.json") -> void:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("level.json missing")
		return
	var d: Dictionary = JSON.parse_string(f.get_as_text())
	for g in ["Floor", "Blocks", "Blocks_Traversable", "Landscape"]:
		var m := ShaderMaterial.new()
		m.shader = GRID_SHADER
		materials[g] = m
	for a in d["actors"]:
		if a["class"] == "Teleporter_Destination_C":
			for c in a["components"]:
				if c["name"] == "TeleportPoint":
					teleport_destinations[a["name"]] = Vector3(c["xform"]["t"][0], c["xform"]["t"][1], c["xform"]["t"][2])
	for a in d["actors"]:
		match a["class"]:
			"LevelBlock_C", "LevelBlock_Traversable_C":
				_build_block(a)
			"Landscape":
				_build_landscape(a)
			"Teleporter_Sender_C", "Teleporter_Destination_C", "LevelButton_C":
				_build_prop(a)
			"PlayerStart":
				var t: Array = a["xform"]["t"]
				player_start = {"pos": Vector3(t[0], t[1], t[2]), "yaw": UE.quat_yaw(_q(a["xform"]["q"]))}
			"LevelVisuals_C":
				_build_visuals(a)
	apply_style(style_index)

static func _q(a: Array) -> Quaternion:
	return Quaternion(a[0], a[1], a[2], a[3]).normalized()

static func _v(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])

## UE component xform -> [Godot rotation basis, Godot origin, Godot-axis scale]
static func _gxf(x: Dictionary) -> Array:
	var s: Array = x["s"]
	return [Basis(UE.to_godot_quat(_q(x["q"]))), UE.to_godot_pos(_v(x["t"])), Vector3(s[0], s[2], s[1])]

func _mesh_kind(path) -> String:
	if path == null:
		return ""
	var p := String(path)
	if p.find("SM_Cube") >= 0: return "sm_cube"
	if p.find("SM_Cylinder") >= 0: return "sm_cylinder"
	if p.find("BasicShapes/Cube") >= 0: return "cube"
	if p.find("BasicShapes/Cylinder") >= 0: return "cylinder"
	return ""

## Adds a mesh (+ optional collision) for a prototype shape. Returns the StaticBody3D (or null).
func _add_shape(kind: String, xf: Dictionary, mat: Material, collide: bool, parent: Node3D) -> StaticBody3D:
	var g := _gxf(xf)
	var rot: Basis = g[0]; var origin: Vector3 = g[1]; var sc: Vector3 = g[2]
	var mesh: PrimitiveMesh
	var center := Vector3.ZERO # local center in unit (unscaled) shape space
	match kind:
		"sm_cube":
			mesh = BoxMesh.new(); (mesh as BoxMesh).size = Vector3.ONE; center = Vector3(0.5, 0.5, 0.5)
		"sm_cylinder":
			mesh = CylinderMesh.new(); (mesh as CylinderMesh).top_radius = 0.5; (mesh as CylinderMesh).bottom_radius = 0.5; (mesh as CylinderMesh).height = 1.0; (mesh as CylinderMesh).radial_segments = 48; center = Vector3(0, 0.5, 0)
		"cube":
			mesh = BoxMesh.new(); (mesh as BoxMesh).size = Vector3.ONE
		"cylinder":
			mesh = CylinderMesh.new(); (mesh as CylinderMesh).top_radius = 0.5; (mesh as CylinderMesh).bottom_radius = 0.5; (mesh as CylinderMesh).height = 1.0; (mesh as CylinderMesh).radial_segments = 48
		_:
			return null
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.transform = Transform3D(rot * Basis.from_scale(sc), origin) * Transform3D(Basis(), center)
	parent.add_child(mi)
	if not collide:
		return null
	var body := StaticBody3D.new()
	body.transform = Transform3D(rot, origin)
	var cs := CollisionShape3D.new()
	if kind.ends_with("cube"):
		var bs := BoxShape3D.new(); bs.size = sc.abs(); cs.shape = bs
	else:
		var cy := CylinderShape3D.new(); cy.radius = 0.5 * maxf(absf(sc.x), absf(sc.z)); cy.height = absf(sc.y); cs.shape = cy
	cs.position = center * sc
	body.add_child(cs)
	parent.add_child(body)
	return body

func _build_block(a: Dictionary) -> void:
	var group: String = a.get("props", {}).get("ColorGroup", "Blocks")
	var mat: ShaderMaterial = materials.get(group, materials["Blocks"])
	var body: StaticBody3D = null
	var ledges := []
	for c in a["components"]:
		if c["type"] == "StaticMeshComponent" and c.get("mesh") != null:
			var b := _add_shape(_mesh_kind(c["mesh"]), c["xform"], mat, true, self)
			if b:
				body = b
				body.set_meta("color_group", group)
		elif c["type"] == "SplineComponent":
			var pts: Array = c["points"]
			if pts.size() >= 2:
				ledges.append({"name": c["name"], "a": _v(pts[0]), "b": _v(pts[pts.size() - 1])})
		elif c["type"] == "TextRenderComponent":
			_add_text(c)
	if a["class"] == "LevelBlock_Traversable_C" and body:
		# Ledge_1..4, opposite pairs (1,2) (3,4); ledge spline up vector = outward horizontal normal
		ledges.sort_custom(func(x, y): return String(x["name"]) < String(y["name"]))
		var center := Vector3.ZERO
		for l in ledges:
			center += (l["a"] + l["b"]) * 0.5
		center /= maxf(1.0, ledges.size())
		for l in ledges:
			var dir: Vector3 = (l["b"] - l["a"]).normalized()
			var n := Vector3(-dir.y, dir.x, 0.0).normalized()
			var mid: Vector3 = (l["a"] + l["b"]) * 0.5
			if n.dot(mid - center) < 0.0:
				n = -n
			l["normal"] = n
		body.collision_layer = 1 | 2 # ECC_GameTraceChannel1 "Traversable" blocks
		body.set_meta("traversable", true)
		body.set_meta("ledges", ledges)
		traversable_blocks.append({"body": body, "ledges": ledges})

func _add_text(c: Dictionary) -> void:
	var txt := String(c.get("text", "")).replace("\r\n", "\n")
	if txt.strip_edges() == "":
		return
	var g := _gxf(c["xform"])
	var lbl := Label3D.new()
	lbl.text = txt
	var col: Array = c.get("color", [255, 255, 255, 255])
	lbl.modulate = Color8(int(col[0]), int(col[1]), int(col[2]), int(col[3]))
	var ws: float = c.get("world_size", 26.0)
	lbl.font_size = 64
	lbl.outline_size = 0
	lbl.pixel_size = ws * 0.01 / 64.0 * (g[2] as Vector3).y
	lbl.double_sided = false
	lbl.shaded = false
	var ha := String(c.get("halign", "")); var va := String(c.get("valign", ""))
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER if ha.find("CENTER") >= 0 else (HORIZONTAL_ALIGNMENT_RIGHT if ha.find("RIGHT") >= 0 else HORIZONTAL_ALIGNMENT_LEFT)
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER if va.find("CENTER") >= 0 else (VERTICAL_ALIGNMENT_BOTTOM if va.find("BOTTOM") >= 0 else VERTICAL_ALIGNMENT_TOP)
	# UE text renders in the component YZ plane, readable from +X (right = -Y, up = +Z)
	var face := Basis(Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0))
	lbl.transform = Transform3D((g[0] as Basis) * face, g[1])
	add_child(lbl)

func _build_landscape(a: Dictionary) -> void:
	var hf: Dictionary = a["heightfield"]
	var nx: int = hf["nx"]; var ny: int = hf["ny"]; var step: float = hf["step"]
	var x0: float = hf["x0"]; var y0: float = hf["y0"]
	var H: Array = hf["heights"]
	var data := PackedFloat32Array()
	data.resize(nx * ny)
	for j in ny:
		for i in nx:
			var h = H[j][i]
			data[j * nx + i] = (0.0 if h == null else float(h)) * 0.01
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var hs := HeightMapShape3D.new()
	hs.map_width = nx; hs.map_depth = ny; hs.map_data = data
	cs.shape = hs
	var center := UE.to_godot_pos(Vector3(x0 + (nx - 1) * step * 0.5, y0 + (ny - 1) * step * 0.5, 0.0))
	body.position = center
	cs.scale = Vector3(step * 0.01, 1.0, step * 0.01)
	body.add_child(cs)
	add_child(body)
	# render mesh
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for j in ny:
		for i in nx:
			st.add_vertex(UE.to_godot_pos(Vector3(x0 + i * step, y0 + j * step, data[j * nx + i] * 100.0)))
	for j in ny - 1:
		for i in nx - 1:
			var a0 := j * nx + i; var a1 := a0 + 1; var b0 := a0 + nx; var b1 := b0 + 1
			st.add_index(a0); st.add_index(a1); st.add_index(b0)
			st.add_index(a1); st.add_index(b1); st.add_index(b0)
	st.generate_normals()
	var mi := MeshInstance3D.new()
	mi.mesh = st.commit()
	mi.material_override = materials["Landscape"]
	add_child(mi)

func _plate_material(c: Dictionary) -> Material:
	var m := StandardMaterial3D.new()
	var mats: Array = c.get("materials", [])
	var col := Color(0.8, 0.8, 0.8)
	if mats.size() > 0 and mats[0] != null:
		var vp: Dictionary = mats[0].get("vectors", {})
		if vp.has("Base Color"):
			var v: Array = vp["Base Color"]
			col = Color(v[0], v[1], v[2])
	m.albedo_color = col
	m.emission_enabled = true
	m.emission = col * 0.25
	return m

func _build_prop(a: Dictionary) -> void:
	var root := Node3D.new()
	root.name = a["name"]
	add_child(root)
	for c in a["components"]:
		match c["type"]:
			"StaticMeshComponent":
				if c.get("mesh") != null:
					_add_shape(_mesh_kind(c["mesh"]), c["xform"], _plate_material(c), false, root)
			"TextRenderComponent":
				_add_text(c)
			"CapsuleComponent", "BoxComponent":
				var area := Area3D.new()
				var g := _gxf(c["xform"])
				area.transform = Transform3D(g[0], g[1])
				var cs := CollisionShape3D.new()
				if c["type"] == "CapsuleComponent":
					var cap := CapsuleShape3D.new(); cap.radius = c["radius"] * 0.01; cap.height = c["half_height"] * 2.0 * 0.01; cs.shape = cap
				else:
					var e: Array = c["extent"]
					var bs := BoxShape3D.new(); bs.size = Vector3(e[0], e[2], e[1]) * 0.02; cs.shape = bs
				area.add_child(cs)
				root.add_child(area)
				if a["class"] == "Teleporter_Sender_C":
					var dest_name: String = a.get("props", {}).get("Destination", "")
					area.body_entered.connect(_on_teleport.bind(dest_name))
				elif a["class"] == "LevelButton_C":
					var label := ""
					for cc in a["components"]:
						if cc["type"] == "TextRenderComponent":
							label = String(cc.get("text", "")).replace("\r\n", " ")
					area.body_entered.connect(_on_button.bind(label))

func _on_teleport(body: Node3D, dest_name: String) -> void:
	if not teleport_destinations.has(dest_name) or not body.has_method("level_teleport_to_ue"):
		return
	body.level_teleport_to_ue(teleport_destinations[dest_name])

func _on_button(body: Node3D, label: String) -> void:
	if not body.has_method("teleport_to_ue"):
		return
	if label.begins_with("Level Style"):
		if label.ends_with("Dark"): apply_style(0)
		elif label.ends_with("Light"): apply_style(1)
		elif label.ends_with("Colorful"): apply_style(2)
	button_pressed.emit(label)

func _build_visuals(a: Dictionary) -> void:
	for c in a["components"]:
		if c["type"] == "DirectionalLightComponent":
			sun = DirectionalLight3D.new()
			var g := _gxf(c["xform"])
			# UE lights shine along +X; Godot along -Z
			var dir_ue := _q(c["xform"]["q"]) * Vector3(1, 0, 0)
			var dir := UE.to_godot_dir(dir_ue).normalized()
			sun.transform = Transform3D(Basis.looking_at(dir, Vector3.UP if absf(dir.y) < 0.99 else Vector3.FORWARD), g[1])
			sun.light_energy = 1.2
			sun.shadow_enabled = true
			sun.directional_shadow_max_distance = 80.0
			add_child(sun)

func _params(group: String, style: Dictionary) -> Dictionary:
	var p: Dictionary = style["blocks"].get(group, {})
	return {"grid": p.get("grid", DEF_GRID_COLOR), "surface": p.get("surface", DEF_SURFACE_COLOR),
		"sizes": p.get("sizes", DEF_GRID_SIZES), "spec": p.get("spec", DEF_SPECULAR)}

func apply_style(idx: int) -> void:
	style_index = clampi(idx, 0, LEVEL_STYLES.size() - 1)
	var style: Dictionary = LEVEL_STYLES[style_index]
	for g in materials:
		var p := _params(g, style)
		var m: ShaderMaterial = materials[g]
		m.set_shader_parameter("surface_color", p["surface"])
		m.set_shader_parameter("grid_color", p["grid"])
		m.set_shader_parameter("grid_sizes", p["sizes"])
		m.set_shader_parameter("base_specular", p["spec"])
	if environment:
		var fc: Color = style.get("fog_color", Color(395.833374, 375.0, 500.0) / 500.0)
		environment.fog_light_color = Color(clampf(fc.r, 0, 1), clampf(fc.g, 0, 1), clampf(fc.b, 0, 1))
