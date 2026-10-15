extends CharacterBody3D
## CBP_SandboxCharacter + CharacterMovementComponent (walking/falling) + ABP_SandboxCharacter logic.
## Gameplay state is kept in UE space (cm, degrees, UE axes); Godot physics does the collision.

enum EGait { WALK, RUN, SPRINT }
enum EMovementMode { ON_GROUND, IN_AIR }
enum EMovementState { IDLE, MOVING }
enum ERotationMode { ORIENT_TO_MOVEMENT, STRAFE }
enum EStance { STAND, CROUCH }

const CAPSULE_RADIUS := 30.0
const CAPSULE_HALF_HEIGHT := 86.0
## CharacterMovementComponent keeps a walking capsule floating between MIN_FLOOR_DIST (1.9) and MAX_FLOOR_DIST (2.4)
## above the floor (measured in GASP PIE: capsule centre = floor + 86 + 2.2). Emulated by lowering the collision shape
## relative to the body origin, so the actor location, mesh and foot traces sit where UE's do.
const FLOOR_HOVER := 2.15
const CROUCHED_HALF_HEIGHT := 60.0
const MESH_REL_Z := -88.0
const MESH_REL_YAW := -90.0
const GRAVITY_Z := -980.0
const JUMP_Z := 500.0
const AIR_CONTROL := 0.25
const AIR_CONTROL_BOOST_MULTIPLIER := 2.0
const AIR_CONTROL_BOOST_VELOCITY_THRESHOLD := 25.0
const MIN_ANALOG_WALK_SPEED := 150.0
const TERMINAL_VELOCITY := 4000.0
const BRAKING_FRICTION_FACTOR := 1.0
const BRAKING_FRICTION := 0.0 # bUseSeparateBrakingFriction=True, BrakingFriction default 0
const BRAKING_SUB_STEP_TIME := 1.0 / 33.0
const WALK_SPEEDS := Vector3(200, 180, 150)
const RUN_SPEEDS := Vector3(500, 350, 300)
const SPRINT_SPEEDS := Vector3(700, 700, 700)
const CROUCH_SPEEDS := Vector3(225, 200, 180)
const STRAFE_SPEED_MAP := [[0.0, 0.0], [45.0, 0.0], [80.0, 1.0], [100.0, 1.0], [135.0, 2.0], [180.0, 2.0]]
const HEAVY_LAND_SPEED_THRESHOLD := 700.0
const MOUSE_SENSITIVITY := 0.175 # Mouse2D Sensitivity 0.07 * legacy InputYawScale 2.5

## Retargeted character (CBP_SandboxCharacter_<name>); empty = CBP_SandboxCharacter (UEFN mannequin visible)
@export var character_variant := ""
@export var mesh_scene: PackedScene = preload("res://assets/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.fbx")
@export var camera_path: NodePath

# --- input state (CharacterInputState) ---
var wants_to_sprint := false
var wants_to_walk := false
var wants_to_strafe := true
var wants_to_aim := false
var move_input := Vector2.ZERO
var control_yaw := 0.0
var control_pitch := 0.0

# --- CMC state (UE space) ---
var actor_yaw := 0.0
var ue_velocity := Vector3.ZERO
var ue_acceleration := Vector3.ZERO
var max_acceleration := 800.0
var braking_deceleration_walking := 1500.0
var ground_friction := 5.0
var max_walk_speed := 500.0
var max_walk_speed_crouched := 225.0
var is_falling := false
var is_crouched := false
var use_controller_desired_rotation := true
var orient_rotation_to_movement := false
var gait: int = EGait.RUN
var just_landed := false
var just_landed_timer := 0.0
var land_velocity := Vector3.ZERO

# --- ABP state ---
var anim: GaspAnimator
var chooser := ChooserTable.new()        # CHT_PoseSearchDatabases_Dense
var chooser_sparse := ChooserTable.new() # CHT_PoseSearchDatabases_Sparse
var traj := GaspTrajectory.new()
var Trajectory := traj
var TrajectoryCollision := {"TimeToLand": 0.0, "LandSpeed": 0.0}
var trj_past_velocity := Vector3.ZERO
var trj_current_velocity := Vector3.ZERO
var trj_future_velocity := Vector3.ZERO
var character_xform_q := Quaternion.IDENTITY
var character_xform_q_last := Quaternion.IDENTITY
var root_xform_q := Quaternion.IDENTITY
var acceleration := Vector3.ZERO
var acceleration_amount := 0.0
var has_acceleration := false
var velocity_last := Vector3.ZERO
var abp_velocity := Vector3.ZERO
var Speed2D := 0.0
var has_velocity := false
var velocity_acceleration := Vector3.ZERO
var last_non_zero_velocity := Vector3.ZERO
var MovementMode: int = EMovementMode.ON_GROUND
var movement_mode_last: int = EMovementMode.ON_GROUND
var RotationMode: int = ERotationMode.STRAFE
var rotation_mode_last: int = ERotationMode.STRAFE
var MovementState: int = EMovementState.IDLE
var movement_state_last: int = EMovementState.IDLE
var Gait: int = EGait.RUN
var gait_last: int = EGait.RUN
var Stance: int = EStance.STAND
var stance_last: int = EStance.STAND
var current_database_tags := PackedStringArray()
## Mesh component tags ("Force SM Setup" / "Force MM Setup", CBP_SandboxCharacter_SetupComparison)
var component_tags: Array = []
var use_experimental_state_machine := false
var sm: ExperimentalStateMachine
var slot_weight := 0.0
var debug_disable := 0

# chooser bindings (read through ChooserTable via get())
var IsStarting := false
var IsPivoting := false
var ShouldTurnInPlace := false
var ShouldSpinTransition := false
var JustLanded_Light := false
var JustLanded_Heavy := false
var JustTraversed := false

# traversal state
var traversal_chooser := ChooserTable.new()
var doing_traversal := false
var foley: FoleyAudio
var always_show_debug_text := false
var montage_playing := false
var is_flying := false
var traversal_result := {}
var last_traversal_check := {}
var animated_dist_front_to_back_ledge := 0.0
var animated_dist_front_to_back_floor := 0.0
var has_interaction := false
var interaction_pos_ue := Vector3.ZERO
var interaction_rot_ue := Quaternion.IDENTITY

var mesh_root: Node3D
var skeleton: Skeleton3D
var visual: CharacterVisual
var collision: CollisionShape3D
var debug_label: Label
var first_update := true

func _ready() -> void:
	foley = FoleyAudio.new()
	foley.name = "Foley"
	add_child(foley)
	collision = CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = CAPSULE_RADIUS * 0.01
	cap.height = CAPSULE_HALF_HEIGHT * 2.0 * 0.01
	collision.shape = cap
	collision.position.y = -FLOOR_HOVER * 0.01
	add_child(collision)
	floor_snap_length = 0.45 # MaxStepHeight 45
	floor_max_angle = deg_to_rad(44.765) # WalkableFloorAngle
	up_direction = Vector3.UP

	mesh_root = mesh_scene.instantiate()
	mesh_root.top_level = true
	add_child(mesh_root)
	skeleton = mesh_root.find_children("*", "Skeleton3D", true, false)[0]
	if character_variant != "" and CharacterVisual.is_retargeted(character_variant):
		# the UEFN mannequin (Mesh) stays animated but hidden; visible meshes are retargeted from it
		mesh_root.visible = character_variant in CharacterVisual.KEEP_SOURCE_VISIBLE
		visual = CharacterVisual.new()
		visual.name = "Visual_" + character_variant
		visual.top_level = true
		add_child(visual)
		visual.setup(character_variant)

	anim = GaspAnimator.new()
	if not anim.initialize("res://data"):
		push_error("GaspAnimator failed to initialize")
	# ABP_UEFN_Mannequin_PostProcess (post process anim blueprint of SKM_UEFN_Mannequin)
	anim.set_post_process_scales({"head": Vector3(1.15, 1.15, 1.15), "thigh_l": Vector3(1.0, 1.12, 1.12), "foot_l": Vector3.ONE,
		"thigh_r": Vector3(1.0, 1.12, 1.12), "foot_r": Vector3.ONE})
	anim.set_physics_space(get_world_3d().space, get_rid())
	chooser.load_json("res://data/chooser_mm.json")
	chooser_sparse.load_json("res://data/chooser_mm_sparse.json")
	traversal_chooser.load_json("res://data/chooser_traversal.json")
	actor_yaw = -rad_to_deg(rotation.y)
	control_yaw = actor_yaw
	rotation = Vector3.ZERO
	var comp := _component_xform()
	anim.reset(comp[1], comp[0])
	if character_variant == "SetupComparison":
		_setup_comparison()

	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		control_yaw = UE.normalize_axis(control_yaw + event.relative.x * MOUSE_SENSITIVITY)
		control_pitch = clampf(control_pitch - event.relative.y * MOUSE_SENSITIVITY, -89.0, 89.0)
	elif event is InputEventKey and event.pressed and not event.echo:
		match event.physical_keycode:
			KEY_CTRL:
				if not wants_to_sprint:
					wants_to_walk = not wants_to_walk
			KEY_SPACE:
				_try_jump()
			KEY_C:
				if not is_falling:
					_set_crouched(not is_crouched)
			KEY_ESCAPE:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED else Input.MOUSE_MODE_CAPTURED
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_MIDDLE and event.pressed:
			wants_to_strafe = not wants_to_strafe
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			wants_to_aim = event.pressed
		elif event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _read_input() -> void:
	var x := 0.0; var y := 0.0
	if Input.is_physical_key_pressed(KEY_W): y += 1.0
	if Input.is_physical_key_pressed(KEY_S): y -= 1.0
	if Input.is_physical_key_pressed(KEY_D): x += 1.0
	if Input.is_physical_key_pressed(KEY_A): x -= 1.0
	var pad := Vector2(Input.get_joy_axis(0, JOY_AXIS_LEFT_X), -Input.get_joy_axis(0, JOY_AXIS_LEFT_Y))
	if pad.length() > 0.2:
		x = pad.x; y = pad.y
	move_input = Vector2(x, y)
	var sprint := Input.is_physical_key_pressed(KEY_SHIFT)
	if sprint != wants_to_sprint:
		wants_to_sprint = sprint
		wants_to_walk = false

