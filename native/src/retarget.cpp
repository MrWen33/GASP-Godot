#include "retarget.h"
#include <algorithm>
#include <cctype>

namespace gasp {

static std::string Lower(const std::string& S) { std::string R = S; for (char& c : R) c = (char)std::tolower((unsigned char)c); return R; }
static bool NameEq(const std::string& A, const std::string& B) { return Lower(A) == Lower(B); }
static bool IsNone(const std::string& S) { return S.empty() || NameEq(S, "None") || S == "- None -"; }

// ------------------------------------------------------------------ skeleton
void RetargetSkeleton::Initialize(const SkeletonDef& Skel, const RetargetPoseDef& Pose, const std::string& RetargetRootBone) {
	BoneNames = Skel.Names;
	ParentIndices = Skel.Parents;
	// GenerateRetargetPose
	RetargetLocalPose = Skel.RefLocal;
	RetargetGlobalPose = RetargetLocalPose;
	UpdateGlobalTransformsBelowBone(-1, RetargetLocalPose, RetargetGlobalPose);
	for (size_t b = 0; b < BoneNames.size(); ++b) { RetargetLocalPose[b].Scale3D = Vec3(1, 1, 1); RetargetGlobalPose[b].Scale3D = Vec3(1, 1, 1); }
	if (IsNone(RetargetRootBone)) return;
	const int RootBoneIndex = FindBoneIndexByName(RetargetRootBone);
	if (RootBoneIndex != -1) {
		RetargetGlobalPose[RootBoneIndex].Translation += Pose.RootOffset;
		UpdateLocalTransformOfSingleBone(RootBoneIndex, RetargetLocalPose, RetargetGlobalPose);
	}
	for (const auto& Delta : Pose.Rotations) {
		const int B = FindBoneIndexByName(Delta.first);
		if (B == -1) continue;
		RetargetLocalPose[B].Rotation = Skel.RefLocal[B].Rotation * Delta.second;
	}
	UpdateGlobalTransformsBelowBone(-1, RetargetLocalPose, RetargetGlobalPose);
}

int RetargetSkeleton::FindBoneIndexByName(const std::string& Name) const {
	for (int i = 0; i < (int)BoneNames.size(); ++i) if (NameEq(BoneNames[i], Name)) return i;
	return -1;
}

void RetargetSkeleton::UpdateGlobalTransformsBelowBone(int Start, const std::vector<Xform>& Local, std::vector<Xform>& Global) const {
	for (int b = Start + 1; b < (int)Global.size(); ++b) UpdateGlobalTransformOfSingleBone(b, Local, Global);
}

void RetargetSkeleton::UpdateGlobalTransformOfSingleBone(int B, const std::vector<Xform>& Local, std::vector<Xform>& Global) const {
	const int P = ParentIndices[B];
	Global[B] = P == -1 ? Local[B] : Local[B] * Global[P];
}

void RetargetSkeleton::UpdateLocalTransformOfSingleBone(int B, std::vector<Xform>& Local, const std::vector<Xform>& Global) const {
	const int P = ParentIndices[B];
	Local[B] = P == -1 ? Global[B] : Global[B].GetRelativeTransform(Global[P]);
}

Xform RetargetSkeleton::GetGlobalRetargetPoseOfSingleBone(int B, const std::vector<Xform>& Global) const {
	const int P = ParentIndices[B];
	return P == -1 ? RetargetLocalPose[B] : RetargetLocalPose[B] * Global[P];
}

int RetargetSkeleton::GetCachedEndOfBranchIndex(int B) const {
	const int NumBones = (int)BoneNames.size();
	if (B < 0 || B >= NumBones) return -1;
	if (B == 0 || B + 1 >= NumBones) return NumBones - 1;
	const int StartParent = GetParentIndex(B);
	int Bone = B + 1, Parent = GetParentIndex(Bone);
	int End = -2;
	while (Parent > StartParent) {
		End = Bone;
		Bone++;
		if (Bone >= NumBones) break;
		Parent = GetParentIndex(Bone);
	}
	return End == -2 ? -1 : End;
}

void RetargetSkeleton::GetChildrenIndicesRecursive(int B, std::vector<int>& Out) const {
	const int Last = GetCachedEndOfBranchIndex(B);
	if (Last == -1) return;
	for (int c = B + 1; c <= Last; ++c) Out.push_back(c);
}

bool RetargetSkeleton::ResolveChain(const BoneChainDef& Chain, std::vector<int>& Out) const {
	Out.clear();
	const int StartIndex = FindBoneIndexByName(Chain.Start), EndIndex = FindBoneIndexByName(Chain.End);
	if (StartIndex < 0 || EndIndex < 0 || EndIndex < StartIndex) return false;
	Out = { EndIndex };
	if (EndIndex == StartIndex) return true;
	int P = GetParentIndex(EndIndex);
	while (P > -1 && P >= StartIndex) { Out.push_back(P); P = GetParentIndex(P); }
	if (Out.back() == StartIndex) { std::reverse(Out.begin(), Out.end()); return true; }
	Out.clear();
	return false;
}

// ------------------------------------------------------------------ FK chains
bool ChainFK::Initialize(const RetargetSkeleton& Skel, const std::vector<int>& Bones, const std::vector<Xform>& InitialGlobalPose) {
	InitialGlobalTransforms.clear();
	for (int B : Bones) InitialGlobalTransforms.push_back(InitialGlobalPose[B]);
	CurrentGlobalTransforms = InitialGlobalTransforms;
	InitialLocalTransforms.resize(InitialGlobalTransforms.size());
	FillTransformsWithLocalSpaceOfChain(Skel, InitialGlobalPose, Bones, InitialLocalTransforms);
	ChainParentBoneIndex = Skel.GetParentIndex(Bones[0]);
	ChainParentInitialGlobalTransform = ChainParentBoneIndex != -1 ? InitialGlobalPose[ChainParentBoneIndex] : Xform();
	// CalculateBoneParameters
	Params.clear();
	if (InitialGlobalTransforms.size() == 1) { Params.push_back(1.0); return true; }
	std::vector<double> Dist = { 0.0 };
	double Total = 0;
	for (size_t i = 1; i < InitialGlobalTransforms.size(); ++i) {
		Total += (InitialGlobalTransforms[i].Translation - InitialGlobalTransforms[i - 1].Translation).Size();
		Dist.push_back(Total);
	}
	if (Total <= KINDA_SMALL_NUMBER) return false;
	for (size_t i = 0; i < InitialGlobalTransforms.size(); ++i) Params.push_back(Dist[i] / Total);
	return true;
}

Xform ChainFK::GetTransformAtParam(const std::vector<Xform>& T, double Param) const {
	if (Params.size() == 1) return T[0];
	if (Param < KINDA_SMALL_NUMBER) return T[0];
	if (Param > 1.0 - KINDA_SMALL_NUMBER) return T.back();
	for (size_t i = 1; i < Params.size(); ++i) {
		const double Cur = Params[i];
		if (Cur <= Param) continue;
		const double Prev = Params[i - 1];
		const double Pct = (Param - Prev) / (Cur - Prev);
		const Xform& A = T[i - 1];
		const Xform& B = T[i];
		return Xform(Quat::FastLerp(A.Rotation, B.Rotation, Pct).GetNormalized(), Vec3::Lerp(A.Translation, B.Translation, Pct), Vec3::Lerp(A.Scale3D, B.Scale3D, Pct));
	}
	return Xform();
}

void ChainFK::FillTransformsWithLocalSpaceOfChain(const RetargetSkeleton& Skel, const std::vector<Xform>& Global, const std::vector<int>& Bones, std::vector<Xform>& Out) {
	for (size_t i = 0; i < Bones.size(); ++i) {
		const int B = Bones[i], P = Skel.GetParentIndex(B);
		Out[i] = P == -1 ? Global[B] : Global[B].GetRelativeTransform(Global[P]);
	}
}

void ChainFK::PutCurrentTransformsInRefPose(const std::vector<int>& Bones, const RetargetSkeleton& Skel, const std::vector<Xform>& CurrentGlobal) {
	for (size_t i = 0; i < Bones.size(); ++i) {
		if (i == 0) CurrentGlobalTransforms[i] = Skel.GetGlobalRetargetPoseOfSingleBone(Bones[i], CurrentGlobal);
		else CurrentGlobalTransforms[i] = Skel.RetargetLocalPose[Bones[i]] * CurrentGlobalTransforms[i - 1];
	}
}

void ChainEncoderFK::EncodePose(const RetargetSkeleton& Skel, const std::vector<int>& Bones, const std::vector<Xform>& SourceGlobal) {
	for (size_t i = 0; i < Bones.size(); ++i) CurrentGlobalTransforms[i] = SourceGlobal[Bones[i]];
	CurrentLocalTransforms.resize(Bones.size());
	FillTransformsWithLocalSpaceOfChain(Skel, SourceGlobal, Bones, CurrentLocalTransforms);
	ChainParentCurrentGlobalTransform = ChainParentBoneIndex != -1 ? SourceGlobal[ChainParentBoneIndex] : Xform();
}

void ChainEncoderFK::TransformCurrentChainTransforms(const Xform& NewParent) {
	for (size_t i = 0; i < CurrentGlobalTransforms.size(); ++i)
		CurrentGlobalTransforms[i] = CurrentLocalTransforms[i] * (i == 0 ? NewParent : CurrentGlobalTransforms[i - 1]);
}

void ChainDecoderFK::InitializeIntermediateParentIndices(int RetargetRootBone, int ChainRootBone, const TargetSkeleton& Target) {
	IntermediateParentIndices.clear();
	int P = Target.ParentIndices[ChainRootBone];
	while (true) {
		if (P < 0 || P == RetargetRootBone) break;
		if (Target.IsBoneRetargeted[P]) break;
		IntermediateParentIndices.push_back(P);
		P = Target.ParentIndices[P];
	}
	std::reverse(IntermediateParentIndices.begin(), IntermediateParentIndices.end());
}

void ChainDecoderFK::DecodePose(const RootRetargeter& Root, const FKSettings& S, const std::vector<int>& TargetBones, ChainEncoderFK& Source, const TargetSkeleton& Target, std::vector<Xform>& InOutGlobal) {
	for (int P : IntermediateParentIndices) Target.UpdateGlobalTransformOfSingleBone(P, Target.RetargetLocalPose, InOutGlobal);
	const Xform SourceChainParentInitialDelta = Source.ChainParentInitialGlobalTransform.GetRelativeTransform(ChainParentInitialGlobalTransform);
	const Xform TargetChainParentCurrent = ChainParentBoneIndex == -1 ? Xform() : InOutGlobal[ChainParentBoneIndex];
	Source.TransformCurrentChainTransforms(SourceChainParentInitialDelta * TargetChainParentCurrent);
	if (!S.EnableFK) {
		PutCurrentTransformsInRefPose(TargetBones, Target, InOutGlobal);
		for (size_t i = 0; i < TargetBones.size(); ++i) InOutGlobal[TargetBones[i]] = CurrentGlobalTransforms[i];
		return;
	}
	const int NumSource = (int)Source.CurrentGlobalTransforms.size();
	const int NumTarget = (int)TargetBones.size();
	const int TargetStartIndex = std::max(0, NumTarget - NumSource);
	const int SourceStartIndex = std::max(0, NumSource - NumTarget);
	for (int i = 0; i < NumTarget; ++i) {
		const int B = TargetBones[i];
		const Xform& TargetInitial = InitialGlobalTransforms[i];
		Xform SourceCurrent, SourceInitial;
		switch (S.RotationMode) {
		case ERotationMode::Interpolated:
			SourceCurrent = Source.GetTransformAtParam(Source.CurrentGlobalTransforms, Params[i]);
			SourceInitial = Source.GetTransformAtParam(Source.InitialGlobalTransforms, Params[i]);
			break;
		case ERotationMode::OneToOne:
			if (i < NumSource) { SourceCurrent = Source.CurrentGlobalTransforms[i]; SourceInitial = Source.InitialGlobalTransforms[i]; }
			else { SourceCurrent = Source.CurrentGlobalTransforms.back(); SourceInitial = Source.InitialGlobalTransforms.back(); }
			break;
		case ERotationMode::OneToOneReversed:
			if (i < TargetStartIndex) { SourceCurrent = Source.InitialGlobalTransforms[0]; SourceInitial = Source.InitialGlobalTransforms[0]; }
			else {
				const int SI = SourceStartIndex + (i - TargetStartIndex);
				SourceCurrent = Source.CurrentGlobalTransforms[SI]; SourceInitial = Source.InitialGlobalTransforms[SI];
			}
			break;
		case ERotationMode::None:
			SourceInitial = Source.InitialGlobalTransforms[0];
			SourceCurrent = Source.InitialLocalTransforms[0] * Source.ChainParentCurrentGlobalTransform;
			break;
		}
		const Quat RotationDelta = SourceCurrent.Rotation * SourceInitial.Rotation.Inverse();
		const Quat OutRotation = RotationDelta * TargetInitial.Rotation;
		const int P = Target.ParentIndices[B];
		const Xform ParentGlobal = P != -1 ? InOutGlobal[P] : Xform();
		Vec3 OutPosition;
		switch (S.TranslationMode) {
		case ETranslationMode::None: OutPosition = ParentGlobal.TransformPosition(Target.RetargetLocalPose[B].Translation); break;
		case ETranslationMode::GloballyScaled: OutPosition = SourceCurrent.Translation * Root.GlobalScaleFactor; break;
		case ETranslationMode::Absolute: OutPosition = SourceCurrent.Translation; break;
		}
		const Vec3 OutScale = SourceCurrent.Scale3D + (TargetInitial.Scale3D - SourceInitial.Scale3D);
		CurrentGlobalTransforms[i] = Xform(OutRotation, OutPosition, OutScale);
		InOutGlobal[B] = CurrentGlobalTransforms[i];
	}
	const bool bBlendRot = std::abs(S.RotationAlpha - 1.0) > SMALL_NUMBER;
	const bool bBlendTrans = std::abs(S.TranslationAlpha - 1.0) > SMALL_NUMBER;
	if (bBlendRot || bBlendTrans) {
		std::vector<Xform> NewLocal(InitialLocalTransforms.size());
		FillTransformsWithLocalSpaceOfChain(Target, InOutGlobal, TargetBones, NewLocal);
		for (size_t i = 0; i < InitialLocalTransforms.size(); ++i) {
			Xform& L = NewLocal[i];
			const Xform& Ref = InitialLocalTransforms[i];
			L.Translation = Vec3::Lerp(Ref.Translation, L.Translation, S.TranslationAlpha);
			L.Rotation = Quat::FastLerp(Ref.Rotation, L.Rotation, S.RotationAlpha).GetNormalized();
			const int B = TargetBones[i], P = Target.ParentIndices[B];
			InOutGlobal[B] = L * (P == -1 ? Xform() : InOutGlobal[P]);
		}
	}
}

// ------------------------------------------------------------------ pole vector matching
static Vec3 UnitAxis(const Xform& T, int Axis) { return T.GetUnitAxis(Axis); }

int PoleVectorMatcher::CalculateBestPoleAxisForChain(const std::vector<int>& Idx, const std::vector<Xform>& Global) {
	if (Idx.size() == 1) return 1;
	const Vec3 Origin = Global[Idx[0]].Translation;
	const Vec3 N = (Global[Idx.back()].Translation - Origin).GetSafeNormal();
	double MostDifferentDot = 2.0;
	int Best = 1;
	for (int Axis = 0; Axis < 3; ++Axis) {
		const double D = std::abs(UnitAxis(Global[Idx[0]], Axis) | N);
		if (D < MostDifferentDot) { MostDifferentDot = D; Best = Axis; }
	}
	return Best;
}

Vec3 PoleVectorMatcher::GetChainAxisNormalized(const std::vector<int>& Idx, const std::vector<Xform>& Global) {
	return (Global[Idx.back()].Translation - Global[Idx[0]].Translation).GetSafeNormal();
}

Vec3 PoleVectorMatcher::CalculatePoleVector(int Axis, const std::vector<int>& Idx, const std::vector<Xform>& Global) {
	const Vec3 ChainNormal = GetChainAxisNormalized(Idx, Global);
	return Vec3::VectorPlaneProject(UnitAxis(Global[Idx[0]], Axis), ChainNormal).GetSafeNormal();
}

bool PoleVectorMatcher::Initialize(const std::vector<int>& SourceIdx, const std::vector<int>& TargetIdx, const std::vector<Xform>& SourceGlobal, const std::vector<Xform>& TargetGlobal, const RetargetSkeleton& TargetSkel) {
	SourcePoleAxis = CalculateBestPoleAxisForChain(SourceIdx, SourceGlobal);
	TargetPoleAxis = CalculateBestPoleAxisForChain(TargetIdx, TargetGlobal);
	const Vec3 SP = CalculatePoleVector(SourcePoleAxis, SourceIdx, SourceGlobal);
	const Vec3 TP = CalculatePoleVector(TargetPoleAxis, TargetIdx, TargetGlobal);
	TargetToSourceAngularOffsetAtRefPose = std::acos(Clamp(TP | SP, -1.0, 1.0));
	std::vector<int> All;
	TargetSkel.GetChildrenIndicesRecursive(TargetIdx[0], All);
	std::vector<int> ChildrenOfEnd = { TargetIdx.back() };
	TargetSkel.GetChildrenIndicesRecursive(TargetIdx.back(), ChildrenOfEnd);
	AllChildrenWithinChain.clear();
	for (int C : All) if (std::find(ChildrenOfEnd.begin(), ChildrenOfEnd.end(), C) == ChildrenOfEnd.end()) AllChildrenWithinChain.push_back(C);
	return true;
}

void PoleVectorMatcher::MatchPoleVector(const FKSettings& S, const std::vector<int>& SourceIdx, const std::vector<int>& TargetIdx, const std::vector<Xform>& SourceGlobal, std::vector<Xform>& TargetGlobal, const RetargetSkeleton& TargetSkel) const {
	const bool bMatching = S.PoleVectorMatching > KINDA_SMALL_NUMBER;
	const bool bOffsetting = !IsNearlyZero(S.PoleVectorOffset);
	if (!(bMatching || bOffsetting)) return;
	std::vector<Xform> LocalSpaces;
	for (int C : AllChildrenWithinChain) {
		const int P = TargetSkel.ParentIndices[C];
		LocalSpaces.push_back(P == -1 ? TargetGlobal[C] : TargetGlobal[C].GetRelativeTransform(TargetGlobal[P]));
	}
	const Vec3 TargetChainAxisNorm = GetChainAxisNormalized(TargetIdx, TargetGlobal);
	Quat Matching;
	if (bMatching) {
		const Vec3 SP = CalculatePoleVector(SourcePoleAxis, SourceIdx, SourceGlobal);
		const Vec3 TP = CalculatePoleVector(TargetPoleAxis, TargetIdx, TargetGlobal);
		const double RotateTargetToSource = std::acos(Clamp(SP | TP, -1.0, 1.0));
		const double MatchPoleAngle = RotateTargetToSource - (S.PoleVectorMaintainOffset ? TargetToSourceAngularOffsetAtRefPose : 0);
		Matching = Quat(TargetChainAxisNorm, MatchPoleAngle);
		Matching = Quat::FastLerp(Quat(), Matching, S.PoleVectorMatching).GetNormalized();
	}
	Quat Offset;
	if (bOffsetting) Offset = Quat(TargetChainAxisNorm, DegToRad(S.PoleVectorOffset));
	Xform& Base = TargetGlobal[TargetIdx[0]];
	Base.Rotation = Matching * Offset * Base.Rotation;
	for (size_t i = 0; i < AllChildrenWithinChain.size(); ++i) {
		const int C = AllChildrenWithinChain[i], P = TargetSkel.ParentIndices[C];
		if (P == -1) { TargetGlobal[C] = LocalSpaces[i]; return; }
		TargetGlobal[C] = LocalSpaces[i] * TargetGlobal[P];
	}
}

// ------------------------------------------------------------------ IK chains
bool ChainRetargeterIK::InitializeSource(const std::vector<int>& Bones, const std::vector<Xform>& G) {
	if (Bones.size() < 2) return false;
	Source.StartBoneIndex = Bones[0];
	Source.EndBoneIndex = Bones.back();
	const Xform& End = G[Source.EndBoneIndex];
	Source.InitialEndPosition = End.Translation;
	Source.InitialEndRotation = End.Rotation;
	const double Length = (G[Source.StartBoneIndex].Translation - Source.InitialEndPosition).Size();
	if (Length <= KINDA_SMALL_NUMBER) return false;
	Source.InvInitialLength = 1.0 / Length;
	return true;
}

void ChainRetargeterIK::EncodePose(const std::vector<Xform>& G) {
	const Vec3 Start = G[Source.StartBoneIndex].Translation;
	const Vec3 End = G[Source.EndBoneIndex].Translation;
	const Vec3 ChainVector = End - Start;
	const double ChainLength = ChainVector.Size();
	const Vec3 ChainDirection = ChainLength > SMALL_NUMBER ? ChainVector * (1.0 / ChainLength) : Vec3();
	Source.CurrentEndPosition = End;
	Source.CurrentEndDirectionNormalized = ChainDirection * (ChainLength * Source.InvInitialLength);
	Source.CurrentEndRotation = G[Source.EndBoneIndex].Rotation;
}

bool ChainRetargeterIK::InitializeTarget(const std::vector<int>& Bones, const std::vector<Xform>& G) {
	if (Bones.size() < 3) return false;
	Target.BoneIndexA = Bones[0];
	Target.BoneIndexC = Bones.back();
	const Xform& Last = G[Bones.back()];
	Target.PrevEndPosition = Last.Translation;
	Target.InitialEndPosition = Last.Translation;
	Target.InitialEndRotation = Last.Rotation;
	Target.InitialLength = (G[Target.BoneIndexA].Translation - Last.Translation).Size();
	return Target.InitialLength > KINDA_SMALL_NUMBER;
}

void ChainRetargeterIK::DecodePose(const IKSettings& S, const RootRetargeter& Root, const std::vector<Xform>& G) {
	const Quat DeltaRotation = Source.CurrentEndRotation * Source.InitialEndRotation.Inverse();
	Quat GoalRotation = DeltaRotation * Target.InitialEndRotation;
	GoalRotation = GoalRotation * S.StaticRotationOffset;
	const Vec3 InvAffect = Vec3(1, 1, 1) - Root.Settings.GetAffectIKWeightVector();
	const Vec3 InvRootModification = Root.Target.RootTranslationDelta * InvAffect;
	const Vec3 Start = G[Target.BoneIndexA].Translation - InvRootModification;
	Vec3 GoalPosition = Start + Source.CurrentEndDirectionNormalized * Target.InitialLength;
	if (S.BlendToSource > KINDA_SMALL_NUMBER) {
		const Vec3 RootModification = Root.Target.RootTranslationDelta * Root.Settings.GetAffectIKWeightVector();
		const Vec3 W = S.BlendToSourceWeights * S.BlendToSource;
		const Vec3 SourceLocation = Source.CurrentEndPosition + RootModification;
		GoalPosition.X = GoalPosition.X + (SourceLocation.X - GoalPosition.X) * W.X;
		GoalPosition.Y = GoalPosition.Y + (SourceLocation.Y - GoalPosition.Y) * W.Y;
		GoalPosition.Z = GoalPosition.Z + (SourceLocation.Z - GoalPosition.Z) * W.Z;
	}
	GoalPosition += S.StaticOffset;
	GoalPosition += GoalRotation.RotateVector(S.StaticLocalOffset);
	GoalPosition.Z *= S.ScaleVertical;
	if (std::abs(S.Extension - 1.0) > SMALL_NUMBER) GoalPosition = Start + (GoalPosition - Start) * S.Extension;
	Results.EndEffectorPosition = GoalPosition;
	Results.EndEffectorRotation = GoalRotation;
	Target.PrevEndPosition = GoalPosition;
}

// ------------------------------------------------------------------ root
bool RootRetargeter::InitializeSource(const std::string& RootName, const RetargetSkeleton& Skel) {
	Source.BoneIndex = Skel.FindBoneIndexByName(RootName);
	if (Source.BoneIndex == -1) return false;
	const Xform& Init = Skel.RetargetGlobalPose[Source.BoneIndex];
	double InitialHeight = Init.Translation.Z;
	Source.InitialRotation = Init.Rotation;
	if (InitialHeight < KINDA_SMALL_NUMBER) InitialHeight = 1.0;
	Source.InitialHeightInverse = 1.0 / InitialHeight;
	return true;
}

bool RootRetargeter::InitializeTarget(const std::string& RootName, const TargetSkeleton& Skel) {
	Target.BoneIndex = Skel.FindBoneIndexByName(RootName);
	if (Target.BoneIndex == -1) return false;
	const Xform& Init = Skel.RetargetGlobalPose[Target.BoneIndex];
	Target.InitialHeight = Init.Translation.Z;
	Target.InitialRotation = Init.Rotation;
	Target.InitialPosition = Init.Translation;
	const double F = Source.InitialHeightInverse * Target.InitialHeight;
	GlobalScaleFactor = Vec3(F, F, F);
	return true;
}

void RootRetargeter::EncodePose(const std::vector<Xform>& G) {
	const Xform& T = G[Source.BoneIndex];
	Source.CurrentPosition = T.Translation;
	Source.CurrentPositionNormalized = Source.CurrentPosition * Source.InitialHeightInverse;
	Source.CurrentRotation = T.Rotation;
}

void RootRetargeter::DecodePose(std::vector<Xform>& Out) {
	Vec3 Position;
	{
		const Vec3 Retargeted = Source.CurrentPositionNormalized * Target.InitialHeight;
		const Vec3 W = Settings.BlendToSourceWeights * Settings.BlendToSource;
		Position = Vec3(Retargeted.X + (Source.CurrentPosition.X - Retargeted.X) * W.X,
			Retargeted.Y + (Source.CurrentPosition.Y - Retargeted.Y) * W.Y,
			Retargeted.Z + (Source.CurrentPosition.Z - Retargeted.Z) * W.Z);
		Vec3 Scaled = Position;
		Scaled.Z *= Settings.ScaleVertical;
		const Vec3 HorizontalOffset = (Scaled - Target.InitialPosition) * Vec3(Settings.ScaleHorizontal, Settings.ScaleHorizontal, 1.0);
		Position = Target.InitialPosition + HorizontalOffset;
		Position += Settings.TranslationOffset;
		Position = Vec3::Lerp(Target.InitialPosition, Position, Settings.TranslationAlpha);
		Target.RootTranslationDelta = Position - Retargeted;
	}
	Quat Rotation;
	{
		const Quat RotationDelta = Source.CurrentRotation * Source.InitialRotation.Inverse();
		const Quat Retargeted = RotationDelta * Target.InitialRotation;
		Rotation = Retargeted * Settings.RotationOffset;
		Rotation = Quat::FastLerp(Target.InitialRotation, Rotation, Settings.RotationAlpha);
		Rotation.Normalize();
		Target.RootRotationDelta = Retargeted * Target.InitialRotation.Inverse();
	}
	Out[Target.BoneIndex].Translation = Position;
	Out[Target.BoneIndex].Rotation = Rotation;
}

// ------------------------------------------------------------------ IK rig (target) with the FBIK solver
bool IKRigProcessor::Initialize(const IKRigDef& Rig, const SkeletonDef& Skel, const std::vector<std::string>& GoalsToExclude) {
	bInitialized = false;
	Solver.Reset();
	Goals.clear(); Effectors.clear(); BoneSettings.clear();
	if (!Rig.bHasSolver) return false;
	NumBones = (int)Skel.Names.size();
	auto Find = [&](const std::string& N) { for (int i = 0; i < NumBones; ++i) if (NameEq(Skel.Names[i], N)) return i; return -1; };
	auto IsExcluded = [&](int B) { for (const std::string& E : Rig.Excluded) if (NameEq(Skel.Names[B], E)) return true; return false; };
	// RefPoseGlobal (FIKRigSkeleton, scale kept in the local -> global conversion)
	std::vector<Xform> RefGlobal(NumBones);
	for (int b = 0; b < NumBones; ++b) RefGlobal[b] = Skel.Parents[b] == -1 ? Skel.RefLocal[b] : Skel.RefLocal[b] * RefGlobal[Skel.Parents[b]];
	// goals (all goals of the rig that are not excluded)
	for (const auto& G : Rig.GoalBones) {
		bool bEx = false;
		for (const std::string& E : GoalsToExclude) if (NameEq(E, G.first)) bEx = true;
		if (bEx) continue;
		Goal Go; Go.Name = G.first; Go.Bone = Find(G.second);
		if (Go.Bone < 0) continue;
		Go.Position = RefGlobal[Go.Bone].Translation; Go.Rotation = RefGlobal[Go.Bone].Rotation;
		Goals.push_back(Go);
	}
	// UIKRigFBIKSolver::Initialize
	int NumEffectors = 0;
	for (const auto& E : Rig.Effectors) if (Find(E.Bone) != -1) ++NumEffectors;
	if (NumEffectors == 0 || IsNone(Rig.SolverRoot)) return false;
	for (int b = 0; b < NumBones; ++b) {
		int P = Skel.Parents[b];
		while (P != -1 && IsExcluded(P)) P = Skel.Parents[P];
		Solver.AddBone(Skel.Names[b], P, RefGlobal[b].Translation, RefGlobal[b].Rotation, NameEq(Skel.Names[b], Rig.SolverRoot));
	}
	for (const auto& E : Rig.Effectors) {
		bool bEx = false;
		for (const std::string& X : GoalsToExclude) if (NameEq(X, E.Goal)) bEx = true;
		if (bEx) continue;
		Eff F; F.Goal = E.Goal; F.Index = Solver.AddEffector(Skel.Names[std::max(0, Find(E.Bone))]);
		if (Find(E.Bone) < 0) F.Index = -1;
		F.S.StrengthAlpha = E.Strength; F.S.ChainDepth = E.ChainDepth; F.S.PullChainAlpha = E.PullChain; F.S.PinRotation = E.PinRotation;
		Effectors.push_back(F);
	}
	if (!Solver.Initialize()) return false;
	for (const auto& BS : Rig.BoneSettings) {
		const int B = Find(BS.Bone);
		if (B >= 0) BoneSettings.push_back({ B, BS.S });
	}
	Settings = Rig.Solver;
	bInitialized = true;
	return true;
}

void IKRigProcessor::SetGoal(const std::string& Name, const Vec3& P, const Quat& R) {
	for (Goal& G : Goals) if (NameEq(G.Name, Name)) { G.Position = P; G.Rotation = R; G.bSet = true; }
}

void IKRigProcessor::Solve(std::vector<Xform>& InOut) {
	if (!bInitialized || (int)InOut.size() != NumBones) return;
	// ResolveFinalGoalTransforms: component space goals with alpha 1 -> the goal itself
	for (int b = 0; b < NumBones; ++b) Solver.SetBoneTransform(b, InOut[b]);
	for (const auto& BS : BoneSettings)
		if (pbik::BoneSettings* S = Solver.GetBoneSettings(BS.first)) *S = BS.second;
	for (const Eff& E : Effectors) {
		if (E.Index < 0) continue;
		const Goal* G = nullptr;
		for (const Goal& Go : Goals) if (NameEq(Go.Name, E.Goal)) G = &Go;
		if (!G) return;
		Solver.SetEffectorGoal(E.Index, G->Position, G->Rotation, E.S);
	}
	Solver.Solve(Settings);
	for (int b = 0; b < NumBones; ++b) Solver.GetBoneGlobalTransform(b, InOut[b]);
	for (Xform& T : InOut) T.Rotation.Normalize();
}

// ------------------------------------------------------------------ processor
static const BoneChainDef* FindChain(const IKRigDef& Rig, const std::string& Name) {
	for (const BoneChainDef& C : Rig.Chains) if (NameEq(C.Name, Name)) return &C;
	return nullptr;
}

bool RetargetProcessor::Initialize(const SkeletonDef& Source, const SkeletonDef& Target, const RetargeterDef& InDef) {
	Def = InDef;
	RuntimeChains = Def.Chains;
	bIsInitialized = bRootsInitialized = bAtLeastOneValidBoneChainPair = bIKRigInitialized = false;
	SourceSkel.Initialize(Source, Def.SourcePose, Def.SourceRig.RetargetRoot);
	TargetSkel.InitializeTarget(Target, Def.TargetPose, Def.TargetRig.RetargetRoot);
	// roots
	Root = RootRetargeter();
	Root.Settings = Def.Root;
	const bool bSrcRoot = Root.InitializeSource(Def.SourceRig.RetargetRoot, SourceSkel);
	const bool bTgtRoot = bSrcRoot && Root.InitializeTarget(Def.TargetRig.RetargetRoot, TargetSkel);
	bRootsInitialized = bSrcRoot && bTgtRoot;
	// chain pairs
	ChainPairsFK.clear(); ChainPairsIK.clear();
	for (const ChainMapDef& M : Def.Chains) {
		const BoneChainDef* TC = FindChain(Def.TargetRig, M.Target);
		if (!TC || IsNone(M.Source)) continue;
		const BoneChainDef* SC = FindChain(Def.SourceRig, M.Source);
		if (!SC) continue;
		PairFK F; F.SourceName = SC->Name; F.TargetName = TC->Name;
		const bool bValid = SourceSkel.ResolveChain(*SC, F.SourceBones) && TargetSkel.ResolveChain(*TC, F.TargetBones);
		if (bValid && F.Encoder.Initialize(SourceSkel, F.SourceBones, SourceSkel.RetargetGlobalPose)
			&& F.Decoder.Initialize(TargetSkel, F.TargetBones, TargetSkel.RetargetGlobalPose)
			&& F.Pole.Initialize(F.SourceBones, F.TargetBones, SourceSkel.RetargetGlobalPose, TargetSkel.RetargetGlobalPose, TargetSkel)) {
			F.Settings = M.FK;
			ChainPairsFK.push_back(F);
		}
		if (!IsNone(TC->Goal) && bValid) {
			PairIK I; I.SourceName = SC->Name; I.TargetName = TC->Name; I.Goal = TC->Goal;
			I.SourceBones = F.SourceBones; I.TargetBones = F.TargetBones;
			if (I.IK.InitializeSource(I.SourceBones, SourceSkel.RetargetGlobalPose) && I.IK.InitializeTarget(I.TargetBones, TargetSkel.RetargetGlobalPose)) {
				I.Settings = M.IK;
				ChainPairsIK.push_back(I);
			}
		}
	}
	auto Sorter = [](const auto& A, const auto& B) {
		const int IA = A.TargetBones.empty() ? -1 : A.TargetBones[0], IB = B.TargetBones.empty() ? -1 : B.TargetBones[0];
		if (IA == IB) return Lower(A.TargetName) < Lower(B.TargetName);
		return IA < IB;
	};
	std::stable_sort(ChainPairsFK.begin(), ChainPairsFK.end(), Sorter);
	std::stable_sort(ChainPairsIK.begin(), ChainPairsIK.end(), Sorter);
	for (const PairFK& F : ChainPairsFK) for (int B : F.TargetBones) TargetSkel.IsBoneRetargeted[B] = 1;
	for (PairFK& F : ChainPairsFK) F.Decoder.InitializeIntermediateParentIndices(Root.Target.BoneIndex, F.TargetBones[0], TargetSkel);
	if (bRootsInitialized) TargetSkel.IsBoneRetargeted[Root.Target.BoneIndex] = 1;
	bAtLeastOneValidBoneChainPair = !(ChainPairsFK.empty() && ChainPairsIK.empty());
	// IK rig
	std::vector<std::string> GoalsToExclude;
	for (const PairIK& I : ChainPairsIK) if (!I.Settings.EnableIK) GoalsToExclude.push_back(I.Goal);
	bIKRigInitialized = IKRig.Initialize(Def.TargetRig, Target, GoalsToExclude);
	bIsInitialized = bRootsInitialized || bAtLeastOneValidBoneChainPair;
	return bIsInitialized;
}

ChainMapDef* RetargetProcessor::FindChainSettings(const std::string& TargetChain) {
	for (ChainMapDef& C : RuntimeChains) if (NameEq(C.Target, TargetChain)) return &C;
	return nullptr;
}

const ChainMapDef* RetargetProcessor::FindAssetChainSettings(const std::string& TargetChain) const {
	for (const ChainMapDef& C : Def.Chains) if (NameEq(C.Target, TargetChain)) return &C;
	return nullptr;
}

void RetargetProcessor::ApplyRuntimeSettings() {
	for (PairFK& F : ChainPairsFK) for (const ChainMapDef& C : RuntimeChains) if (NameEq(C.Target, F.TargetName)) F.Settings = C.FK;
	for (PairIK& I : ChainPairsIK) for (const ChainMapDef& C : RuntimeChains) if (NameEq(C.Target, I.TargetName)) I.Settings = C.IK;
}

const std::vector<Xform>& RetargetProcessor::RunRetargeter(const std::vector<Xform>& In) {
	ApplyRuntimeSettings();
	std::vector<Xform>& Out = TargetSkel.OutputGlobalPose;
	Out = TargetSkel.RetargetGlobalPose;
	if (Def.bEnableRoot && bRootsInitialized) {
		Root.EncodePose(In);
		Root.DecodePose(Out);
		TargetSkel.UpdateGlobalTransformsBelowBone(Root.Target.BoneIndex, TargetSkel.RetargetLocalPose, Out);
	}
	if (Def.bEnableFK && bAtLeastOneValidBoneChainPair) {
		for (PairFK& F : ChainPairsFK) {
			F.Encoder.EncodePose(SourceSkel, F.SourceBones, In);
			F.Decoder.DecodePose(Root, F.Settings, F.TargetBones, F.Encoder, TargetSkel, Out);
		}
		TargetSkel.UpdateGlobalTransformsAllNonRetargetedBones(Out);
	}
	if (bAtLeastOneValidBoneChainPair && bIKRigInitialized && !ChainPairsIK.empty()) {
		for (PairIK& I : ChainPairsIK) {
			I.IK.EncodePose(In);
			I.IK.DecodePose(I.Settings, Root, Out);
		}
		for (PairIK& I : ChainPairsIK) IKRig.SetGoal(I.Goal, I.IK.Results.EndEffectorPosition, I.IK.Results.EndEffectorRotation);
		if (Def.bEnableIK) IKRig.Solve(Out);
	}
	if (Def.bEnableFK && bAtLeastOneValidBoneChainPair)
		for (PairFK& F : ChainPairsFK) F.Pole.MatchPoleVector(F.Settings, F.SourceBones, F.TargetBones, In, Out, TargetSkel);
	return Out;
}

} // namespace gasp
