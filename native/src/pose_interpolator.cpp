#include "pose_interpolator.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void GaspPoseInterpolator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("capture", "skeleton"), &GaspPoseInterpolator::capture);
	ClassDB::bind_method(D_METHOD("apply", "skeleton", "fraction"), &GaspPoseInterpolator::apply);
	ClassDB::bind_method(D_METHOD("reset"), &GaspPoseInterpolator::reset);
}

void GaspPoseInterpolator::capture(Skeleton3D* skeleton) {
	if (!skeleton) return;
	const int N = skeleton->get_bone_count();
	if ((int)Cur.size() != N) bReset = true;
	Prev.swap(Cur);
	Cur.resize(N);
	for (int b = 0; b < N; ++b) {
		Cur[b].Position = skeleton->get_bone_pose_position(b);
		Cur[b].Rotation = skeleton->get_bone_pose_rotation(b);
		Cur[b].Scale = skeleton->get_bone_pose_scale(b);
	}
	if (bReset || (int)Prev.size() != N) {
		Prev = Cur;
		bReset = false;
	}
}

void GaspPoseInterpolator::apply(Skeleton3D* skeleton, double fraction) {
	if (!skeleton) return;
	const int N = skeleton->get_bone_count();
	if ((int)Cur.size() != N || (int)Prev.size() != N) return;
	const real_t F = (real_t)CLAMP(fraction, 0.0, 1.0);
	for (int b = 0; b < N; ++b) {
		const BonePose& A = Prev[b];
		const BonePose& B = Cur[b];
		skeleton->set_bone_pose_position(b, A.Position.lerp(B.Position, F));
		skeleton->set_bone_pose_rotation(b, A.Rotation.slerp(B.Rotation, F).normalized());
		skeleton->set_bone_pose_scale(b, A.Scale.lerp(B.Scale, F));
	}
}

} // namespace godot