# ------------------------------------------------------------------------------------------------
# CBP: UpdateMovement_PreCMC / UpdateRotation_PreCMC

func _pending_input_vector() -> Vector3:
	# AddMovementInput(right(controlYaw) * x) + (forward(controlYaw) * y); MovementStickMode 0 -> Normal2D
	var inp := move_input
	if inp.length_squared() > 0.0:
		inp = inp.normalized()
	var cy := deg_to_rad(control_yaw)
	var fwd := Vector3(cos(cy), sin(cy), 0)
	var right := Vector3(-sin(cy), cos(cy), 0)
	return right * inp.x + fwd * inp.y

func _can_sprint() -> bool:
	if not wants_to_sprint:
		return false
	if orient_rotation_to_movement:
		return true
	var dir := ue_acceleration
	if dir.is_zero_approx():
		dir = _pending_input_vector()
	return absf(UE.delta_yaw(actor_yaw, UE.vec_to_rotator_yaw(dir))) < 50.0

func _get_desired_gait() -> int:
	var full_input := true
	if _can_sprint():
		return EGait.SPRINT if full_input else EGait.RUN
	if wants_to_walk:
		return EGait.WALK
	return EGait.RUN if full_input else EGait.WALK

func _calc_max_speed() -> float:
	var strafe_map := 0.0
	if use_controller_desired_rotation:
		strafe_map = UE.eval_linear_curve(STRAFE_SPEED_MAP, absf(UE.calculate_direction(ue_velocity, UE.quat_from_yaw(actor_yaw))))
	var speeds: Vector3 = [WALK_SPEEDS, RUN_SPEEDS, SPRINT_SPEEDS][gait]
	if strafe_map < 1.0:
		return UE.map_range_clamped(strafe_map, 0.0, 1.0, speeds.x, speeds.y)
	return UE.map_range_clamped(strafe_map, 1.0, 2.0, speeds.y, speeds.z)

func _calc_max_crouch_speed() -> float:
	var strafe_map := 0.0
	if not orient_rotation_to_movement:
		strafe_map = UE.eval_linear_curve(STRAFE_SPEED_MAP, absf(UE.calculate_direction(ue_velocity, UE.quat_from_yaw(actor_yaw))))
	if strafe_map < 1.0:
		return UE.map_range_clamped(strafe_map, 0.0, 1.0, CROUCH_SPEEDS.x, CROUCH_SPEEDS.y)
	return UE.map_range_clamped(strafe_map, 1.0, 2.0, CROUCH_SPEEDS.y, CROUCH_SPEEDS.z)

func _pre_cmc() -> void:
	gait = _get_desired_gait()
	var speed_xy := UE.size_2d(ue_velocity)
	match gait:
		EGait.SPRINT:
			max_acceleration = UE.map_range_clamped(speed_xy, 300.0, 700.0, 800.0, 300.0)
			ground_friction = UE.map_range_clamped(speed_xy, 0.0, 500.0, 5.0, 3.0)
		_:
			max_acceleration = 800.0
			ground_friction = 5.0
	braking_deceleration_walking = 500.0 if not _pending_input_vector().is_zero_approx() else 2000.0
	max_walk_speed = _calc_max_speed()
	max_walk_speed_crouched = _calc_max_crouch_speed()
	# UpdateRotation_PreCMC
	if wants_to_strafe or wants_to_aim:
		use_controller_desired_rotation = true
		orient_rotation_to_movement = false
	else:
		use_controller_desired_rotation = false
		orient_rotation_to_movement = true

# ------------------------------------------------------------------------------------------------
# CharacterMovementComponent

func _max_speed() -> float:
	return max_walk_speed_crouched if is_crouched else max_walk_speed

func _apply_velocity_braking(dt: float, friction: float, braking_decel: float) -> void:
	if ue_velocity.is_zero_approx() or dt < GaspTrajectory.MIN_TICK_TIME:
		return
	friction = maxf(0.0, friction * maxf(0.0, BRAKING_FRICTION_FACTOR))
	braking_decel = maxf(0.0, braking_decel)
	var zero_friction := friction == 0.0
	var zero_braking := braking_decel == 0.0
	if zero_friction and zero_braking:
		return
	var old := ue_velocity
	var remaining := dt
	var max_step := clampf(BRAKING_SUB_STEP_TIME, 1.0 / 75.0, 1.0 / 20.0)
	var rev := Vector3.ZERO if zero_braking else -braking_decel * UE.safe_normal(ue_velocity)
	while remaining >= GaspTrajectory.MIN_TICK_TIME:
		var step := minf(max_step, remaining * 0.5) if (remaining > max_step and not zero_friction) else remaining
		remaining -= step
		ue_velocity = ue_velocity + ((-friction) * ue_velocity + rev) * step
		if ue_velocity.dot(old) <= 0.0:
			ue_velocity = Vector3.ZERO
			return
	var s2 := ue_velocity.length_squared()
	if s2 <= UE.KINDA_SMALL or (not zero_braking and s2 <= 100.0):
		ue_velocity = Vector3.ZERO

func _calc_velocity(dt: float, friction: float, braking_decel: float, accel: Vector3, analog_modifier: float) -> void:
	var max_speed := _max_speed()
	var max_input_speed := maxf(max_speed * analog_modifier, MIN_ANALOG_WALK_SPEED if not is_falling else 0.0)
	max_speed = max_input_speed
	var zero_accel := accel.is_zero_approx()
	var over_max := ue_velocity.length_squared() > pow(max_speed * 1.01, 2)
	if zero_accel or over_max:
		var old := ue_velocity
		var braking_friction := BRAKING_FRICTION if not is_falling else friction
		_apply_velocity_braking(dt, braking_friction, braking_decel)
		if over_max and ue_velocity.length_squared() < max_speed * max_speed and accel.dot(old) > 0.0:
			ue_velocity = UE.safe_normal(old) * max_speed
	elif not zero_accel:
		var acc_dir := UE.safe_normal(accel)
		var vs := ue_velocity.length()
		ue_velocity = ue_velocity - (ue_velocity - acc_dir * vs) * minf(dt * friction, 1.0)
	if not zero_accel:
		var new_max := ue_velocity.length() if ue_velocity.length_squared() > pow(max_input_speed * 1.01, 2) else max_input_speed
		ue_velocity += accel * dt
		ue_velocity = UE.clamped_to_max_size(ue_velocity, new_max)

func _physics_rotation(dt: float) -> void:
	var rate := 200.0 if is_falling else -1.0
	var desired_yaw := actor_yaw
	var has_desired := false
	if orient_rotation_to_movement:
		if ue_acceleration.length_squared() > UE.KINDA_SMALL:
			desired_yaw = UE.vec_to_rotator_yaw(ue_acceleration)
			has_desired = true
	elif use_controller_desired_rotation:
		desired_yaw = control_yaw
		has_desired = true
	if not has_desired:
		return
	var delta_deg := rate * dt if rate >= 0.0 else 360.0
	var diff := UE.normalize_axis(desired_yaw - actor_yaw)
	if absf(diff) <= delta_deg:
		actor_yaw = desired_yaw
	else:
		actor_yaw = UE.normalize_axis(actor_yaw + signf(diff) * delta_deg)

func _try_jump() -> void:
	# IA_Jump Started: TryTraversalAction first, Jump() if no traversal could be performed
	if doing_traversal:
		return
	var res := _try_traversal_action(_traversal_check_inputs())
	if res[0] or res[1]:
		_jump()

