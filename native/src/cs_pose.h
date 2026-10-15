// FCSPose-like helper: local pose + component space cache. Setting a component-space transform keeps
// children's local transforms (so the subtree follows), matching UE FCSPose semantics.
#pragma once
#include "anim_lib.h"

namespace gasp {

struct CSPose {
	std::vector<Xform> Local;
	std::vector<Xform> CS;
	const std::vector<int>* Parents = nullptr;

	void Init(const std::vector<Xform>& InLocal) {
		Parents = &AnimLibrary::Get().Parents;
		Local = InLocal;
		CS.resize(Local.size());
		for (int b = 0; b < (int)Local.size(); ++b) {
			const int P = (*Parents)[b];
			CS[b] = P < 0 ? Local[b] : Local[b] * CS[P];
		}
	}
	const Xform& Get(int b) const { return CS[b]; }
	void Set(int b, const Xform& X) {
		const int P = (*Parents)[b];
		CS[b] = X;
		Local[b] = P < 0 ? X : X.GetRelativeTransform(CS[P]);
		// recompute descendants (bones are ordered parent-first)
		for (int c = b + 1; c < (int)Local.size(); ++c) {
			const int PC = (*Parents)[c];
			if (PC >= b && IsDescendant(c, b)) CS[c] = Local[c] * CS[PC];
		}
	}
	bool IsDescendant(int c, int b) const {
		int p = (*Parents)[c];
		while (p >= 0) { if (p == b) return true; p = (*Parents)[p]; }
		return false;
	}
	// set several bones at once from CS (like SafeSetCSBoneTransforms with sorted transforms)
	void ToLocal(std::vector<Xform>& Out) const { Out = Local; }
};

} // namespace gasp

namespace gasp {
// FAnimNode_ModifyBone (ScaleMode BMM_Replace, ScaleSpace component) chain applied to a local pose. As in FCSPose, setting a
// bone's component transform only changes that bone's local transform; children keep theirs (and inherit the new scale).
inline void ApplyComponentSpaceScaleReplace(std::vector<Xform>& Local, const std::vector<int>& Parents, const std::vector<std::pair<int, Vec3>>& Scales) {
	if (Scales.empty()) return;
	const int N = (int)Local.size();
	std::vector<Xform> CS(N);
	for (const auto& S : Scales) {
		const int B = S.first;
		if (B < 0 || B >= N) continue;
		for (int b = 0; b <= B; ++b) CS[b] = Parents[b] < 0 ? Local[b] : Local[b] * CS[Parents[b]];
		Xform T = CS[B];
		T.Scale3D = S.second;
		Local[B] = Parents[B] < 0 ? T : T.GetRelativeTransform(CS[Parents[B]]);
	}
}
} // namespace gasp
