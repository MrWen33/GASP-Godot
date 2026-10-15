extends Node3D
## Port of GASP's Gameplay Camera setup (UE 5.5 GameplayCameras plugin):
##  - CameraAsset_SandboxCharacter + CameraDirector_SandboxCharacter: picks one of 9 rigs from
##    CameraStyle (Far/Balanced/Close) x CameraMode (Freecam/Strafe/Aim), plus the Crouch_Offset rig.
##  - CameraRigPrefab_BasicThirdPersonBehavior: OffsetCameraNode(CrouchOffset) -> BoomArm(control rotation)
##    -> DampenPosition(critical dampers per camera axis) -> OffsetCameraNode(Offset, camera space) -> FieldOfView.
##  - Rig changes blend with SmoothBlend (SmootherStep): 0.2s into Aim rigs, 1.0s otherwise (shared transition).
##  - Persistent global rig CameraRig_CollisionOffset: CollisionPush (safe position = context + (0,15,55),
##    sphere 10cm, push damper 60, pull damper 10). Base rig: near plane 2cm.
## Camera state is computed in UE space (cm, UE axes) and converted to Godot at the end.

@export var target_path: NodePath

const RIGS := {
	"Far_Freecam":      {"fov": 80.0, "damp": Vector3(6, 6, 6),    "offset": Vector3(-375, 0, 20)},
	"Far_Strafe":       {"fov": 80.0, "damp": Vector3(10, 10, 10), "offset": Vector3(-300, 50, 20)},
	"Far_Aim":          {"fov": 70.0, "damp": Vector3(20, 20, 20), "offset": Vector3(-275, 60, 20), "enter_blend": 0.2},
	"Balanced_Freecam": {"fov": 80.0, "damp": Vector3(15, 15, 15), "offset": Vector3(-275, 20, 20)},
	"Balanced_Strafe":  {"fov": 80.0, "damp": Vector3(20, 20, 20), "offset": Vector3(-225, 60, 20)},
	"Balanced_Aim":     {"fov": 70.0, "damp": Vector3(30, 30, 30), "offset": Vector3(-200, 65, 25), "enter_blend": 0.2},
	"Close_Freecam":    {"fov": 80.0, "damp": Vector3(25, 25, 25), "offset": Vector3(-200, 50, 30)},
	"Close_Strafe":     {"fov": 80.0, "damp": Vector3(30, 30, 30), "offset": Vector3(-180, 60, 40)},
	"Close_Aim":        {"fov": 70.0, "damp": Vector3(50, 50, 50), "offset": Vector3(-175, 60, 40), "enter_blend": 0.2},
}
# Loose_Strafe_0 does not override LateralDampingFactor -> prefab default 10 (already 10 above)
const STYLE_NAMES := ["Far", "Balanced", "Close"]
const MODE_NAMES := ["Freecam", "Strafe", "Aim"]
const SHARED_BLEND_TIME := 1.0
const CROUCH_OFFSET := Vector3(0, 0, -50)
const GAMEPLAY_CAMERA_REL := Vector3(0, 0, 100) # GameplayCamera component, attached to the mesh (yaw +90)
const SAFE_POSITION_OFFSET := Vector3(0, 15, 55)
const COLLISION_RADIUS := 10.0
const PUSH_DAMPING := 60.0
const PULL_DAMPING := 10.0
const STABILIZATION := 0.00001

## FCriticalDamper
class CriticalDamper:
	var w0 := 0.0
	var x0 := 0.0
	var dx0 := 0.0
	func reset(x: float, dx: float) -> void:
		x0 = x; dx0 = dx
	func update(x: float, dt: float) -> float:
		var forced := x - x0
		if w0 > 0.0:
			var dv := forced / dt if forced != 0.0 else 0.0
			var p0 := 2.0 * dv / w0
			var p1 := -dv
			var e := exp(-w0 * dt)
			var a := x0 - p0
			var b := dx0 + w0 * a - p1
			var xt := (a + b * dt) * e + p0 + p1 * dt
			var dxt := (-w0 * a + b - w0 * b * dt) * e + p1
			x0 = xt + forced
			dx0 = dxt
			if absf(x0) <= STABILIZATION and absf(dx0) <= STABILIZATION:
				x0 = 0.0; dx0 = 0.0
		else:
			x0 = 0.0; dx0 = 0.0
		return x0