func _jump() -> void:
	if is_falling or is_crouched or is_flying:
		return
	var ground_speed := UE.size_2d(ue_velocity)
	ue_velocity.z = JUMP_Z
	is_falling = true
	_on_jumped(ground_speed)

func _set_crouched(c: bool) -> void:
	if c == is_crouched:
		return
	var half := CROUCHED_HALF_HEIGHT if c else CAPSULE_HALF_HEIGHT
	var adjust := CAPSULE_HALF_HEIGHT - CROUCHED_HALF_HEIGHT
	if not c:
		# UnCrouch: check head room
		var params := PhysicsShapeQueryParameters3D.new()
		var cap := CapsuleShape3D.new()
		cap.radius = CAPSULE_RADIUS * 0.01 * 0.95
		cap.height = CAPSULE_HALF_HEIGHT * 2.0 * 0.01
		params.shape = cap
		params.transform = Transform3D(Basis(), global_position + Vector3(0, (adjust - FLOOR_HOVER) * 0.01, 0))
		params.exclude = [get_rid()]
		if not get_world_3d().direct_space_state.intersect_shape(params, 1).is_empty():
			return
	is_crouched = c
	(collision.shape as CapsuleShape3D).height = half * 2.0 * 0.01
	global_position.y += (-adjust if c else adjust) * 0.01

## Level teleporter (Teleporter_Sender: K2_TeleportTo keeping rotation); location is the capsule center in UE space.
## UE keeps the velocity, the anim instance (blend stack, motion matching, pose history) and the trajectory history:
## CharacterMovementComponent::OnTeleported only re-finds the floor.
func level_teleport_to_ue(loc: Vector3) -> void:
	global_position = UE.to_godot_pos(loc)
	reset_physics_interpolation()
	_reset_pose_interpolation()

## CharacterMovementComponent::OnTeleported / SetMovementMode(Walking) -> FindFloor + AdjustFloorHeight: a walkable floor
## within MaxStepHeight + MAX_FLOOR_DIST keeps the character walking and snaps it to the floor (+ hover) instead of
## entering a fall and triggering a landing. Returns whether a walkable floor was found.
func _find_floor_and_snap(loc: Vector3) -> bool:
	var half := CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT
	var fl := _trace_ue(loc, loc - Vector3(0, 0, half + 45.0 + 2.4))
	if not fl["hit"] or fl["normal"].z < cos(floor_max_angle):
		return false
	global_position = UE.to_godot_pos(Vector3(loc.x, loc.y, fl["point"].z + half + FLOOR_HOVER))
	is_falling = false
	just_landed = false
	apply_floor_snap()
	return true

## Hard placement used for spawning (and tests): resets movement, animation and trajectory.
func teleport_to_ue(loc: Vector3) -> void:
	if doing_traversal:
		_on_traversal_montage_ended()
	montage_playing = false
	is_flying = false
	global_position = UE.to_godot_pos(loc)
	ue_velocity = Vector3.ZERO
	_find_floor_and_snap(loc)
	reset_physics_interpolation()
	_reset_pose_interpolation()
	var comp := _component_xform()
	anim.reset(comp[1], comp[0])
	traj = GaspTrajectory.new()
	traj.desired_controller_yaw_last_update = control_yaw
	Trajectory = traj
	_reset_extra_instances()

func spawn_at_ue(loc: Vector3, yaw: float) -> void:
	actor_yaw = yaw
	control_yaw = yaw
	teleport_to_ue(loc)

func _on_jumped(ground_speed_before_jump: float) -> void:
	foley.play("Jump", remap(clampf(ground_speed_before_jump, 0.0, 500.0), 0.0, 500.0, 0.5, 1.0))

func _on_landed(land_vel: Vector3) -> void:
	land_velocity = land_vel
	foley.play("Land", remap(clampf(land_vel.z, -900.0, -500.0), -500.0, -900.0, 0.5, 1.5))
	just_landed = true
	just_landed_timer = 0.3

func _physics_process(dt: float) -> void:
	_read_input()
	_pre_cmc()

	# --- CMC PerformMovement ---
	var input_vec := _pending_input_vector()
	var analog := clampf(input_vec.length(), 0.0, 1.0)
	ue_acceleration = UE.clamped_to_max_size(input_vec, 1.0) * max_acceleration
	ue_acceleration.z = 0.0
	# IA_Jump Triggered (InputTriggerDown): keep trying traversal while the button is held
	if Input.is_physical_key_pressed(KEY_SPACE) and not doing_traversal and not first_update:
		_try_traversal_action(_traversal_check_inputs())
	first_update = false
	# TickCharacterPose: montage advance + root motion (RootMotionFromMontagesOnly)
	var rm := _tick_montage(dt)
	if rm.get("root_motion", false):
		_physics_root_motion(dt, rm)
		_update_animation(dt)
		return
	if is_flying:
		_phys_flying(dt, analog)
		_update_animation(dt)
		return
	var old_velocity := ue_velocity
	if not is_falling:
		ue_velocity.z = 0.0
		_calc_velocity(dt, ground_friction, braking_deceleration_walking, ue_acceleration, analog)
	else:
		var fall_acc := ue_acceleration
		var tick_air := AIR_CONTROL
		if UE.size_2d(ue_velocity) < AIR_CONTROL_BOOST_VELOCITY_THRESHOLD:
			tick_air *= AIR_CONTROL_BOOST_MULTIPLIER
		fall_acc *= tick_air
		var vz := ue_velocity.z
		ue_velocity.z = 0.0
		_calc_velocity(dt, 0.0, 0.0, fall_acc, analog)
		ue_velocity.z = maxf(vz + GRAVITY_Z * dt, -TERMINAL_VELOCITY)

	_physics_rotation(dt)

	velocity = UE.to_godot_dir(ue_velocity) * 0.01
	var pre_move_loc := _actor_location_ue()
	var pre_move_z := pre_move_loc.z
	# PhysFalling never snaps to the floor (Godot would, right after leaving a ledge, pulling the capsule onto the corner)
	var snap := floor_snap_length
	if is_falling:
		floor_snap_length = 0.0
	move_and_slide()
	floor_snap_length = snap
	var was_falling := is_falling
	var new_vel := UE.from_godot_dir(velocity) * 100.0
	var on_floor := is_on_floor()
	if on_floor and not was_falling and not _perch_floor(Vector3(_actor_location_ue().x, _actor_location_ue().y, pre_move_z)):
		# off the ledge: start falling from the walking height instead of rolling down the corner
		var gp := global_position
		gp.y = UE.to_godot_pos(Vector3(0, 0, pre_move_z)).y
		global_position = gp
		on_floor = false
	elif on_floor and was_falling and not _perch_floor(_actor_location_ue()):
		# PhysFalling IsValidLandingSpot: touching a ledge corner outside the perch radius is not a landing
		on_floor = false
	if on_floor:
		if was_falling and ue_velocity.z <= 0.0:
			is_falling = false
			_on_landed(ue_velocity)
			new_vel.z = 0.0
		elif not was_falling:
			# PhysWalking: "Make velocity reflect actual move" (Velocity = (NewLocation - OldLocation) / timeTick), so running
			# into a wall decelerates over the frames of the partial moves instead of the slide-projected velocity
			var moved := _actor_location_ue() - pre_move_loc
			new_vel = Vector3(moved.x, moved.y, 0.0) / maxf(dt, 1e-4)
	else:
		if not was_falling:
			is_falling = true
			new_vel.z = minf(new_vel.z, 0.0)
	ue_velocity = new_vel

	if just_landed:
		just_landed_timer -= dt
		if just_landed_timer <= 0.0:
			just_landed = false

	_update_animation(dt)

# ------------------------------------------------------------------------------------------------
# Animation (ABP_SandboxCharacter)

## PhysFlying without animation root motion, e.g. while a traversal montage auto-blends out (the montage gives no root
## motion once stopped, the movement mode stays Flying until OnCompleted): CalcVelocity(Friction = 0.5 * FluidFriction,
## bFluid = true, BrakingDecelerationFlying = 0) with MaxFlySpeed, no gravity, then SafeMoveUpdatedComponent + slide.
const MAX_FLY_SPEED := 600.0
const FLUID_FRICTION := 0.3 # default physics volume

