// Port of UIKRetargetProcessor (UE 5.5 IKRig plugin) for FAnimNode_RetargetPoseFromMesh: root retarget, FK chains
// (Interpolated / OneToOne / OneToOneReversed / None), IK goals solved by the target IK Rig (Full Body IK = pbik),
// pole vector matching. Stride warping / speed planting / post ops are not used by GASP's retargeters.
#pragma once
#include "pbik.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace gasp {
using namespace ue;

enum class ERotationMode { Interpolated, OneToOne, OneToOneReversed, None };
enum class ETranslationMode { None, GloballyScaled, Absolute };

struct FKSettings {
	bool EnableFK = true;
	ERotationMode RotationMode = ERotationMode::Interpolated;
	double RotationAlpha = 1.0;
	ETranslationMode TranslationMode = ETranslationMode::None;
	double TranslationAlpha = 1.0;
	double PoleVectorMatching = 0.0;
	bool PoleVectorMaintainOffset = false;
	double PoleVectorOffset = 0.0;
};
struct IKSettings {
	bool EnableIK = true;
	double BlendToSource = 0.0;
	Vec3 BlendToSourceWeights = Vec3(1, 1, 1);
	Vec3 StaticOffset, StaticLocalOffset;
	Quat StaticRotationOffset;
	double ScaleVertical = 1.0, Extension = 1.0;
};
struct RootSettings {
	double RotationAlpha = 1, TranslationAlpha = 1, BlendToSource = 0;
	Vec3 BlendToSourceWeights = Vec3(1, 1, 1);
	double ScaleHorizontal = 1, ScaleVertical = 1;
	Vec3 TranslationOffset;
	Quat RotationOffset;
	double AffectIKHorizontal = 1, AffectIKVertical = 0;
	Vec3 GetAffectIKWeightVector() const { return Vec3(AffectIKHorizontal, AffectIKHorizontal, AffectIKVertical); }
};
struct BoneChainDef { std::string Name, Start, End, Goal; };
struct RetargetPoseDef { Vec3 RootOffset; std::vector<std::pair<std::string, Quat>> Rotations; };
struct IKRigEffectorDef { std::string Goal, Bone; int ChainDepth = 0; float Strength = 1, PullChain = 1, PinRotation = 1; };
struct IKRigBoneSettingDef { std::string Bone; pbik::BoneSettings S; };
struct IKRigDef {
	std::string RetargetRoot;
	std::vector<BoneChainDef> Chains;
	std::unordered_map<std::string, std::string> GoalBones;
	bool bHasSolver = false;
	std::string SolverRoot;
	pbik::SolverSettings Solver;
	std::vector<IKRigEffectorDef> Effectors;
	std::vector<IKRigBoneSettingDef> BoneSettings;
	std::vector<std::string> Excluded;
};
struct ChainMapDef { std::string Source, Target; FKSettings FK; IKSettings IK; };
struct RetargeterDef {
	IKRigDef SourceRig, TargetRig;
	RootSettings Root;
	bool bEnableRoot = true, bEnableFK = true, bEnableIK = true;
	std::vector<ChainMapDef> Chains;
	RetargetPoseDef SourcePose, TargetPose;
};

struct SkeletonDef { std::vector<std::string> Names; std::vector<int> Parents; std::vector<Xform> RefLocal; };

class RetargetSkeleton {
public:
	std::vector<std::string> BoneNames;
	std::vector<int> ParentIndices;
	std::vector<Xform> RetargetLocalPose, RetargetGlobalPose;
	void Initialize(const SkeletonDef& Skel, const RetargetPoseDef& Pose, const std::string& RetargetRootBone);
	int FindBoneIndexByName(const std::string& Name) const;
	int GetParentIndex(int B) const { return (B < 0 || B >= (int)ParentIndices.size()) ? -1 : ParentIndices[B]; }
	void UpdateGlobalTransformsBelowBone(int Start, const std::vector<Xform>& Local, std::vector<Xform>& Global) const;
	void UpdateGlobalTransformOfSingleBone(int B, const std::vector<Xform>& Local, std::vector<Xform>& Global) const;
	void UpdateLocalTransformOfSingleBone(int B, std::vector<Xform>& Local, const std::vector<Xform>& Global) const;
	Xform GetGlobalRetargetPoseOfSingleBone(int B, const std::vector<Xform>& Global) const;
	void GetChildrenIndicesRecursive(int B, std::vector<int>& Out) const;
	int GetCachedEndOfBranchIndex(int B) const;
	bool ResolveChain(const BoneChainDef& Chain, std::vector<int>& Out) const;
};

