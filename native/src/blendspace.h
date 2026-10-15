// UBlendSpace sampling (triangulation / segments, target weight smoothing incl. per-bone overrides)
// and mesh-space rotation additive application (FAnimationRuntime).
#pragma once
#include "anim_lib.h"
#include <array>

namespace gasp {

struct BlendSpaceAsset {
	std::string Name;
	struct Sample { int Anim = -1; double X = 0, Y = 0; };
	std::vector<Sample> Samples;
	std::vector<std::array<int, 3>> Triangles;
	std::vector<std::array<int, 2>> Segments;
	double Min[2] = { 0, 0 }, Max[2] = { 1, 1 };
	int NumDims = 1;
	float Speed = 0.f;
	std::vector<int> PerBoneIndex;   // bone -> per-bone override index or -1
	std::vector<float> PerBoneSpeed; // per override
};

class BlendSpaceLibrary {
public:
	static BlendSpaceLibrary& Get();
	bool Load(const std::string& DataDir);
	int Find(const std::string& Name) const;
	const BlendSpaceAsset& Asset(int i) const { return Assets[i]; }
	// AO blend curve (FRichCurve with cubic keys)
	struct CurveKey { double T, V, Arrive, Leave; };
	std::vector<CurveKey> AOBlendCurve;
	double EvalAOBlendCurve(double T) const;
	// cached additive deltas (mesh space rotation offset) at time 0
	const std::vector<Xform>& AdditiveDelta(int Anim);
private:
	std::vector<BlendSpaceAsset> Assets;
	std::unordered_map<int, std::vector<Xform>> DeltaCache;
};

struct BlendSampleData {
	int Index = -1;
	float TotalWeight = 0, WeightRate = 0;
	std::vector<float> PerBone, PerBoneRate;
};

class BlendSpacePlayer {
public:
	int AssetIdx = -1;
	std::vector<BlendSampleData> Cache;
	void Reset() { Cache.clear(); }
	void Update(double X, double Y, float DeltaTime);
	// weighted blend of the additive sample deltas
	void EvaluateAdditive(std::vector<Xform>& Out) const;
private:
	void GetTargetWeights(double X, double Y, std::vector<BlendSampleData>& Out) const;
};

void ConvertPoseToMeshRotation(std::vector<Xform>& Pose);
void ConvertMeshRotationPoseToLocalSpace(std::vector<Xform>& Pose);
// FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToLocalPose
void AccumulateMeshSpaceRotationAdditive(std::vector<Xform>& Base, const std::vector<Xform>& Additive, float Weight);
void MakeIdentityAdditive(std::vector<Xform>& Out);

} // namespace gasp