func _phys_flying(dt: float, analog: float) -> void:
	var friction := 0.5 * FLUID_FRICTION
	var max_input_speed := MAX_FLY_SPEED * analog # GetMinAnalogSpeed() is 0 while flying
	var zero_accel := ue_acceleration.is_zero_approx()
	var over_max := ue_velocity.length_squared() > pow(max_input_speed * 1.01, 2)
	if zero_accel or over_max:
		var old := ue_velocity
		_apply_velocity_braking(dt, BRAKING_FRICTION, 0.0)
		if over_max and ue_velocity.length_squared() < max_input_speed * max_input_speed and ue_acceleration.dot(old) > 0.0:
			ue_velocity = UE.safe_normal(old) * max_input_speed
	else:
		var acc_dir := UE.safe_normal(ue_acceleration)
		var vs := ue_velocity.length()
		ue_velocity = ue_velocity - (ue_velocity - acc_dir * vs) * minf(dt * friction, 1.0)
	ue_velocity *= 1.0 - minf(friction * dt, 1.0) # bFluid
	if not zero_accel:
		var new_max := ue_velocity.length() if ue_velocity.length_squared() > pow(max_input_speed * 1.01, 2) else max_input_speed
		ue_velocity += ue_acceleration * dt
		ue_velocity = UE.clamped_to_max_size(ue_velocity, new_max)
	_physics_rotation(dt)
	velocity = UE.to_godot_dir(ue_velocity) * 0.01
	var snap := floor_snap_length
	floor_snap_length = 0.0
	move_and_slide()
	floor_snap_length = snap
	ue_velocity = UE.from_godot_dir(velocity) * 100.0

## PhysFlying with animation root motion (velocity from root motion, rotation applied directly)
func _physics_root_motion(dt: float, rm: Dictionary) -> void:
	var dq: Quaternion = rm.get("delta_rot", Quaternion.IDENTITY)
	actor_yaw = UE.quat_yaw(dq * UE.quat_from_yaw(actor_yaw))
	var dp: Vector3 = rm.get("delta_pos", Vector3.ZERO)
	ue_velocity = dp / maxf(dt, 1e-4)
	if not is_flying:
		ue_velocity.z = ue_velocity.z if is_falling else 0.0
	velocity = UE.to_godot_dir(ue_velocity) * 0.01
	var snap := floor_snap_length
	floor_snap_length = 0.0
	move_and_slide()
	floor_snap_length = snap
	ue_velocity = UE.from_godot_dir(velocity) * 100.0
	if not is_flying:
		is_falling = not is_on_floor()

func _actor_location_ue() -> Vector3:
	return UE.from_godot_pos(global_position)

## Skeletal mesh component world transform (UE): [quat, location]
func _component_xform() -> Array:
	return _component_xform_at(_actor_location_ue())

## Actor location as drawn this frame (physics interpolation between the last two physics ticks). Anything updated
## per rendered frame (the camera) must follow this, not global_position, or it steps at the 60 Hz physics rate
## against the smoothly drawn character and the character visibly shakes.
func _render_location_ue() -> Vector3:
	return UE.from_godot_pos(get_global_transform_interpolated().origin)

func _component_xform_render() -> Array:
	return _component_xform_at(_render_location_ue())

func _component_xform_at(actor_loc: Vector3) -> Array:
	var actor_q := UE.quat_from_yaw(actor_yaw)
	var rel_z := MESH_REL_Z + (CAPSULE_HALF_HEIGHT - CROUCHED_HALF_HEIGHT if is_crouched else 0.0)
	var loc := actor_loc + actor_q * Vector3(0, 0, rel_z) + comp_world_offset
	var q := actor_q * UE.quat_from_yaw(MESH_REL_YAW)
	return [q.normalized(), loc]

## CMC FindFloor with bUseFlatBaseForFloorChecks and PerchRadiusThreshold 20: the character keeps standing on a ledge
## (on a flat base, at full height) only while walkable floor lies within ValidPerchRadius = radius - 20 = 10 cm of the
## capsule centre, within MaxStepHeight + MAX_FLOOR_DIST below the capsule bottom. Godot's rounded capsule would
## otherwise keep "floor" contact on the corner and slide down it before falling.
const PERCH_RADIUS_THRESHOLD := 20.0

func _perch_floor(loc: Vector3) -> bool:
	var half := CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT
	var r := (CAPSULE_RADIUS - PERCH_RADIUS_THRESHOLD) * 0.707
	for o in [Vector2.ZERO, Vector2(r, r), Vector2(-r, r), Vector2(r, -r), Vector2(-r, -r)]:
		var start := loc + Vector3(o.x, o.y, 0.0)
		var fl := _trace_ue(start, start - Vector3(0, 0, half + floor_snap_length * 100.0 + 2.4))
		if fl["hit"] and fl["normal"].z >= cos(floor_max_angle):
			return true
	return false

func _trace_ue(from_ue: Vector3, to_ue: Vector3) -> Dictionary:
	var q := PhysicsRayQueryParameters3D.create(UE.to_godot_pos(from_ue), UE.to_godot_pos(to_ue))
	q.exclude = [get_rid()]
	var r := get_world_3d().direct_space_state.intersect_ray(q)
	if r.is_empty():
		return {"hit": false}
	return {"hit": true, "point": UE.from_godot_pos(r["position"]), "normal": UE.from_godot_dir(r["normal"])}

func _update_trajectory(dt: float) -> void:
	var d := GaspTrajectory.Derived.new()
	var analog := clampf(_pending_input_vector().length(), 0.0, 1.0)
	d.max_speed = maxf(_max_speed() * analog, MIN_ANALOG_WALK_SPEED if not is_falling else 0.0)
	d.braking_deceleration = maxf(0.0, braking_deceleration_walking if not is_falling else 0.0)
	d.braking_sub_step_time = BRAKING_SUB_STEP_TIME
	d.orient_rotation_to_movement = orient_rotation_to_movement
	d.velocity = ue_velocity
	d.acceleration = ue_acceleration
	d.step_ground_prediction = not is_falling
	if d.acceleration.is_zero_approx():
		d.friction = maxf(0.0, BRAKING_FRICTION * maxf(0.0, BRAKING_FRICTION_FACTOR))
	else:
		d.friction = ground_friction
	# idle / moving trajectory data
	var max_yaw_rate := 100.0 if not (Speed2D > 0.0) else 0.0
	var desired_yaw := control_yaw
	var yaw_delta := desired_yaw - traj.desired_controller_yaw_last_update
	traj.desired_controller_yaw_last_update = desired_yaw
	d.controller_yaw_rate = UE.normalize_axis(yaw_delta) * (1.0 / dt)
	if max_yaw_rate >= 0.0:
		d.controller_yaw_rate = signf(d.controller_yaw_rate) * minf(absf(d.controller_yaw_rate), max_yaw_rate)
	var comp := _component_xform()
	d.position = comp[1]
	d.mesh_comp_relative_rotation = UE.quat_from_yaw(MESH_REL_YAW)
	if d.orient_rotation_to_movement:
		d.facing = comp[0]
	else:
		d.facing = UE.quat_from_yaw(traj.desired_controller_yaw_last_update) * d.mesh_comp_relative_rotation
	traj.generate(d, dt, -1.0, 30, 0.1, 15, 0.0)
	traj.handle_world_collisions(ue_velocity, GRAVITY_Z, 0.01, 150.0, _trace_ue)
	TrajectoryCollision = {"TimeToLand": traj.time_to_land, "LandSpeed": traj.land_speed}
	trj_past_velocity = traj.velocity(-0.3, -0.2)
	trj_current_velocity = traj.velocity(0.0, 0.2)
	trj_future_velocity = traj.velocity(0.4, 0.5)

func _is_moving_ue() -> bool:
	var fv := trj_future_velocity
	var ne_future := absf(fv.x) > 10.0 or absf(fv.y) > 10.0 or absf(fv.z) > 10.0
	var ne_acc := absf(acceleration.x) > UE.KINDA_SMALL or absf(acceleration.y) > UE.KINDA_SMALL or absf(acceleration.z) > UE.KINDA_SMALL
	return ne_future and ne_acc

func _trajectory_turn_angle() -> float:
	return UE.delta_yaw(UE.vec_to_rotator_yaw(trj_future_velocity), UE.vec_to_rotator_yaw(abp_velocity))

func _update_essential_values(dt: float) -> void:
	character_xform_q_last = character_xform_q
	character_xform_q = UE.quat_from_yaw(actor_yaw)
	var ro: Transform3D = anim.get_root_offset_ue()
	var rr := UE.quat_to_rotator(ro.basis.get_rotation_quaternion())
	root_xform_q = UE.quat_from_rotator(rr.x, rr.y + 90.0, rr.z)
	acceleration = ue_acceleration
	acceleration_amount = acceleration.length() / max_acceleration
	has_acceleration = acceleration_amount > 0.0
	velocity_last = abp_velocity
	abp_velocity = ue_velocity
	Speed2D = UE.size_2d(abp_velocity)
	has_velocity = Speed2D > 5.0
	velocity_acceleration = (abp_velocity - velocity_last) / maxf(dt, 0.001)
	if has_velocity:
		last_non_zero_velocity = abp_velocity

func _update_states() -> void:
	movement_mode_last = MovementMode
	MovementMode = EMovementMode.IN_AIR if is_falling else EMovementMode.ON_GROUND
	rotation_mode_last = RotationMode
	RotationMode = ERotationMode.ORIENT_TO_MOVEMENT if orient_rotation_to_movement else ERotationMode.STRAFE
	movement_state_last = MovementState
	MovementState = EMovementState.MOVING if _is_moving_ue() else EMovementState.IDLE
	gait_last = Gait
	Gait = gait
	stance_last = Stance
	Stance = EStance.CROUCH if is_crouched else EStance.STAND

