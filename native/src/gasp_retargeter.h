#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "retarget.h"

namespace godot {

class GaspAnimator;

// FAnimNode_RetargetPoseFromMesh driven by ABP_GenericRetarget: copies the GaspAnimator (UEFN mannequin) pose onto
// another skeleton through an IK Retargeter (data/retarget/RTG_*.json).
class GaspRetargeter : public RefCounted {
	GDCLASS(GaspRetargeter, RefCounted);

protected:
	static void _bind_methods();

public:
	bool setup(const Dictionary& rtg, Skeleton3D* target);
	void update(const Ref<GaspAnimator>& animator, Skeleton3D* target);
	Dictionary get_ik_goals() const;
	void set_post_process_scales(const Dictionary& scales);

private:
	gasp::RetargetProcessor Processor;
	gasp::SkeletonDef SourceDef, TargetDef;
	std::vector<int> TargetToSkeleton;  // target def index -> Skeleton3D bone index
	std::vector<ue::Xform> SourceCS, LocalOut;
	std::vector<std::pair<int, ue::Vec3>> PostScales;
	bool bReady = false;
	int CurveHandL = -1, CurveHandR = -1;
};

} // namespace godot
