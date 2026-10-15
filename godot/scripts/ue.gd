class_name UE
## Unreal-space helpers. Vectors are UE (x fwd, y right, z up, cm); quaternions use UE axes.
## Godot's Quaternion/Vector3 math is used as plain math on UE-space values (Hamilton product,
## q*v*q^-1 rotation), which matches FQuat semantics. Only conversions to Godot space touch axes.

const KINDA_SMALL := 1e-4
const SMALL := 1e-8

static func to_godot_pos(v: Vector3) -> Vector3:
	return Vector3(v.x, v.z, v.y) * 0.01

static func from_godot_pos(v: Vector3) -> Vector3:
	return Vector3(v.x, v.z, v.y) * 100.0

static func to_godot_dir(v: Vector3) -> Vector3:
	return Vector3(v.x, v.z, v.y)

static func from_godot_dir(v: Vector3) -> Vector3:
	return Vector3(v.x, v.z, v.y)

static func to_godot_quat(q: Quaternion) -> Quaternion:
	return Quaternion(-q.x, -q.z, -q.y, q.w)

static func from_godot_quat(q: Quaternion) -> Quaternion:
	return Quaternion(-q.x, -q.z, -q.y, q.w)

static func to_godot_xform(q: Quaternion, t: Vector3) -> Transform3D:
	return Transform3D(Basis(to_godot_quat(q).normalized()), to_godot_pos(t))

## FRotator(Pitch, Yaw, Roll).Quaternion()
static func quat_from_rotator(pitch: float, yaw: float, roll: float) -> Quaternion:
	var dr := PI / 360.0
	var sp := sin(pitch * dr); var cp := cos(pitch * dr)
	var sy := sin(yaw * dr); var cy := cos(yaw * dr)
	var sr := sin(roll * dr); var cr := cos(roll * dr)
	return Quaternion(
		cr * sp * sy - sr * cp * cy,
		-cr * sp * cy - sr * cp * sy,
		cr * cp * sy - sr * sp * cy,
		cr * cp * cy + sr * sp * sy)

static func quat_from_yaw(yaw: float) -> Quaternion:
	var h := deg_to_rad(yaw) * 0.5
	return Quaternion(0, 0, sin(h), cos(h))

## FQuat::Rotator() -> Vector3(pitch, yaw, roll) in degrees
static func quat_to_rotator(q: Quaternion) -> Vector3:
	var singularity := q.z * q.x - q.w * q.y
	var yaw_y := 2.0 * (q.w * q.z + q.x * q.y)
	var yaw_x := 1.0 - 2.0 * (q.y * q.y + q.z * q.z)
	var thr := 0.4999995
	var r2d := 180.0 / PI
	var pitch: float; var yaw: float; var roll: float
	if singularity < -thr:
		pitch = -90.0
		yaw = atan2(yaw_y, yaw_x) * r2d
		roll = normalize_axis(-yaw - (2.0 * atan2(q.x, q.w) * r2d))
	elif singularity > thr:
		pitch = 90.0
		yaw = atan2(yaw_y, yaw_x) * r2d
		roll = normalize_axis(yaw - (2.0 * atan2(q.x, q.w) * r2d))
	else:
		pitch = asin(2.0 * singularity) * r2d
		yaw = atan2(yaw_y, yaw_x) * r2d
		roll = atan2(-2.0 * (q.w * q.x + q.y * q.z), (1.0 - 2.0 * (q.x * q.x + q.y * q.y))) * r2d
	return Vector3(pitch, yaw, roll)

static func quat_yaw(q: Quaternion) -> float:
	return quat_to_rotator(q).y

static func normalize_axis(a: float) -> float:
	a = fmod(a, 360.0)
	if a < 0.0:
		a += 360.0
	if a > 180.0:
		a -= 360.0
	return a

## FVector::ToOrientationQuat (yaw/pitch from direction, roll 0)
static func vec_to_orientation_quat(v: Vector3) -> Quaternion:
	var yaw := rad_to_deg(atan2(v.y, v.x))
	var pitch := rad_to_deg(atan2(v.z, sqrt(v.x * v.x + v.y * v.y)))
	return quat_from_rotator(pitch, yaw, 0.0)

static func vec_to_rotator_yaw(v: Vector3) -> float:
	return rad_to_deg(atan2(v.y, v.x))

static func size_2d(v: Vector3) -> float:
	return sqrt(v.x * v.x + v.y * v.y)