func _mm_interrupt_mode() -> int:
	var changed := MovementMode != movement_mode_last
	var state_change := (MovementState != movement_state_last) or (Gait != gait_last and MovementState == EMovementState.MOVING) or (Stance != stance_last)
	if changed or (state_change and MovementMode == EMovementMode.ON_GROUND):
		return 1 # InterruptOnDatabaseChange
	return 0

func _mm_blend_time() -> float:
	if MovementMode == EMovementMode.ON_GROUND:
		return 0.5 if movement_mode_last == EMovementMode.ON_GROUND else 0.2
	# InAir: Velocity > 100 compares the vector (z) per BP Greater_DoubleDouble on Velocity.Z
	return 0.15 if abp_velocity.z > 100.0 else 0.5

func _has_tag(t: String) -> bool:
	return current_database_tags.has(t)

func _update_chooser_inputs() -> void:
	var moving := MovementState == EMovementState.MOVING
	IsStarting = moving and UE.size_2d(trj_future_velocity) >= UE.size_2d(abp_velocity) + 100.0 and not _has_tag("Pivots")
	# MM Pivot conditions: |TrajectoryTurnAngle| >= (OrientToMovement ? 45 : 30)
	var pivot_thr := 45.0 if RotationMode == ERotationMode.ORIENT_TO_MOVEMENT else 30.0
	IsPivoting = absf(_trajectory_turn_angle()) >= pivot_thr and moving
	var yaw_diff := absf(UE.delta_yaw(UE.quat_yaw(character_xform_q), UE.quat_yaw(root_xform_q)))
	ShouldTurnInPlace = yaw_diff >= 50.0 and (wants_to_aim or (MovementState == EMovementState.IDLE and movement_state_last == EMovementState.MOVING))
	ShouldSpinTransition = yaw_diff >= 130.0 and Speed2D >= 150.0 and not _has_tag("Pivots")
	JustLanded_Light = just_landed and absf(land_velocity.z) < absf(HEAVY_LAND_SPEED_THRESHOLD)
	JustLanded_Heavy = just_landed and absf(land_velocity.z) >= absf(HEAVY_LAND_SPEED_THRESHOLD)
	JustTraversed = not montage_slot_active and anim.get_curve("MovingTraversal") > 0.0 and absf(_trajectory_turn_angle()) <= 50.0

func _relative_acceleration_amount() -> Vector3:
	var max_brake := braking_deceleration_walking if not is_falling else 0.0
	if max_acceleration > 0.0 and max_brake > 0.0:
		var unrot := character_xform_q.inverse()
		if acceleration.dot(abp_velocity) > 0.0:
			return unrot * (UE.clamped_to_max_size(velocity_acceleration, max_acceleration) / max_acceleration)
		return unrot * (UE.clamped_to_max_size(velocity_acceleration, max_brake) / max_brake)
	return Vector3.ZERO

func _lean_amount() -> float:
	return _relative_acceleration_amount().y * UE.map_range_clamped(Speed2D, 200.0, 500.0, 0.5, 1.0)

func _ao_value() -> Vector2:
	var root_rot := UE.quat_to_rotator(root_xform_q)
	var v := Vector2(UE.normalize_axis(control_yaw - root_rot.y), UE.normalize_axis(control_pitch - root_rot.x))
	return v.lerp(Vector2.ZERO, anim.get_curve("Disable_AO"))

func _enable_ao(ao: Vector2) -> bool:
	var lim := 115.0 if MovementState == EMovementState.IDLE else 180.0
	return absf(ao.x) <= lim and RotationMode == ERotationMode.STRAFE and slot_weight < 0.5

var montage_slot_active := false

# ------------------------------------------------------------------------------------------------
# Additional ABP_SandboxCharacter instances (CBP_SandboxCharacter_SetupComparison: MotionMatchCharacter / StateMachineCharacter).
# Every skeletal mesh component runs its own anim instance, so the ABP variables below are kept per instance.
const ABP_FIELDS := ["anim", "sm", "component_tags", "use_experimental_state_machine", "current_database_tags", "mesh_root", "skeleton",
	"comp_world_offset", "traj", "Trajectory", "TrajectoryCollision", "trj_past_velocity", "trj_current_velocity", "trj_future_velocity",
	"character_xform_q", "character_xform_q_last", "root_xform_q", "acceleration", "acceleration_amount", "has_acceleration", "velocity_last",
	"abp_velocity", "Speed2D", "has_velocity", "velocity_acceleration", "last_non_zero_velocity", "MovementMode", "movement_mode_last",
	"RotationMode", "rotation_mode_last", "MovementState", "movement_state_last", "Gait", "gait_last", "Stance", "stance_last", "IsStarting",
	"IsPivoting", "ShouldTurnInPlace", "ShouldSpinTransition", "JustLanded_Light", "JustLanded_Heavy", "JustTraversed", "slot_weight",
	"montage_slot_active"]
## world offset added to the mesh component location (SetRelativeLocation(0) + AddWorldOffset each tick)
var comp_world_offset := Vector3.ZERO
var extra_instances: Array = [] # [{fields, label}]

func _save_abp_fields() -> Dictionary:
	var d := {}
	for f in ABP_FIELDS:
		d[f] = get(f)
	return d

func _load_abp_fields(d: Dictionary) -> void:
	for f in ABP_FIELDS:
		set(f, d[f])

func _new_animator() -> GaspAnimator:
	var a := GaspAnimator.new()
	if not a.initialize("res://data"):
		push_error("GaspAnimator failed to initialize")
	# ABP_UEFN_Mannequin_PostProcess (post process anim blueprint of SKM_UEFN_Mannequin)
	a.set_post_process_scales({"head": Vector3(1.15, 1.15, 1.15), "thigh_l": Vector3(1.0, 1.12, 1.12), "foot_l": Vector3.ONE,
		"thigh_r": Vector3(1.0, 1.12, 1.12), "foot_r": Vector3.ONE})
	a.set_physics_space(get_world_3d().space, get_rid())
	return a

## CBP_SandboxCharacter_SetupComparison components: two UEFN meshes with their own ABP_SandboxCharacter, Mesh hidden
func _setup_comparison() -> void:
	mesh_root.visible = false
	var main_fields := _save_abp_fields()
	var specs := [{"tags": ["Force MM Setup"], "offset": Vector3(0, -50, 0), "label": "Motion Matching Setup\nOver 800 Animations", "color": null},
		{"tags": ["Force SM Setup"], "offset": Vector3(0, 50, 0), "label": "State Machine Setup\nLess than 400 Animations", "color": Color(0.16, 0.266667, 0.8)}]
	for spec in specs:
		var root: Node3D = mesh_scene.instantiate()
		root.top_level = true
		add_child(root)
		if spec["color"] != null:
			# MI_UEFN_Mannequin_Blue (M_UEFN_Mannequin_Masked region colours)
			var mat := CharacterMaterials.uefn_masked("MI_UEFN_Mannequin_Blue")
			for mi in root.find_children("*", "MeshInstance3D", true, false):
				(mi as MeshInstance3D).material_override = mat
		var f := main_fields.duplicate()
		f["anim"] = _new_animator()
		f["sm"] = null
		f["use_experimental_state_machine"] = false
		f["component_tags"] = spec["tags"]
		f["current_database_tags"] = PackedStringArray()
		f["mesh_root"] = root
		f["skeleton"] = root.find_children("*", "Skeleton3D", true, false)[0]
		f["comp_world_offset"] = spec["offset"]
		var t := GaspTrajectory.new()
		t.desired_controller_yaw_last_update = control_yaw
		f["traj"] = t
		f["Trajectory"] = t
		extra_instances.append({"fields": f, "label": spec["label"]})
	_reset_extra_instances()

func _reset_extra_instances() -> void:
	for inst in extra_instances:
		var f: Dictionary = inst["fields"]
		var actor_q := UE.quat_from_yaw(actor_yaw)
		var comp := _component_xform()
		(f["anim"] as GaspAnimator).reset(comp[1] + f["comp_world_offset"], comp[0])
		var t := GaspTrajectory.new()
		t.desired_controller_yaw_last_update = control_yaw
		f["traj"] = t
		f["Trajectory"] = t
		f["sm"] = null
		f["use_experimental_state_machine"] = false

## Render-rate pose interpolation (see GaspPoseInterpolator): the pose is evaluated on physics ticks like the body, and
## drawn interpolated between the last two ticks like the body transform, instead of in 60 Hz steps.
var pose_interp := {}  # Skeleton3D -> GaspPoseInterpolator

func _visible_skeletons(is_main: bool) -> Array:
	var out := [skeleton]
	if visual and is_main:
		for p in visual.parts:
			if p["skeleton"] != null and not out.has(p["skeleton"]):
				out.append(p["skeleton"])
	return out