## One CameraRigAsset instance on the main layer blend stack
class RigEntry:
	var rig_name := ""
	var params: Dictionary
	var dampers := [CriticalDamper.new(), CriticalDamper.new(), CriticalDamper.new()]
	var prev_location := Vector3.ZERO
	var first := true
	var blend_time := 0.0
	var blend_elapsed := 0.0
	var crouch_weight := 0.0
	func blend_factor() -> float:
		if blend_time <= 0.0:
			return 1.0
		var t := clampf(blend_elapsed / blend_time, 0.0, 1.0)
		return t * t * t * (t * (t * 6.0 - 15.0) + 10.0) # SmootherStep

var target: Node3D
var cam: Camera3D
var stack: Array = []
var camera_style := 1 # CBP default CameraStyle = Balanced
var crouch_entry_weight := 0.0
var crouch_blend_elapsed := 0.0
var crouch_active := false
var crouch_start_weight := 0.0
# collision push state
var push_damper := CriticalDamper.new()
var pull_damper := CriticalDamper.new()
var push_first := true
var pull_first := true
var last_push := 0.0
var last_damped_push := 0.0
var last_pushing := true
var style_message := ""
var style_message_time := 0.0

func _ready() -> void:
	target = get_node(target_path)
	# the camera is driven per rendered frame from the interpolated character, so it must not be physics-interpolated itself
	physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	cam = Camera3D.new()
	cam.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	cam.keep_aspect = Camera3D.KEEP_WIDTH # UE FieldOfView is horizontal
	cam.near = 0.02
	cam.far = 4000.0
	add_child(cam)
	cam.current = true

func _unhandled_input(event: InputEvent) -> void:
	# IA: MouseScrollUp / Gamepad_DPad_Up -> closer style, MouseScrollDown / DPad_Down -> farther
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_set_style(camera_style + 1)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_set_style(camera_style - 1)
	elif event is InputEventJoypadButton and event.pressed:
		if event.button_index == JOY_BUTTON_DPAD_UP:
			_set_style(camera_style + 1)
		elif event.button_index == JOY_BUTTON_DPAD_DOWN:
			_set_style(camera_style - 1)

func _set_style(s: int) -> void:
	var ns := clampi(s, 0, 2)
	if ns == camera_style:
		return
	camera_style = ns
	style_message = "Camera Style: %s" % STYLE_NAMES[camera_style]
	style_message_time = 2.0

## Get_CharacterPropertiesForCamera + CameraDirector_SandboxCharacter
func _director() -> String:
	var mode := 0
	if target.wants_to_aim:
		mode = 2
	elif target.wants_to_strafe:
		mode = 1
	return "%s_%s" % [STYLE_NAMES[camera_style], MODE_NAMES[mode]]

func _activate(rig_name: String) -> void:
	if not stack.is_empty() and stack.back().rig_name == rig_name:
		return
	var e := RigEntry.new()
	e.rig_name = rig_name
	e.params = RIGS[rig_name]
	e.blend_time = 0.0 if stack.is_empty() else float(e.params.get("enter_blend", SHARED_BLEND_TIME))
	stack.append(e)

func _context_pose() -> Array:
	# GameplayCamera component world transform: mesh (actor + (0,0,-88) incl. crouch adjust, yaw -90) * (0,0,100) yaw +90
	var comp: Array = target._component_xform_render()
	var mesh_q: Quaternion = comp[0]
	var loc: Vector3 = comp[1] + mesh_q * GAMEPLAY_CAMERA_REL
	var rot: Quaternion = mesh_q * UE.quat_from_yaw(90.0)
	return [loc, rot]

## Runs one rig's node tree; returns [location, rotation, fov]
func _run_rig(e: RigEntry, ctx: Array, dt: float) -> Array:
	var rot: Quaternion = UE.quat_from_rotator(target.control_pitch, target.control_yaw, 0.0) # BoomArm: control rotation
	var loc: Vector3 = ctx[0]                                                               # BoomArm pivot (no boom offset)
	# DampenPositionCameraNode (CameraPose space)
	var damp: Vector3 = e.params["damp"]
	var axes := [rot * Vector3(1, 0, 0), rot * Vector3(0, 1, 0), rot * Vector3(0, 0, 1)]
	var w := [damp.x, damp.y, damp.z]
	var next_target := loc
	if not e.first:
		var lag := next_target - e.prev_location
		var damped := e.prev_location
		for i in 3:
			var d: CriticalDamper = e.dampers[i]
			d.w0 = w[i]
			var lag_d: float = lag.dot(axes[i])
			d.update(lag_d / 100.0, dt)
			damped += axes[i] * (lag_d - d.x0 * 100.0)
		loc = damped
	else:
		for i in 3:
			e.dampers[i].w0 = w[i]
	e.first = false
	e.prev_location = loc
	# OffsetCameraNode (camera space)
	loc += rot * (e.params["offset"] as Vector3)
	return [loc, rot, float(e.params["fov"])]