static func safe_normal(v: Vector3, tol := SMALL) -> Vector3:
	var s := v.length_squared()
	if s == 1.0:
		return v
	if s < tol:
		return Vector3.ZERO
	return v / sqrt(s)

static func clamped_to_max_size(v: Vector3, m: float) -> Vector3:
	if m < KINDA_SMALL:
		return Vector3.ZERO
	var s := v.length_squared()
	if s > m * m:
		return v * (m / sqrt(s))
	return v

static func map_range_clamped(v: float, a0: float, a1: float, b0: float, b1: float) -> float:
	if is_equal_approx(a0, a1):
		return b0 if v < a0 else b1
	var t := clampf((v - a0) / (a1 - a0), 0.0, 1.0)
	return lerpf(b0, b1, t)

## UKismetAnimationLibrary::CalculateDirection
static func calculate_direction(velocity: Vector3, base_rot: Quaternion) -> float:
	if velocity.is_zero_approx():
		return 0.0
	var fwd := base_rot * Vector3(1, 0, 0)
	var right := base_rot * Vector3(0, 1, 0)
	var nv := safe_normal(Vector3(velocity.x, velocity.y, 0.0)) # FVector::GetSafeNormal2D
	var deg := rad_to_deg(acos(clampf(fwd.dot(nv), -1.0, 1.0)))
	if right.dot(nv) < 0.0:
		deg = -deg
	return deg

## NormalizedDeltaRotator(A, B).Yaw
static func delta_yaw(a_yaw: float, b_yaw: float) -> float:
	return normalize_axis(a_yaw - b_yaw)

## FMath::QInterpConstantTo
static func qinterp_constant_to(current: Quaternion, target: Quaternion, dt: float, speed: float) -> Quaternion:
	if speed <= 0.0:
		return target
	var delta_rad := maxf(SMALL, current.angle_to(target))
	var alpha := clampf(clampf(speed * dt, 0.0, 1.0) / delta_rad, 0.0, 1.0)
	return current.slerp(target, alpha).normalized()

## piecewise-linear curve over [[t, v], ...] (FRichCurve with linear keys, constant extrapolation)
static func eval_linear_curve(keys: Array, t: float) -> float:
	if keys.is_empty():
		return 0.0
	if t <= keys[0][0]:
		return keys[0][1]
	for i in range(1, keys.size()):
		if t <= keys[i][0]:
			var a = keys[i - 1]; var b = keys[i]
			return lerpf(a[1], b[1], (t - a[0]) / (b[0] - a[0]))
	return keys[keys.size() - 1][1]

## FRotationMatrix::MakeFromZ(Z).ToQuat()
static func quat_from_z(z: Vector3) -> Quaternion:
	var nz := z.normalized()
	var up := Vector3(0, 0, 1) if absf(nz.z) < (1.0 - 1e-4) else Vector3(1, 0, 0)
	var nx := up.cross(nz).normalized()
	var ny := nz.cross(nx)
	return Quaternion(Basis(nx, ny, nz)).normalized()

## FRotationMatrix::MakeFromX(X).ToQuat()
static func quat_from_x(x: Vector3) -> Quaternion:
	var nx := x.normalized()
	var up := Vector3(0, 0, 1) if absf(nx.z) < (1.0 - 1e-4) else Vector3(1, 0, 0)
	var ny := up.cross(nx).normalized()
	var nz := nx.cross(ny)
	return Quaternion(Basis(nx, ny, nz)).normalized()

## closest point on segment [a, b] to p, and distance along the segment
static func closest_on_segment(a: Vector3, b: Vector3, p: Vector3) -> Array:
	var ab := b - a
	var l2 := ab.length_squared()
	var t := 0.0 if l2 < 1e-8 else clampf((p - a).dot(ab) / l2, 0.0, 1.0)
	return [a + ab * t, t * sqrt(l2)]

## FMath::RInterpTo for a yaw-only rotator
static func rinterp_to_yaw(current: float, target: float, dt: float, speed: float) -> float:
	if dt == 0.0 or current == target:
		return current
	if speed <= 0.0:
		return target
	var delta := normalize_axis(target - current)
	if absf(delta) <= 1e-8:
		return target
	return normalize_axis(current + delta * clampf(dt * speed, 0.0, 1.0))

## FRotator(0, yaw, 0).Vector()
static func yaw_to_vector(yaw: float) -> Vector3:
	return Vector3(cos(deg_to_rad(yaw)), sin(deg_to_rad(yaw)), 0.0)