class TargetSkeleton : public RetargetSkeleton {
public:
	std::vector<Xform> OutputGlobalPose;
	std::vector<uint8_t> IsBoneRetargeted;
	void InitializeTarget(const SkeletonDef& Skel, const RetargetPoseDef& Pose, const std::string& Root) {
		Initialize(Skel, Pose, Root);
		IsBoneRetargeted.assign(BoneNames.size(), 0);
		OutputGlobalPose = RetargetGlobalPose;
	}
	void UpdateGlobalTransformsAllNonRetargetedBones(std::vector<Xform>& Global) const {
		for (int b = 0; b < (int)Global.size(); ++b) if (!IsBoneRetargeted[b]) UpdateGlobalTransformOfSingleBone(b, RetargetLocalPose, Global);
	}
};

struct ChainFK {
	std::vector<Xform> InitialGlobalTransforms, InitialLocalTransforms, CurrentGlobalTransforms;
	std::vector<double> Params;
	int ChainParentBoneIndex = -1;
	Xform ChainParentInitialGlobalTransform;
	bool Initialize(const RetargetSkeleton& Skel, const std::vector<int>& Bones, const std::vector<Xform>& InitialGlobalPose);
	Xform GetTransformAtParam(const std::vector<Xform>& T, double Param) const;
	static void FillTransformsWithLocalSpaceOfChain(const RetargetSkeleton& Skel, const std::vector<Xform>& Global, const std::vector<int>& Bones, std::vector<Xform>& Out);
	void PutCurrentTransformsInRefPose(const std::vector<int>& Bones, const RetargetSkeleton& Skel, const std::vector<Xform>& CurrentGlobal);
};
struct ChainEncoderFK : ChainFK {
	std::vector<Xform> CurrentLocalTransforms;
	Xform ChainParentCurrentGlobalTransform;
	void EncodePose(const RetargetSkeleton& Skel, const std::vector<int>& Bones, const std::vector<Xform>& SourceGlobal);
	void TransformCurrentChainTransforms(const Xform& NewParent);
};
struct RootRetargeter;
struct ChainDecoderFK : ChainFK {
	std::vector<int> IntermediateParentIndices;
	void DecodePose(const RootRetargeter& Root, const FKSettings& S, const std::vector<int>& TargetBones, ChainEncoderFK& Source, const TargetSkeleton& Target, std::vector<Xform>& InOutGlobal);
	void InitializeIntermediateParentIndices(int RetargetRootBone, int ChainRootBone, const TargetSkeleton& Target);
};
struct PoleVectorMatcher {
	int SourcePoleAxis = 1, TargetPoleAxis = 1;
	double TargetToSourceAngularOffsetAtRefPose = 0;
	std::vector<int> AllChildrenWithinChain;
	bool Initialize(const std::vector<int>& SourceIdx, const std::vector<int>& TargetIdx, const std::vector<Xform>& SourceGlobal, const std::vector<Xform>& TargetGlobal, const RetargetSkeleton& TargetSkel);
	void MatchPoleVector(const FKSettings& S, const std::vector<int>& SourceIdx, const std::vector<int>& TargetIdx, const std::vector<Xform>& SourceGlobal, std::vector<Xform>& TargetGlobal, const RetargetSkeleton& TargetSkel) const;
	static int CalculateBestPoleAxisForChain(const std::vector<int>& Idx, const std::vector<Xform>& Global);
	static Vec3 CalculatePoleVector(int Axis, const std::vector<int>& Idx, const std::vector<Xform>& Global);
	static Vec3 GetChainAxisNormalized(const std::vector<int>& Idx, const std::vector<Xform>& Global);
};
struct ChainRetargeterIK {
	struct { int StartBoneIndex = -1, EndBoneIndex = -1; Vec3 InitialEndPosition, CurrentEndPosition, CurrentEndDirectionNormalized; Quat InitialEndRotation, CurrentEndRotation; double InvInitialLength = 1; } Source;
	struct { int BoneIndexA = -1, BoneIndexC = -1; Vec3 InitialEndPosition, PrevEndPosition; Quat InitialEndRotation; double InitialLength = 1; } Target;
	struct { Vec3 EndEffectorPosition; Quat EndEffectorRotation; } Results;
	bool InitializeSource(const std::vector<int>& Bones, const std::vector<Xform>& SourceInitialGlobal);
	void EncodePose(const std::vector<Xform>& SourceGlobal);
	bool InitializeTarget(const std::vector<int>& Bones, const std::vector<Xform>& TargetInitialGlobal);
	void DecodePose(const IKSettings& S, const RootRetargeter& Root, const std::vector<Xform>& InGlobal);
};
struct RootRetargeter {
	RootSettings Settings;
	struct { int BoneIndex = -1; Quat InitialRotation, CurrentRotation; double InitialHeightInverse = 1; Vec3 CurrentPosition, CurrentPositionNormalized; } Source;
	struct { int BoneIndex = -1; double InitialHeight = 0; Quat InitialRotation; Vec3 InitialPosition, RootTranslationDelta; Quat RootRotationDelta; } Target;
	Vec3 GlobalScaleFactor = Vec3(1, 1, 1);
	bool InitializeSource(const std::string& Root, const RetargetSkeleton& Skel);
	bool InitializeTarget(const std::string& Root, const TargetSkeleton& Skel);
	void EncodePose(const std::vector<Xform>& SourceGlobal);
	void DecodePose(std::vector<Xform>& OutTargetGlobal);
};