func _process(dt: float) -> void:
	if target == null or not target.has_method("_component_xform") or dt <= 0.0:
		return
	if still_active:
		_update_still_cam()
		return
	if not CVars.get_b("DDCVar.NewGameplayCameraSystem.Enable"):
		_legacy_spring_arm(dt)
		return
	legacy_first = true
	_activate(_director())
	var ctx := _context_pose()
	# evaluate the blend stack bottom -> top
	var result: Array = []
	for e in stack:
		var r: Array = _run_rig(e, ctx, dt)
		e.blend_elapsed += dt
		if result.is_empty():
			result = r
		else:
			var a: float = e.blend_factor()
			result = [result[0].lerp(r[0], a), (result[1] as Quaternion).slerp(r[1], a), lerpf(result[2], r[2], a)]
	# pop entries fully covered by a finished blend
	for i in range(stack.size() - 1, 0, -1):
		if stack[i].blend_factor() >= 1.0:
			stack = stack.slice(i)
			break
	# Crouch_Offset rig (Stance == Crouch), blended in/out with the shared SmootherStep transition
	var want_crouch: bool = target.is_crouched
	if want_crouch != crouch_active:
		crouch_active = want_crouch
		crouch_blend_elapsed = 0.0
		crouch_start_weight = crouch_entry_weight
	crouch_blend_elapsed += dt
	var t := clampf(crouch_blend_elapsed / SHARED_BLEND_TIME, 0.0, 1.0)
	var s := t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
	crouch_entry_weight = lerpf(crouch_start_weight, 1.0 if crouch_active else 0.0, s)
	var loc: Vector3 = result[0] + (result[1] as Quaternion) * (CROUCH_OFFSET * crouch_entry_weight)
	# Global persistent rig: CollisionPush
	var safe: Vector3 = ctx[0] + (ctx[1] as Quaternion) * SAFE_POSITION_OFFSET
	loc = _collision_push(safe, loc, dt)
	_apply(loc, result[1], result[2])
	if CVars.get_b("GameplayCameras.Debug.Enable"):
		_debug_text(result[2], loc)
	elif debug_label:
		debug_label.visible = false
	if style_message_time > 0.0:
		style_message_time -= dt

func _collision_push(safe: Vector3, cam_loc: Vector3, dt: float) -> Vector3:
	var found := false
	var push := 0.0
	var length := safe.distance_to(cam_loc)
	if length > 0.0:
		var sphere := SphereShape3D.new()
		sphere.radius = COLLISION_RADIUS * 0.01
		var params := PhysicsShapeQueryParameters3D.new()
		params.shape = sphere
		params.collision_mask = 1
		params.exclude = [target.get_rid()]
		params.transform = Transform3D(Basis(), UE.to_godot_pos(safe))
		params.motion = UE.to_godot_dir(cam_loc - safe) * 0.01
		var r := get_world_3d().direct_space_state.cast_motion(params)
		if r[1] < 1.0:
			found = true
			# hit location (sphere center at impact) -> push factor = distance(end, hit) / length
			var hit_center := safe.lerp(cam_loc, r[0])
			push = hit_center.distance_to(cam_loc) / length
	if not found and last_damped_push < 1e-8:
		last_pushing = true
		last_push = 0.0
		last_damped_push = 0.0
		return cam_loc
	var pushing := last_pushing
	if last_push < push:
		pushing = true
	elif last_push > push:
		pushing = false
	var damped := last_damped_push
	# TCriticalDamperValueInterpolator::OnRun
	var dist := absf(push - last_damped_push)
	var dirn := signf(push - last_damped_push)
	var d: CriticalDamper = push_damper if pushing else pull_damper
	if pushing and push_first:
		push_damper.w0 = PUSH_DAMPING; push_damper.reset(dist, 0.0); push_first = false
	elif not pushing and pull_first:
		pull_damper.w0 = PULL_DAMPING; pull_damper.reset(dist, 0.0); pull_first = false
	var next_dist := d.update(dist, dt)
	damped = last_damped_push + dirn * (dist - next_dist)
	if damped > 0.0:
		cam_loc = cam_loc + (safe - cam_loc) * damped
	last_push = push
	last_damped_push = damped
	last_pushing = pushing
	return cam_loc

func _apply(loc_ue: Vector3, rot_ue: Quaternion, fov: float) -> void:
	var fwd := UE.to_godot_dir(rot_ue * Vector3(1, 0, 0)).normalized()
	var up := UE.to_godot_dir(rot_ue * Vector3(0, 0, 1)).normalized()
	cam.global_transform = Transform3D(Basis.looking_at(fwd, up), UE.to_godot_pos(loc_ue))
	cam.fov = fov