func _capture_poses(is_main: bool) -> void:
	if OS.has_environment("GASP_NO_POSE_INTERP"):
		return
	for sk in _visible_skeletons(is_main):
		if not pose_interp.has(sk):
			pose_interp[sk] = GaspPoseInterpolator.new()
		pose_interp[sk].capture(sk)

func _reset_pose_interpolation() -> void:
	for sk in pose_interp:
		pose_interp[sk].reset()

func _process(_dt: float) -> void:
	if pose_interp.is_empty():
		return
	var f := Engine.get_physics_interpolation_fraction()
	for sk in pose_interp.keys():
		if not is_instance_valid(sk):
			pose_interp.erase(sk)
			continue
		pose_interp[sk].apply(sk, f)

func _update_animation(dt: float) -> void:
	_update_anim_instance(dt, true)
	if extra_instances.is_empty():
		return
	var main_fields := _save_abp_fields()
	for inst in extra_instances:
		_load_abp_fields(inst["fields"])
		# their montages (PerformTraversalAction plays the traversal montage on each component) only drive the pose
		if (anim.montage_state() as Dictionary).get("playing", false):
			var comp := _component_xform()
			anim.montage_tick(dt, {"mesh_pos": comp[1], "mesh_rot": comp[0], "actor_pos": _actor_location_ue(), "actor_rot": UE.quat_from_yaw(actor_yaw),
				"visual_root": _actor_location_ue() - Vector3(0, 0, CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT),
				"has_acceleration": not ue_acceleration.is_zero_approx(), "is_falling": is_falling})
		_update_anim_instance(dt, false)
		DebugDraw.text(_component_xform()[1] + Vector3(0, 0, 180), String(inst["label"]), Color.BLACK)
		inst["fields"] = _save_abp_fields()
	_load_abp_fields(main_fields)

func _update_anim_instance(dt: float, is_main: bool) -> void:
	var ms: Dictionary = anim.montage_state()
	montage_slot_active = ms.get("slot_active", false)
	slot_weight = ms.get("weight", 0.0)
	# Update_Logic: run from BlueprintThreadSafeUpdateAnimation when DDCVar.ThreadSafeAnimationUpdate.Enable is set, otherwise from
	# BlueprintUpdateAnimation; both run it once per frame before the anim graph update, so the result is the same
	_update_trajectory(dt)
	_update_essential_values(dt)
	_update_states()
	# Update_CVarDrivenVariables: UseExperimentalStateMachine
	var use_sm := (CVars.get_b("DDCVar.ExperimentalStateMachine.Enable") or component_tags.has("Force SM Setup")) and not component_tags.has("Force MM Setup")
	if use_sm and sm == null:
		sm = ExperimentalStateMachine.new(self)
	if use_sm and not use_experimental_state_machine:
		sm.reset() # TwoWayBlend switching to the state machine re-initializes it
	use_experimental_state_machine = use_sm
	if use_sm:
		sm.update_logic()
	_update_chooser_inputs()
	if use_sm:
		IsPivoting = sm.is_pivoting()
		sm.update(dt)
	# CHT_PoseSearchDatabases: MMDatabaseLOD (DDCvar.MMDatabaseLOD) in [0, 0] -> Dense, [1, 1] -> Sparse, otherwise no row
	var lod := CVars.get_f("DDCvar.MMDatabaseLOD")
	var dbs := PackedStringArray()
	if lod == 0.0:
		dbs = chooser.evaluate(self)
	elif lod == 1.0:
		dbs = chooser_sparse.evaluate(self)
	var comp := _component_xform()
	var moving := MovementState == EMovementState.MOVING
	var ao := _ao_value()
	var inputs := {
		"comp_pos": comp[1], "comp_rot": comp[0],
		"traj": traj.to_packed(),
		"databases": dbs,
		"interrupt": _mm_interrupt_mode(),
		"mm_blend_time": _mm_blend_time(),
		"locomotion_dir": last_non_zero_velocity,
		"desired_facing": traj.sample_at(0.5)[1],
		"enable_steering": moving or MovementMode == EMovementMode.IN_AIR,
		"offset_translation_mode": "Release" if montage_slot_active else (("Interpolate" if moving else "Release") if MovementMode == EMovementMode.ON_GROUND else "Release"),
		"offset_rotation_mode": "Release" if montage_slot_active else "Accumulate",
		"offset_translation_halflife": 0.3 if moving else 0.1,
		"offset_rotation_halflife": 0.1,
		"offset_max_translation": CVars.get_f("DDCvar.OffsetRootBone.TranslationRadius"),
		"offset_clamp_velocity": moving,
		"offset_root_enabled": true,
		"is_moving": moving,
		"lean": _lean_amount(),
		"enable_ao": _enable_ao(ao),
		"ao_yaw": ao.x,
		"ao_pitch": ao.y,
		"debug_disable": debug_disable,
		"notify_recency_timeout": [0.2, 0.2, 0.16][Gait], # Get_MMNotifyRecencyTimeOut
		"foot_placement": CVars.get_b("a.AnimNode.FootPlacement.Enable"),
		"velocity": ue_velocity,
	}
	if has_interaction:
		inputs["interaction_pos"] = interaction_pos_ue
		inputs["interaction_rot"] = interaction_rot_ue
	# a.AnimNode.OrientationWarping.Enable / a.AnimNode.OffsetRootBone.Enable
	inputs["debug_disable"] = debug_disable | (0 if CVars.get_b("a.AnimNode.OrientationWarping.Enable") else 1) | (0 if CVars.get_b("a.AnimNode.OffsetRootBone.Enable") else 16)
	# CharacterMovementComponent::CurrentFloor for FootPlacement
	var walking := not is_falling and not is_flying
	inputs["fp_walking"] = walking
	var fl := _trace_ue(_actor_location_ue(), _actor_location_ue() - Vector3(0, 0, (CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT) + 10.0))
	inputs["fp_floor_hit"] = walking and fl["hit"]
	if fl["hit"]:
		inputs["fp_floor_location"] = fl["point"]
		inputs["fp_floor_normal"] = fl["normal"]
	if use_sm:
		sm.graph_inputs(inputs)
	anim.update(dt, inputs)
	if use_sm:
		sm.post_update(anim.get_state())
	current_database_tags = anim.get_database_tags()
	# BP_AnimNotify_FoleyEvent (CanPlayFootstepSounds = IsMovingOnGround or DoingTraversalAction)
	var fired: Array = anim.get_fired_notifies()
	foley.handle_notifies(fired, (not is_falling and not is_flying) or doing_traversal)
	_debug_foley(fired)
	mesh_root.global_transform = UE.to_godot_xform(comp[0], comp[1])
	anim.apply_to_skeleton(skeleton)
	if visual and is_main:
		visual.global_transform = mesh_root.global_transform
		visual.update_pose(anim, dt, comp)
	_capture_poses(is_main)
	if debug_label and is_main:
		var show_move := CVars.get_b("p.VisualizeMovement") or always_show_debug_text
		var show_mm := CVars.get_b("a.AnimNode.MotionMatching.DebugDrawQuery") or always_show_debug_text
		debug_label.visible = show_move or show_mm
		if debug_label.visible:
			var st: Dictionary = anim.get_state()
			var t := ""
			if show_move:
				t += "Speed %.0f  Gait %s  %s  %s  Mode %s\nVelocity %s  Accel %s\n" % [Speed2D, ["Walk", "Run", "Sprint"][Gait], ["Stand", "Crouch"][Stance], ["Orient", "Strafe"][RotationMode],
					["Walking", "Falling", "Flying"][2 if is_flying else (1 if is_falling else 0)], str(ue_velocity.round()), str(ue_acceleration.round())]
			if show_mm and use_experimental_state_machine:
				t += "SM %s  Anim %s  t=%.2f  rate=%.2f  stack=%d
" % [sm.NODE_NAMES[sm.node], st.get("sm_anim", ""), st.get("sm_time", 0.0), st.get("sm_play_rate", 1.0), st.get("sm_blend_stack", 0)]
			elif show_mm:
				t += "DB %s\nAnim %s  t=%.2f  cost=%.2f  rate=%.2f\nDBs: %s" % [st.get("database", ""), st.get("anim", ""), st.get("anim_time", 0.0), st.get("cost", 0.0), st.get("play_rate", 1.0), ", ".join(dbs)]
				if montage_slot_active:
					t += "\nMontage %s  t=%.2f  w=%.2f" % [String(ms.get("montage", "")).get_file(), ms.get("position", 0.0), slot_weight]
			debug_label.text = t
	if is_main:
		_debug_draw()
	elif use_experimental_state_machine and CVars.get_b("DDCVar.ExperimentalStateMachine.Debug"):
		var ro: Transform3D = anim.get_root_offset_ue()
		sm.debug_draw(_component_xform()[1], UE.quat_to_rotator(ro.basis.get_rotation_quaternion()).y, _max_speed())

