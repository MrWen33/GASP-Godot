class_name GaspTrajectory
## Port of UPoseSearchTrajectoryLibrary::PoseSearchGenerateTrajectory + HandleTrajectoryWorldCollisions
## (UE 5.5). All values are in UE space (cm, UE quaternions).

const MIN_TICK_TIME := 1e-6
const BRAKE_TO_STOP_VELOCITY := 10.0

var secs: PackedFloat32Array = PackedFloat32Array()
var pos: Array[Vector3] = []
var facing: Array[Quaternion] = []
var desired_controller_yaw_last_update := 0.0

# collision results
var time_to_land := 0.0
var land_speed := 0.0

class Derived:
	var max_speed := 0.0
	var braking_deceleration := 0.0
	var braking_sub_step_time := 1.0 / 33.0
	var orient_rotation_to_movement := false
	var velocity := Vector3.ZERO
	var acceleration := Vector3.ZERO
	var step_ground_prediction := true
	var friction := 0.0
	var controller_yaw_rate := 0.0
	var position := Vector3.ZERO
	var mesh_comp_relative_rotation := Quaternion.IDENTITY
	var facing := Quaternion.IDENTITY

func num() -> int:
	return secs.size()

func sample_at(t: float, extrapolate := false) -> Array:
	var n := secs.size()
	if n > 1:
		# Algo::LowerBound(Samples, t, Value > Sample.AccumulatedSeconds)
		var lb := 0
		while lb < n and t > secs[lb]:
			lb += 1
		var next_i := clampi(lb, 1, n - 1)
		var prev_i := next_i - 1
		var den := secs[next_i] - secs[prev_i]
		if not is_zero_approx(den):
			var a := (t - secs[prev_i]) / den
			if not extrapolate:
				a = clampf(a, 0.0, 1.0)
			# FPoseSearchQueryTrajectorySample::Lerp (FastLerp facing)
			var f := _fast_lerp(facing[prev_i], facing[next_i], a)
			return [pos[prev_i].lerp(pos[next_i], a), f, lerpf(secs[prev_i], secs[next_i], a)]
		return [pos[prev_i], facing[prev_i], secs[prev_i]]
	if n > 0:
		return [pos[0], facing[0], secs[0]]
	return [Vector3.ZERO, Quaternion.IDENTITY, 0.0]

static func _fast_lerp(a: Quaternion, b: Quaternion, t: float) -> Quaternion:
	var bias := 1.0 if a.dot(b) >= 0.0 else -1.0
	var r := Quaternion(b.x * t * bias + a.x * (1.0 - t), b.y * t * bias + a.y * (1.0 - t), b.z * t * bias + a.z * (1.0 - t), b.w * t * bias + a.w * (1.0 - t))
	return r.normalized()

func velocity(t1: float, t2: float) -> Vector3:
	if is_equal_approx(t1, t2):
		return Vector3.ZERO
	return (sample_at(t2)[0] - sample_at(t1)[0]) / (t2 - t1)

## FPoseSearchTrajectoryData::StepCharacterMovementGroundPrediction
static func step_ground_prediction(dt: float, in_vel: Vector3, in_acc: Vector3, d: Derived) -> Vector3:
	var out := in_vel
	if in_acc.is_zero_approx():
		if in_vel.is_zero_approx():
			return Vector3.ZERO
		var zero_friction := d.friction == 0.0
		var zero_braking := d.braking_deceleration == 0.0
		if zero_friction and zero_braking:
			return in_vel
		var remaining := dt
		var max_step := clampf(d.braking_sub_step_time, 1.0 / 75.0, 1.0 / 20.0)
		var prev := out
		var rev := Vector3.ZERO if zero_braking else -d.braking_deceleration * UE.safe_normal(out)
		while remaining >= MIN_TICK_TIME:
			var step := minf(max_step, remaining * 0.5) if (remaining > max_step and not zero_friction) else remaining
			remaining -= step
			out = out + ((-d.friction) * out + rev) * step
			if out.dot(prev) <= 0.0:
				return Vector3.ZERO
		var s2 := out.length_squared()
		if s2 <= UE.KINDA_SMALL or (not zero_braking and s2 <= BRAKE_TO_STOP_VELOCITY * BRAKE_TO_STOP_VELOCITY):
			out = Vector3.ZERO
	else:
		var acc_dir := UE.safe_normal(in_acc)
		var vs := out.length()
		out = out - (out - acc_dir * vs) * minf(dt * d.friction, 1.0)
		out += in_acc * dt
		out = UE.clamped_to_max_size(out, d.max_speed)
	return out