# ------------------------------------------------------------------------------------------------
# DDCVar.NewGameplayCameraSystem.Enable = 0: CBP SetupCamera falls back to the character's SpringArm
# (TargetArmLength 300, +20cm, camera lag speed 10, collision test) + Camera (FOV 100).
const LEGACY_ARM_LENGTH := 300.0
const LEGACY_ARM_OFFSET := Vector3(0, 0, 20)
const LEGACY_LAG_SPEED := 10.0
const LEGACY_PROBE := 12.0
var legacy_first := true
var legacy_pivot := Vector3.ZERO

func _legacy_spring_arm(dt: float) -> void:
	var desired_pivot: Vector3 = target._render_location_ue() + LEGACY_ARM_OFFSET
	if legacy_first:
		legacy_pivot = desired_pivot
		legacy_first = false
	else:
		legacy_pivot = legacy_pivot.lerp(desired_pivot, clampf(dt * LEGACY_LAG_SPEED, 0.0, 1.0))
	var rot := UE.quat_from_rotator(target.control_pitch, target.control_yaw, 0.0)
	var arm_end := legacy_pivot - rot * Vector3(LEGACY_ARM_LENGTH, 0, 0)
	# probe sweep from the (unlagged) arm origin
	var sphere := SphereShape3D.new()
	sphere.radius = LEGACY_PROBE * 0.01
	var q := PhysicsShapeQueryParameters3D.new()
	q.shape = sphere
	q.collision_mask = 1
	q.exclude = [target.get_rid()]
	q.transform = Transform3D(Basis(), UE.to_godot_pos(desired_pivot))
	q.motion = UE.to_godot_dir(arm_end - desired_pivot) * 0.01
	var r := get_world_3d().direct_space_state.cast_motion(q)
	if r[1] < 1.0:
		arm_end = desired_pivot.lerp(arm_end, r[0])
	_apply(arm_end, rot, 100.0)

# ------------------------------------------------------------------------------------------------
# StillCam (Game Animation Widget "Still Cam" buttons): Enable spawns a static camera at the current view,
# Follow moves it with the target's translation, Rotate keeps it looking at the target.
var still_active := false
var still_follow := false
var still_look := false
var still_loc := Vector3.ZERO
var still_rot := Quaternion.IDENTITY
var still_last_target := Vector3.ZERO
var debug_label: Label

func still_cam(action: String) -> void:
	match action:
		"Enable":
			still_active = not still_active
			if still_active:
				still_loc = UE.from_godot_pos(cam.global_position)
				still_rot = UE.from_godot_quat(cam.global_basis.get_rotation_quaternion() * Quaternion(Vector3.UP, -PI / 2.0))
				still_last_target = target._render_location_ue()
		"Follow":
			still_follow = not still_follow
		"Rotate":
			still_look = not still_look

func _update_still_cam() -> void:
	var t: Vector3 = target._render_location_ue()
	if still_follow:
		still_loc += t - still_last_target
	still_last_target = t
	var fwd: Vector3
	if still_look:
		fwd = (t - still_loc).normalized()
		cam.global_transform = Transform3D(Basis.looking_at(UE.to_godot_dir(fwd), Vector3.UP), UE.to_godot_pos(still_loc))
	else:
		cam.global_position = UE.to_godot_pos(still_loc)

func _debug_text(fov: float, loc: Vector3) -> void:
	if debug_label == null:
		debug_label = Label.new()
		debug_label.add_theme_color_override("font_color", Color(0.6, 0.9, 1.0))
		debug_label.add_theme_color_override("font_outline_color", Color.BLACK)
		debug_label.add_theme_constant_override("outline_size", 4)
		debug_label.position = Vector2(12, 300)
		var layer := CanvasLayer.new()
		add_child(layer)
		layer.add_child(debug_label)
	debug_label.visible = true
	var lines := ["Gameplay Cameras Debug", "Camera Asset: CameraAsset_SandboxCharacter  Director: CameraDirector_SandboxCharacter",
		"Style: %s  FOV: %.1f  Location: %s" % [STYLE_NAMES[camera_style], fov, str(loc.round())], "Main layer blend stack:"]
	for e in stack:
		lines.append("  %s  blend %.2f" % [e.rig_name, e.blend_factor()])
	lines.append("Crouch_Offset weight %.2f   CollisionPush factor %.2f" % [crouch_entry_weight, last_damped_push])
	debug_label.text = "\n".join(lines)