# ------------------------------------------------------------------------------------------------
# Traversal (CBP: GetTraversalCheckInputs / TryTraversalAction / UpdateWarpTargets / PerformTraversalAction,
# LevelBlock_Traversable: GetLedgeTransforms / FindLedgeClosestToActor)

const TRAVERSABLE_MASK := 2  # ECC_GameTraceChannel1 "Traversable"
const VISIBILITY_MASK := 1
const MIN_LEDGE_WIDTH := 60.0
enum ETraversalAction { NONE, HURDLE, VAULT, MANTLE }  # values of E_TraversalActionType (NewEnumerator0/2/1/3)

func _ue_movement_mode() -> int:
	if is_flying: return 5
	return 3 if is_falling else 1

func _traversal_check_inputs() -> Dictionary:
	var aq := UE.quat_from_yaw(actor_yaw)
	var fwd := aq * Vector3(1, 0, 0)
	if not is_falling and not is_flying:
		var local_v := aq.inverse() * ue_velocity
		return {"dir": fwd, "dist": UE.map_range_clamped(local_v.x, 0.0, 500.0, 75.0, 350.0), "origin": Vector3.ZERO,
			"end_offset": Vector3.ZERO, "radius": 30.0, "half_height": 60.0}
	return {"dir": fwd, "dist": 75.0, "origin": Vector3.ZERO, "end_offset": Vector3(0, 0, 50), "radius": 30.0, "half_height": 86.0}

## KismetSystemLibrary::CapsuleTraceSingle in UE space
func _capsule_trace(start: Vector3, end: Vector3, radius: float, half_height: float, mask: int) -> Dictionary:
	var cap := CapsuleShape3D.new()
	cap.radius = radius * 0.01
	cap.height = maxf(half_height, radius) * 2.0 * 0.01
	var params := PhysicsShapeQueryParameters3D.new()
	params.shape = cap
	params.collision_mask = mask
	params.exclude = [get_rid()]
	params.transform = Transform3D(Basis(), UE.to_godot_pos(start))
	var space := get_world_3d().direct_space_state
	var initial := not space.intersect_shape(params, 1).is_empty()
	if initial:
		var info0 := space.get_rest_info(params)
		var col0 = instance_from_id(info0["collider_id"]) if info0.has("collider_id") else null
		var p0: Vector3 = info0.get("point", params.transform.origin)
		return {"hit": true, "initial": true, "point": UE.from_godot_pos(p0), "collider": col0}
	var motion := UE.to_godot_dir(end - start) * 0.01
	params.motion = motion
	var r := space.cast_motion(params)
	if r[1] >= 1.0:
		return {"hit": false, "initial": false}
	params.transform.origin += motion * r[1]
	params.motion = Vector3.ZERO
	var info := space.get_rest_info(params)
	if info.is_empty():
		return {"hit": false, "initial": false}
	return {"hit": true, "initial": false, "point": UE.from_godot_pos(info["point"]), "normal": UE.from_godot_dir(info["normal"]),
		"collider": instance_from_id(info["collider_id"])}

func _find_ledge_closest_to_actor(ledges: Array, actor_loc: Vector3) -> int:
	var best := -1
	var best_d := INF
	for i in ledges.size():
		var l: Dictionary = ledges[i]
		var cp: Vector3 = UE.closest_on_segment(l["a"], l["b"], actor_loc)[0]
		var nrm: Vector3 = l["normal"]
		var d: float = (cp + nrm * 10.0).distance_to(actor_loc)
		if d < best_d or i == 0:
			best_d = d
			best = i
	return best

func _get_ledge_transforms(body: Node, hit_loc: Vector3, actor_loc: Vector3, r: Dictionary) -> void:
	var ledges: Array = body.get_meta("ledges", [])
	var li := _find_ledge_closest_to_actor(ledges, actor_loc)
	if li < 0:
		r["has_front_ledge"] = false
		return
	var l: Dictionary = ledges[li]
	var length: float = (l["b"] - l["a"]).length()
	if length < MIN_LEDGE_WIDTH:
		r["has_front_ledge"] = false
		return
	var dist: float = clampf(UE.closest_on_segment(l["a"], l["b"], hit_loc)[1], MIN_LEDGE_WIDTH / 2.0, length - MIN_LEDGE_WIDTH / 2.0)
	r["has_front_ledge"] = true
	r["front_ledge_location"] = l["a"] + (l["b"] - l["a"]).normalized() * dist
	r["front_ledge_normal"] = l["normal"]
	var oi := li ^ 1 # OppositeLedges: 1<->2, 3<->4
	if oi >= ledges.size():
		r["has_back_ledge"] = false
		return
	var o: Dictionary = ledges[oi]
	r["has_back_ledge"] = true
	r["back_ledge_location"] = UE.closest_on_segment(o["a"], o["b"], r["front_ledge_location"])[0]
	r["back_ledge_normal"] = o["normal"]

## returns [TraversalCheckFailed, MontageSelectionFailed]
func _try_traversal_action(inputs: Dictionary) -> Array:
	var actor_loc := _actor_location_ue()
	var cap_r := CAPSULE_RADIUS
	var cap_hh := CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT
	var r := {"action": ETraversalAction.NONE, "has_front_ledge": false, "front_ledge_location": Vector3.ZERO, "front_ledge_normal": Vector3.ZERO,
		"has_back_ledge": false, "back_ledge_location": Vector3.ZERO, "back_ledge_normal": Vector3.ZERO,
		"has_back_floor": false, "back_floor_location": Vector3.ZERO, "obstacle_height": 0.0, "obstacle_depth": 0.0, "back_ledge_height": 0.0}
	var start: Vector3 = actor_loc + inputs["origin"]
	var end: Vector3 = start + inputs["dir"] * inputs["dist"] + inputs["end_offset"]
	var hit := _capsule_trace(start, end, inputs["radius"], inputs["half_height"], TRAVERSABLE_MASK)
	if not hit["hit"] or hit["collider"] == null or not hit["collider"].has_meta("traversable"):
		return [true, false]
	r["hit_component"] = hit["collider"]
	_get_ledge_transforms(hit["collider"], hit["point"], actor_loc, r)
	# then_2: room checks
	if not r["has_front_ledge"]:
		return [true, false]
	var room_front: Vector3 = r["front_ledge_location"] + r["front_ledge_normal"] * (cap_r + 2.0) + Vector3(0, 0, cap_hh + 2.0)
	var h := _capsule_trace(actor_loc, room_front, cap_r, cap_hh, VISIBILITY_MASK)
	if h["hit"] or h.get("initial", false):
		r["has_front_ledge"] = false
		return [true, false]
	r["obstacle_height"] = absf(((actor_loc - Vector3(0, 0, cap_hh)) - r["front_ledge_location"]).z)
	var room_back: Vector3 = r["back_ledge_location"] + r["back_ledge_normal"] * (cap_r + 2.0) + Vector3(0, 0, cap_hh + 2.0)
	var top := _capsule_trace(room_front, room_back, cap_r, cap_hh, VISIBILITY_MASK)
	if not top["hit"]:
		r["obstacle_depth"] = UE.size_2d(r["front_ledge_location"] - r["back_ledge_location"])
		var floor_end: Vector3 = r["back_ledge_location"] + r["back_ledge_normal"] * (cap_r + 2.0) - Vector3(0, 0, 50)
		var fl := _capsule_trace(room_back, floor_end, cap_r, cap_hh, VISIBILITY_MASK)
		if fl["hit"]:
			r["has_back_floor"] = true
			r["back_floor_location"] = fl["point"]
			r["back_ledge_height"] = absf((fl["point"] - r["back_ledge_location"]).z)
		else:
			r["has_back_floor"] = false
	else:
		r["obstacle_depth"] = UE.size_2d(top["point"] - r["front_ledge_location"])
		r["has_back_ledge"] = false
	# then_3: CHT_TraversalAnims
	var ctx := {"ActionType": r["action"], "HasFrontLedge": r["has_front_ledge"], "HasBackLedge": r["has_back_ledge"],
		"HasBackFloor": r["has_back_floor"], "ObstacleHeight": r["obstacle_height"], "ObstacleDepth": r["obstacle_depth"],
		"BackLedgeHeight": r["back_ledge_height"], "MovementMode": _ue_movement_mode(), "Gait": Gait, "Speed": UE.size_2d(ue_velocity)}
	var valid := traversal_chooser.evaluate(ctx)
	r["action"] = int(traversal_chooser.outputs.get("ActionType", 0))
	r["valid_montages"] = valid
	last_traversal_check = r
	_debug_traversal(r)
	# then_4
	if r["action"] == ETraversalAction.NONE:
		return [true, false]
	var interaction_rot := UE.quat_from_z(r["front_ledge_normal"])
	interaction_pos_ue = r["front_ledge_location"]
	interaction_rot_ue = interaction_rot
	has_interaction = true
	var mm: Dictionary = anim.motion_match_montages(valid, r["front_ledge_location"], interaction_rot)
	if mm.is_empty():
		push_warning("Failed To Find Montage!")
		return [false, true]
	r["montage"] = mm["montage"]
	r["start_time"] = mm["time"]
	r["play_rate"] = mm["play_rate"]
	traversal_result = r
	_perform_traversal_action()
	return [false, false]

