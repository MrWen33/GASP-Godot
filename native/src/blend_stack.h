// Port of FAnimNode_BlendStack_Standalone (+ FBlendStackAnimPlayer), with per-player sample graph hooks.
#pragma once
#include "anim_lib.h"
#include <functional>
#include <memory>

namespace gasp {

struct SampleGraphState; // defined by the graph (orientation warping / steering state per player)

struct BlendStackPlayer {
	int Anim = -1;
	float AccumulatedTime = 0;
	float PreviousTime = 0;
	float MoveDelta = 0;
	bool bLoop = false;
	float PlayRate = 1.f;
	BlendOption Option = BlendOption::Linear;
	float TotalBlendInTime = 0, CurrentBlendInTime = 0, TimeToActivation = 0;
	std::vector<float> TotalBlendInTimePerBone;
	bool bHasStoredPose = false;
	Pose Stored;
	float BlendWeight = 0;
	int PoseLinkIndex = -1;
	std::shared_ptr<SampleGraphState> Graph;

	bool IsActive() const { return TimeToActivation <= 0.f; }
	float GetBlendInPercentage() const {
		if (TotalBlendInTime < 1e-8f) return TimeToActivation > 0.f ? 0.f : 1.f;
		return std::min(CurrentBlendInTime / TotalBlendInTime, 1.f);
	}
	float GetBlendInWeight() const { return (float)AlphaToBlendOption(GetBlendInPercentage(), Option); }
	void GetBlendInWeights(std::vector<float>& W) const;
	void UpdateWithDeltaTime(float DeltaTime, int PlayerDepth, float PlayerDepthBlendInTimeMultiplier);
	float GetCurrentAssetLength() const;
	bool IsLooping() const { return Anim >= 0 && bLoop; }
};

class BlendStack {
public:
	int MaxActiveBlends = 4;
	bool bStoreBlendedPose = true;
	float MaxBlendInTimeToOverrideAnimation = 0.03f;
	float PlayerDepthBlendInTimeMultiplier = 1.1f;
	int NumPoseLinks = 5; // sample graph instances

	std::vector<BlendStackPlayer> Players;
	int CurrentSamplePoseLink = -1;
	std::vector<FiredNotify> Fired; // notifies triggered during the last Update

	// hooks provided by the owning graph
	std::function<std::shared_ptr<SampleGraphState>(int PoseLinkIndex)> MakeGraphState;
	std::function<void(BlendStackPlayer&, float DeltaTime, bool bIsMain)> UpdateSampleGraph;
	std::function<void(BlendStackPlayer&, Pose&)> EvaluateSampleGraph; // post-process a player's sampled pose
	std::function<void(BlendStackPlayer&)> PreAdvance; // e.g. BlendStackInput play rate override

	void Reset() { Players.clear(); }
	void BlendTo(int Anim, float AccumulatedTime, bool bLoop, float BlendTime, const AnimLibrary::BlendProfile* Profile, BlendOption Option, float PlayRate, float ActivationDelay = 0.f);
	void UpdatePlayRate(float PlayRate) { if (!Players.empty()) Players[0].PlayRate = PlayRate; }
	void Update(float DeltaTime);
	void Evaluate(Pose& Out);

	int GetAnim() const { return Players.empty() ? -1 : Players[0].Anim; }
	float GetAccumulatedTime() const { return Players.empty() ? 0.f : Players[0].AccumulatedTime; }
	float GetCurrentAssetLength() const { return Players.empty() ? 0.f : Players[0].GetCurrentAssetLength(); }
	bool IsCurrentAssetLooping() const { return !Players.empty() && Players[0].IsLooping(); }

private:
	void PopLastAnimPlayer();
	bool EvaluateSample(Pose& Out, int PlayerIndex);
	int GetNextPoseLinkIndex();
	Pose Scratch;
};

// pose blending helpers
void BlendWithPose(Pose& InOut, const Pose& Other, float InOutWeight);
void BlendWithPosePerBone(Pose& InOut, const Pose& Other, const std::vector<float>& OtherWeights);

} // namespace gasp
