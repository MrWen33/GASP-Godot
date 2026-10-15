#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "uemath.h"

namespace gasp {

using ue::Quat;
using ue::Vec3;
using ue::Xform;

// Pose of one skeletal mesh in UE space (cm, UE quaternions) with an FCSPose-like component space cache.
struct PPPose {
	std::vector<std::string> Names;
	std::vector<int> Parents;
	std::vector<Xform> RefLocal, RefCS;
	std::vector<Xform> Local, CS;
	bool bCSDirty = true;
	std::unordered_map<std::string, int> Index;

	int Find(const std::string& N) const { auto it = Index.find(N); return it == Index.end() ? -1 : it->second; }
	void UpdateCS() {
		if (!bCSDirty) return;
		for (size_t b = 0; b < Local.size(); ++b) CS[b] = Parents[b] < 0 ? Local[b] : Local[b] * CS[Parents[b]];
		bCSDirty = false;
	}
	const Xform& GetCS(int b) { UpdateCS(); return CS[b]; }
	// FCSPose::SafeSetCSBoneTransforms: listed bones get the component transform, other bones keep their local transform
	void SetCSBatch(const std::vector<std::pair<int, Xform>>& Sorted);
	void SetCS(int b, const Xform& X) { SetCSBatch({ { b, X } }); }
};

// curve name -> value (morph targets / material curves)
struct PPCurves {
	std::vector<std::string> Names;
	std::vector<float> Values;
	std::unordered_map<std::string, int> Index;
	int Add(const std::string& N) {
		auto it = Index.find(N);
		if (it != Index.end()) return it->second;
		Index[N] = (int)Names.size();
		Names.push_back(N);
		Values.push_back(0.f);
		return (int)Names.size() - 1;
	}
};

struct PPFrame {
	double DeltaTime = 0;
	Xform ComponentWorld;  // mesh component world transform (UE space, no scale)
	bool bTeleport = false;
};

struct PPNode {
	virtual ~PPNode() = default;
	virtual void Evaluate(PPPose& Pose, PPCurves& Curves, const PPFrame& Frame) = 0;
	virtual void Reset() {}
	virtual void DebugBodies(std::vector<std::pair<Xform, Vec3>>& Out) const {}
};

} // namespace gasp

namespace godot {

// Character mesh post-process anim blueprint (ABP_Echo_PostProcess, ABP_Echo_Hair, m_med_nrw_animbp, ...) evaluated on a
// Skeleton3D after retargeting: PoseDriver (RBF), Constraint, ported Control Rig ops and RigidBody (AnimDynamics-style
// immediate physics on the physics asset bodies). Data: data/post_process/<ABP>.json (tools/build/build_post_process.py).
class GaspPostProcess : public RefCounted {
	GDCLASS(GaspPostProcess, RefCounted);

protected:
	static void _bind_methods();

public:
	bool setup(const Dictionary& program, Skeleton3D* skeleton);
	// component_location: mesh component world location (UE cm); component_yaw: its world yaw (degrees)
	void update(Skeleton3D* skeleton, double delta, const Vector3& component_location, double component_yaw);
	PackedStringArray get_curve_names() const;
	PackedFloat32Array get_curve_values() const;
	void reset();
	void set_physics_debug_flags(int flags);
	Array get_debug_joints() const;
	// simulated / kinematic physics bodies for debug drawing: [{ "xf": Transform3D (UE CS -> godot skeleton space), "shape": Vector3(radius, half_length, simulated) }]
	Array get_debug_bodies() const;

private:
	gasp::PPPose Pose;
	gasp::PPCurves Curves;
	std::vector<std::unique_ptr<gasp::PPNode>> Nodes;
	bool bReady = false;
	bool bHasPrev = false;
	gasp::Xform PrevComponent;
};

} // namespace godot