func _update_warp_targets() -> void:
	var r := traversal_result
	anim.set_warp_target("FrontLedge", r["front_ledge_location"] + Vector3(0, 0, 0.5), UE.quat_from_x(-r["front_ledge_normal"]))
	if r["action"] == ETraversalAction.HURDLE or r["action"] == ETraversalAction.VAULT:
		var w: Array = anim.get_montage_warp_windows(r["montage"], "BackLedge")
		if not w.is_empty():
			animated_dist_front_to_back_ledge = anim.get_montage_curve(r["montage"], "Distance_From_Ledge", w[0][1])
			anim.set_warp_target("BackLedge", r["back_ledge_location"], Quaternion.IDENTITY)
		else:
			anim.remove_warp_target("BackLedge")
	else:
		anim.remove_warp_target("BackLedge")
	if r["action"] == ETraversalAction.HURDLE:
		var w2: Array = anim.get_montage_warp_windows(r["montage"], "BackFloor")
		if not w2.is_empty():
			animated_dist_front_to_back_floor = anim.get_montage_curve(r["montage"], "Distance_From_Ledge", w2[0][1])
			var p: Vector3 = r["back_ledge_location"] + r["back_ledge_normal"] * absf(animated_dist_front_to_back_ledge - animated_dist_front_to_back_floor)
			anim.set_warp_target("BackFloor", Vector3(p.x, p.y, r["back_floor_location"].z), Quaternion.IDENTITY)
		else:
			anim.remove_warp_target("BackFloor")
	else:
		anim.remove_warp_target("BackFloor")

func _perform_traversal_action() -> void:
	_update_warp_targets()
	for inst in extra_instances:
		(inst["fields"]["anim"] as GaspAnimator).montage_play(traversal_result["montage"], traversal_result["play_rate"], traversal_result["start_time"])
	if anim.montage_play(traversal_result["montage"], traversal_result["play_rate"], traversal_result["start_time"]):
		doing_traversal = true
		montage_playing = true
		var hc = traversal_result.get("hit_component")
		if hc is PhysicsBody3D:
			add_collision_exception_with(hc)
		is_flying = true
		is_falling = false

## PlayMontageCallbackProxy OnCompleted / OnInterrupted
func _on_traversal_montage_ended() -> void:
	doing_traversal = false
	var hc = traversal_result.get("hit_component")
	if hc is PhysicsBody3D and is_instance_valid(hc):
		remove_collision_exception_with(hc)
	is_flying = false
	# OnCompleted: SetMovementMode(Vault ? Falling : Walking); entering Walking finds the floor right away
	is_falling = traversal_result.get("action", 0) == ETraversalAction.VAULT
	if not is_falling and not _find_floor_and_snap(_actor_location_ue()):
		is_falling = true

func _tick_montage(dt: float) -> Dictionary:
	if not montage_playing:
		return {}
	var comp := _component_xform()
	var owner_state := {"mesh_pos": comp[1], "mesh_rot": comp[0], "actor_pos": _actor_location_ue(), "actor_rot": UE.quat_from_yaw(actor_yaw),
		"visual_root": _actor_location_ue() - Vector3(0, 0, CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT),
		"has_acceleration": not ue_acceleration.is_zero_approx(), "is_falling": is_falling}
	var res: Dictionary = anim.montage_tick(dt, owner_state)
	montage_playing = res.get("playing", false)
	# PlayMontageCallbackProxy: OnInterrupted fires when an interrupted montage starts blending out (early blend out
	# notify state -> Montage_StopWithBlendSettings), OnCompleted when an uninterrupted one has finished blending out
	if doing_traversal and (res.get("ended", false) or (res.get("blend_out_started", false) and res.get("interrupted", false))):
		_on_traversal_montage_ended()
	return res

# ------------------------------------------------------------------------------------------------
# Debug visualisation driven by the Game Animation Widget console buttons

func _bone_world_ue(b: int) -> Vector3:
	return UE.from_godot_pos(skeleton.global_transform * skeleton.get_bone_global_pose(b).origin)

func _debug_draw() -> void:
	var loc := _actor_location_ue()
	if use_experimental_state_machine and CVars.get_b("DDCVar.ExperimentalStateMachine.Debug"):
		var ro: Transform3D = anim.get_root_offset_ue()
		sm.debug_draw(_component_xform()[1], UE.quat_to_rotator(ro.basis.get_rotation_quaternion()).y, _max_speed())
	if CVars.get_b("show collision"):
		DebugDraw.capsule(loc, (CROUCHED_HALF_HEIGHT if is_crouched else CAPSULE_HALF_HEIGHT), CAPSULE_RADIUS, Color(1.0, 0.6, 0.0))
	if CVars.get_b("p.VisualizeMovement"):
		DebugDraw.arrow(loc, loc + ue_velocity * 0.25, Color(0.0, 1.0, 0.0), 10.0)
		DebugDraw.arrow(loc + Vector3(0, 0, 5), loc + Vector3(0, 0, 5) + ue_acceleration * 0.1, Color(1.0, 1.0, 0.0), 10.0)
	if CVars.get_b("show bones") and skeleton:
		for b in skeleton.get_bone_count():
			var parent := skeleton.get_bone_parent(b)
			if parent >= 0:
				DebugDraw.line(_bone_world_ue(parent), _bone_world_ue(b), Color(1.0, 1.0, 1.0, 0.9))
	if CVars.get_b("a.AnimNode.FootPlacement.Debug") and CVars.get_b("a.AnimNode.FootPlacement.Enable"):
		var fp: Dictionary = anim.get_foot_placement_debug()
		for l in fp["legs"]:
			var col: Color = [Color(0, 1, 0), Color(1, 0, 0), Color(1, 0.5, 0)][int(l["plant"])]
			DebugDraw.sphere(l["input"], 4.0, Color(0, 0, 1), 0.0, 6)
			DebugDraw.sphere(l["output"], 6.0, col, 0.0, 8)
			DebugDraw.line(l["input"], l["output"], col)
		DebugDraw.sphere(fp["pelvis_in"], 6.0, Color(0, 0, 1), 0.0, 8)
		DebugDraw.sphere(fp["pelvis_out"], 8.0, Color(0, 1, 0), 0.0, 8)
	if CVars.get_b("a.AnimNode.PoseHistory.DebugDrawTrajectory"):
		for i in traj.num():
			var p: Vector3 = traj.pos[i]
			var col := Color(0.0, 0.6, 1.0) if traj.secs[i] > 0.0 else Color(1.0, 0.3, 0.0)
			DebugDraw.sphere(p, 3.0, col, 0.0, 6)
			if i > 0:
				DebugDraw.line(traj.pos[i - 1], p, col)
			DebugDraw.line(p, p + traj.facing[i] * Vector3(20, 0, 0), col)

func _debug_foley(notifies: Array) -> void:
	if not CVars.get_b("DDCvar.DrawVisLogShapesForFoleySounds") or skeleton == null:
		return
	for n in notifies:
		var cls := String(n.get("class", ""))
		if not cls.contains("Foley"):
			continue
		var bone := "foot_l" if cls.ends_with("_L_C") else ("foot_r" if cls.ends_with("_R_C") else "")
		var p := _actor_location_ue()
		if bone != "":
			var bi := skeleton.find_bone(bone)
			if bi >= 0:
				p = _bone_world_ue(bi)
		DebugDraw.sphere(p, 10.0, Color(0.0, 1.0, 1.0), 0.25)

func _debug_traversal(r: Dictionary) -> void:
	var level := CVars.get_i("DDCvar.Traversal.DrawDebugLevel")
	if level < 1:
		return
	var dur := CVars.get_f("DDCvar.Traversal.DrawDebugDuration")
	if r.get("has_front_ledge", false):
		DebugDraw.sphere(r["front_ledge_location"], 10.0, Color(0, 1, 0), dur)
		if r.get("has_back_ledge", false):
			DebugDraw.sphere(r["back_ledge_location"], 10.0, Color(0, 0.64, 1), dur)
		if r.get("has_back_floor", false):
			DebugDraw.sphere(r["back_floor_location"], 10.0, Color(1, 0.5, 0), dur)
	if level >= 2:
		print("Traversal: action=%s height=%.1f depth=%.1f back_ledge_height=%.1f" % [r.get("action", 0), r.get("obstacle_height", 0.0), r.get("obstacle_depth", 0.0), r.get("back_ledge_height", 0.0)])