// UIKRigProcessor + UIKRigFBIKSolver for the target rig
class IKRigProcessor {
public:
	bool Initialize(const IKRigDef& Rig, const SkeletonDef& Skel, const std::vector<std::string>& GoalsToExclude);
	void SetGoal(const std::string& Goal, const Vec3& P, const Quat& R);
	void Solve(std::vector<Xform>& InOutGlobal);
	bool bInitialized = false;
private:
	struct Goal { std::string Name; int Bone = -1; Vec3 Position; Quat Rotation; bool bSet = false; };
	std::vector<Goal> Goals;
	struct Eff { int Index = -1; std::string Goal; pbik::EffectorSettings S; };
	std::vector<Eff> Effectors;
	std::vector<std::pair<int, pbik::BoneSettings>> BoneSettings;
	pbik::Solver Solver;
	pbik::SolverSettings Settings;
	int NumBones = 0;
};

class RetargetProcessor {
public:
	bool Initialize(const SkeletonDef& Source, const SkeletonDef& Target, const RetargeterDef& Def);
	// InSourceGlobalPose: component space, scale stripped. Returns target component-space pose.
	const std::vector<Xform>& RunRetargeter(const std::vector<Xform>& InSourceGlobalPose);
	// runtime chain setting override (ABP_GenericRetarget profile)
	ChainMapDef* FindChainSettings(const std::string& TargetChain);
	const ChainMapDef* FindAssetChainSettings(const std::string& TargetChain) const;
	bool IsInitialized() const { return bIsInitialized; }
	const TargetSkeleton& GetTarget() const { return TargetSkel; }
private:
	struct PairFK { std::string SourceName, TargetName; std::vector<int> SourceBones, TargetBones; ChainEncoderFK Encoder; ChainDecoderFK Decoder; PoleVectorMatcher Pole; FKSettings Settings; };
	struct PairIK { std::string SourceName, TargetName, Goal; std::vector<int> SourceBones, TargetBones; ChainRetargeterIK IK; IKSettings Settings; };
	RetargeterDef Def;
	std::vector<ChainMapDef> RuntimeChains;
	RetargetSkeleton SourceSkel;
	TargetSkeleton TargetSkel;
	RootRetargeter Root;
	std::vector<PairFK> ChainPairsFK;
	std::vector<PairIK> ChainPairsIK;
	IKRigProcessor IKRig;
	bool bRootsInitialized = false, bAtLeastOneValidBoneChainPair = false, bIKRigInitialized = false, bIsInitialized = false;
	void ApplyRuntimeSettings();
};

} // namespace gasp
