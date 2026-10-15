// Montage playback on the DefaultSlot (FAnimMontageInstance subset) + MotionWarping (UMotionWarpingComponent with
// URootMotionModifier_SkewWarp windows) for the traversal montages.
#pragma once
#include "anim_lib.h"
#include <map>

namespace gasp {

struct WarpTargetXf {
	Vec3 Location;
	Quat Rotation;
};

// Character state the warping needs (UMotionWarpingBaseAdapter), all UE world space
struct WarpOwnerState {
	Xform MeshTransform;      // skeletal mesh component world transform (actor * base visual offset)
	Quat ActorQuat;
	Vec3 VisualRootLocation;  // actor location - capsule half height
	Quat MeshRotationOffset;  // GetBaseVisualRotationOffset
	bool bHasAcceleration = false;
	bool bIsFalling = false;
};

class MontagePlayer {
public:
	int Montage = -1;
	int Anim = -1;
	float Position = 0.f, PreviousPosition = 0.f, PlayRate = 1.f;
	bool bPlaying = false;      // instance alive (playing or blending out)
	bool bStopping = false;     // blending out
	bool bEndedThisFrame = false;
	bool bInterrupted = false;
	bool bBlendOutStartedThisFrame = false; // Stop() began the blend out during the last Advance (OnMontageBlendingOut)
	std::vector<FiredNotify> Fired; // notifies triggered during the last Advance

	std::map<std::string, WarpTargetXf> WarpTargets;

	void Play(int MontageIdx, float InPlayRate, float StartPosition);
	void Stop(float BlendTime, BlendOption Option, const AnimLibrary::BlendProfile* Profile);
	void StopImmediately();
	// Advances the instance (weight + position, auto blend out, blend out notify states) and returns the
	// warped root motion delta for this tick in mesh component space.
	Xform Advance(float DeltaTime, const WarpOwnerState& Owner, bool& bOutHasRootMotion);
	// Slot node: blends the montage pose over Source.
	void EvaluateSlot(Pose& Source) const;
	bool IsSlotActive() const { return (bPlaying && Weight > 0.f) || Out.bActive; }
	float GetWeight() const { return Weight; }
	float GetOutgoingWeight() const { return Out.bActive ? Out.Weight : 0.f; }

	void AddOrUpdateWarpTarget(const std::string& Name, const Vec3& Location, const Quat& Rotation) { WarpTargets[Name] = { Location, Rotation }; }
	void RemoveWarpTarget(const std::string& Name) { WarpTargets.erase(Name); }

private:
	// FAlphaBlend
	float BlendBegin = 0.f, BlendDesired = 0.f, BlendTime = 0.f, BlendElapsed = 0.f;
	BlendOption Option = BlendOption::Linear;
	const AnimLibrary::BlendProfile* Profile = nullptr;
	float Weight = 0.f;
	float LinearAlpha = 1.f;

	enum class ModState { Waiting, Active, MarkedForRemoval, Disabled };
	struct Modifier {
		int Window = -1;
		ModState State = ModState::Waiting;
		float PrevPos = 0.f, CurPos = 0.f;
		float ActualStartTime = 0.f;
		Xform StartTransform;
		Xform TotalRootMotionWithinWindow;
		bool bHasCachedOffset = false;
		Xform CachedOffsetFromWarpPoint;
		bool bHasCachedTarget = false;
		Xform CachedTargetTransform;
	};
	std::vector<Modifier> Modifiers;
	std::vector<int> BlendOutStatesActive;

	// UAnimInstance keeps a montage replaced by a new Montage_Play (bShouldStopAllMontages -> StopAllMontagesByGroupName
	// with the new montage's blend in) as a separate instance that blends out in parallel. It only contributes its
	// pose: it is no longer the root motion instance and has no warping.
	struct OutgoingInstance {
		bool bActive = false;
		int Montage = -1;
		int Anim = -1;
		float Position = 0.f, PreviousPosition = 0.f, PlayRate = 1.f, Length = 0.f;
		float BlendBegin = 0.f, BlendDesired = 0.f, BlendTime = 0.f, BlendElapsed = 0.f, Weight = 0.f, LinearAlpha = 1.f;
		BlendOption Option = BlendOption::Linear;
		const AnimLibrary::BlendProfile* Profile = nullptr;
	} Out;
	// per-bone slot weight of a montage instance (blend profile while blending out, otherwise its weight)
	static void PerBoneWeights(const AnimLibrary::BlendProfile* Profile, bool bStopping, float Weight, float LinearAlpha,
		float BlendBegin, float BlendDesired, BlendOption Option, std::vector<float>& W);

	void SetBlend(float Desired, float Time, BlendOption Opt);
	void UpdateWarping(float DeltaTime, const WarpOwnerState& Owner);
	Xform ProcessSkewWarp(Modifier& M, const Xform& InRootMotion, float DeltaTime, const WarpOwnerState& Owner);
	Xform RootRelativeToWarpPointAtTime(float Time, const std::string& Bone, const Quat& MeshRotationOffset) const;
};

} // namespace gasp