## PoseSearchGenerateTrajectory (history interval, history count, prediction interval, prediction count)
func generate(d: Derived, dt: float, history_interval: float, history_count: int, prediction_interval: float, prediction_count: int, rotate_towards_movement_speed: float) -> void:
	# InitTrajectorySamples
	var total := history_count + 1 + prediction_count
	if secs.size() != total:
		secs.resize(total); pos.resize(total); facing.resize(total)
		var sph := maxf(history_interval, 0.0)
		for i in history_count:
			pos[i] = d.position; facing[i] = d.facing
			secs[i] = sph * (i - history_count - 1)
		var spp := maxf(prediction_interval, 0.0)
		for i in range(history_count, total):
			pos[i] = d.position; facing[i] = d.facing
			secs[i] = spp * (i - history_count) + dt
	# UpdateHistory_TransformHistory
	if history_count > 0:
		var predicted: Vector3 = sample_at(dt)[0]
		for i in history_count:
			pos[i] = predicted - pos[i]
		var cur_t := d.velocity * dt
		if history_interval <= 0.0 or absf(secs[history_count - 1]) >= history_interval:
			for i in history_count - 1:
				secs[i] = secs[i + 1] - dt
				pos[i] = pos[i + 1] + cur_t
				facing[i] = facing[i + 1]
			secs[history_count - 1] = 0.0
			pos[history_count - 1] = cur_t
			facing[history_count - 1] = facing[history_count]
		else:
			for i in history_count:
				secs[i] -= dt
				pos[i] += cur_t
		for i in history_count:
			pos[i] = d.position - pos[i]
	# UpdatePrediction_SimulateCharacterMovement
	var cur_pos := d.position
	var cur_vel := d.velocity
	var cur_acc := d.acceleration
	var cur_facing := d.facing
	var ctrl_step := UE.quat_from_rotator(0.0, d.controller_yaw_rate * prediction_interval, 0.0)
	var acc_secs := dt
	var last := total - 1
	var i := history_count
	while i <= last:
		pos[i] = cur_pos; facing[i] = cur_facing; secs[i] = acc_secs
		if i == last:
			break
		cur_pos += cur_vel * prediction_interval
		acc_secs += prediction_interval
		if d.step_ground_prediction:
			cur_acc = ctrl_step * cur_acc
			cur_vel = step_ground_prediction(prediction_interval, cur_vel, cur_acc, d)
			cur_facing = ctrl_step * cur_facing
			if d.orient_rotation_to_movement and not cur_acc.is_zero_approx():
				var acc_cs := d.mesh_comp_relative_rotation * cur_acc
				cur_facing = UE.qinterp_constant_to(cur_facing, UE.vec_to_orientation_quat(acc_cs), prediction_interval, rotate_towards_movement_speed)
		i += 1

## HandleTrajectoryWorldCollisionsWithGravity. trace(start_ue, end_ue) -> Dictionary {hit, point, normal} in UE space
func handle_world_collisions(starting_velocity: Vector3, gravity_z: float, floor_offset: float, max_obstacle_height: float, trace: Callable) -> void:
	var n := secs.size()
	var grav_dir := Vector3.ZERO
	var g := 0.0
	var initial_vz := starting_velocity.z
	if not is_zero_approx(gravity_z):
		var acc := Vector3(0, 0, gravity_z)
		g = acc.length(); grav_dir = acc / g
		g = -g
		var on_axis := grav_dir * starting_velocity.dot(grav_dir)
		initial_vz = on_axis.length() * -signf(grav_dir.dot(on_axis))
	time_to_land = secs[n - 1]
	if not is_zero_approx(g):
		var last_impact_valid := false
		var first_fall := true
		var gravity := grav_dir * -g
		var free_fall := 0.0
		for si in range(1, n):
			if secs[si] > 0.0:
				var pi := si - 1
				free_fall += secs[si] - secs[pi]
				if last_impact_valid:
					# PointPlaneProject onto plane through previous sample with normal -grav_dir
					var p := pos[si]; var o := pos[pi]; var nrm := -grav_dir
					pos[si] = p - nrm * (p - o).dot(nrm)
				pos[si] += gravity * (0.5 * free_fall * free_fall)
				if floor_offset > 0.0:
					var h: Dictionary = trace.call(pos[si] + grav_dir * -max_obstacle_height, pos[si])
					if h.get("hit", false):
						# FindClosestPointOnLine(ImpactPoint, Sample.Position, GravityDirection)
						var ip: Vector3 = h["point"]
						var lp := pos[si] + grav_dir * (ip - pos[si]).dot(grav_dir)
						last_impact_valid = true
						pos[si] = lp - grav_dir * floor_offset
						if first_fall:
							var initial_h: float = sample_at(0.0)[0].z
							var fall_h := absf(pos[si].z - initial_h)
							first_fall = false
							time_to_land = (initial_vz / -g) + (sqrt(initial_vz * initial_vz + (2.0 * -g * fall_h)) / -g)
							land_speed = initial_vz + g * time_to_land
						free_fall = 0.0
	land_speed = initial_vz + g * time_to_land

func to_packed() -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(secs.size() * 8)
	for i in secs.size():
		var o := i * 8
		out[o] = secs[i]
		out[o + 1] = pos[i].x; out[o + 2] = pos[i].y; out[o + 3] = pos[i].z
		out[o + 4] = facing[i].x; out[o + 5] = facing[i].y; out[o + 6] = facing[i].z; out[o + 7] = facing[i].w
	return out
