#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

namespace godot {

// Render-rate pose interpolation. Animation is evaluated on 60 Hz physics ticks while the body transform is drawn with
// physics interpolation; UE instead evaluates the pose every rendered frame. Without this, fast in-pose motion (e.g. the
// pelvis dropping ~8 cm per tick during a hurdle) is shown as 60 Hz steps on a high refresh display while the world and
// the character's transform move smoothly.
//   capture(): after a physics tick wrote the pose -> previous = current, current = skeleton pose
//   apply(f):  in _process, writes lerp/slerp(previous, current, Engine.get_physics_interpolation_fraction())
//   reset():   next capture also becomes the previous pose (teleports, character swaps)
class GaspPoseInterpolator : public RefCounted {
	GDCLASS(GaspPoseInterpolator, RefCounted);

protected:
	static void _bind_methods();

public:
	void capture(Skeleton3D* skeleton);
	void apply(Skeleton3D* skeleton, double fraction);
	void reset() { bReset = true; }

private:
	struct BonePose {
		Vector3 Position;
		Quaternion Rotation;
		Vector3 Scale;
	};
	std::vector<BonePose> Prev, Cur;
	bool bReset = true;
};

} // namespace godot
