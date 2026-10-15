// Animation library: UE-space keyframes for all GASP sequences, sampled like UE raw animation data.
#pragma once
#include "uemath.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace gasp {
using namespace ue;

constexpr int MAX_CURVES = 256;

struct Pose {
	std::vector<Xform> Bones;
	std::vector<float> Curves;      // dense, indexed by global curve id
	std::vector<uint8_t> CurveSet;  // 1 if the curve is present in this pose
	Xform RootMotion;               // root motion delta attribute (UE::Anim::IAnimRootMotionProvider)
	bool bHasRootMotion = false;
	void Init(int NumBones, int NumCurves) {
		Bones.assign(NumBones, Xform());
		Curves.assign(NumCurves, 0.f);
		CurveSet.assign(NumCurves, 0);
		RootMotion = Xform();
		bHasRootMotion = false;
	}
	void ClearCurves() { std::fill(Curves.begin(), Curves.end(), 0.f); std::fill(CurveSet.begin(), CurveSet.end(), 0); }
	float GetCurve(int Id) const { return (Id >= 0 && Id < (int)Curves.size()) ? Curves[Id] : 0.f; }
	bool HasCurve(int Id) const { return Id >= 0 && Id < (int)CurveSet.size() && CurveSet[Id]; }
	void SetCurve(int Id, float V) { if (Id >= 0 && Id < (int)Curves.size()) { Curves[Id] = V; CurveSet[Id] = 1; } }
};

struct NotifyEvent {
	std::string Name;
	std::string Class;
	float Time = 0, Duration = 0;
	bool bState = false;
	float CostAddend = 0;
	bool bHasCostAddend = false;
	std::string Side;
	// motion warping modifier
	std::string WarpTargetName, WarpPointBone;
	bool bWarpTranslation = true, bIgnoreZAxis = true, bWarpRotation = true;
	std::string RotationType;
	float WarpRotationTimeMultiplier = 1.f;
	float WarpMaxRotationRate = 0.f;
	// FAnimNotifyEvent::TriggerWeightThreshold (tools/build/build_notify_thresholds.py; GASP foley notifies mostly 0.2)
	float TriggerWeightThreshold = 0.00001f; // ZERO_ANIMWEIGHT_THRESH
	float EndTime() const { return Time + Duration; }
};

struct FiredNotify {
	const NotifyEvent* Event = nullptr;
	float Weight = 0;
	bool bMontage = false;
};

// Non-state notifies whose trigger time lies in (Prev, Prev+Delta] (with wrap for looping assets).
inline void CollectNotifies(const std::vector<NotifyEvent>& Notifies, float Prev, float Delta, float Length, bool bLoop, float Weight, bool bMontage, std::vector<FiredNotify>& Out) {
	if (Delta == 0.f || Notifies.empty()) return;
	auto Range = [&](float A, float B) { // half-open (A, B] for forward play
		for (const NotifyEvent& N : Notifies) {
			if (N.bState) continue;
			// FAnimNotifyQueue::AddAnimNotifiesToDest: only queued when TriggerWeightThreshold <= InstanceWeight
			if (N.TriggerWeightThreshold > Weight) continue;
			if (N.Time > A && N.Time <= B) Out.push_back({ &N, Weight, bMontage });
		}
	};
	if (Delta > 0.f) {
		float End = Prev + Delta;
		if (bLoop && Length > 0.f && End > Length) {
			Range(Prev, Length);
			End = std::fmod(End, Length);
			Range(-1e-6f, End);
		} else Range(Prev, std::min(End, Length));
	} else {
		float End = Prev + Delta;
		if (bLoop && Length > 0.f && End < 0.f) { Range(0.f - 1e-6f, Prev); End += Length; Range(End, Length); }
		else Range(std::max(End, 0.f), Prev);
	}
}

struct AnimClip {
	std::string Name, Path;
	float Length = 0, Fps = 30;
	int NumKeys = 0;
	bool bLoop = false, bRootMotion = false, bLockRoot = false;
	const float* Keys = nullptr;    // [NumKeys][NumBones][7]
	const float* CurveData = nullptr; // [NumKeys][NumLocalCurves]
	std::vector<int> CurveIds;       // local curve -> global id
	std::vector<int> GlobalToLocal;  // global id -> local index or -1
	std::vector<NotifyEvent> Notifies;
	// additive
	int AdditiveType = 0; // 0 none, 1 local, 2 rotation offset mesh space
	int AdditiveBaseAnim = -1;
	int AdditiveBaseFrame = 0;
};

struct WarpWindow {
	float Start = 0, End = 0;
	std::string Target, Bone;
	bool bBoneProvider = false;
	bool bWarpTranslation = true, bIgnoreZAxis = true, bWarpRotation = true;
	float RotationTimeMultiplier = 1.f;
};

struct BlendOutWindow {
	float Start = 0, End = 0, BlendOutTime = 0.2f;
	std::string Profile;
	int Condition = 0; // E_TraversalBlendOutCondition: 0 ForceBlendOut, 1 WithMovementInput, 2 IfFalling
};

struct MontageInfo {
	std::string Path;
	int Anim = -1;
	float Length = 0;
	std::vector<NotifyEvent> Notifies;
	float BlendInTime = 0.25f, BlendOutTime = 0.25f;
	BlendOption BlendInOption = BlendOption::Linear, BlendOutOption = BlendOption::Linear;
	float BlendOutTriggerTime = -1.f;
	bool bEnableAutoBlendOut = true;
	std::vector<WarpWindow> Warps;
	std::vector<BlendOutWindow> BlendOuts;
	// montage float curves (UAnimMontage::RawCurveData), piecewise linear keys (time, value)
	struct Curve { int Index = -1; std::vector<std::pair<float, float>> Keys; };
	std::vector<Curve> Curves;
	// FAnimMontageInstance evaluation adds these to the montage pose (UAnimMontage::EvaluateCurveData)
	void EvaluateCurves(float Time, std::vector<float>& Values, std::vector<uint8_t>& Set) const {
		for (const Curve& C : Curves) {
			if (C.Index < 0 || C.Keys.empty()) continue;
			float V = C.Keys.front().second;
			if (Time >= C.Keys.back().first) V = C.Keys.back().second;
			else for (size_t k = 1; k < C.Keys.size(); ++k) {
				if (Time < C.Keys[k].first) {
					const auto& A = C.Keys[k - 1]; const auto& B = C.Keys[k];
					const float T = B.first > A.first ? (Time - A.first) / (B.first - A.first) : 0.f;
					V = Time <= A.first ? A.second : A.second + (B.second - A.second) * T;
					break;
				}
			}
			Values[C.Index] = V;
			Set[C.Index] = 1;
		}
	}
};

class AnimLibrary {
public:
	static AnimLibrary& Get();
	bool Load(const std::string& DataDir); // absolute OS path to godot/data
	bool IsLoaded() const { return bLoaded; }

	int NumBones() const { return (int)BoneNames.size(); }
	int NumCurves() const { return (int)CurveNames.size(); }
	int FindBone(const std::string& Name) const;
	int FindCurve(const std::string& Name) const;
	int FindAnim(const std::string& PathOrName) const;
	int FindMontage(const std::string& Path) const;
	const AnimClip& Clip(int i) const { return Clips[i]; }
	const MontageInfo& Montage(int i) const { return Montages[i]; }
	int NumMontages() const { return (int)Montages.size(); }
	int NumClips() const { return (int)Clips.size(); }

	// local pose at time (UE raw-key linear interpolation). Root is locked to the ref pose when the clip uses root motion.
	void SampleLocal(int Anim, float Time, Pose& Out) const;
	Xform SampleBone(int Anim, int Bone, float Time) const;
	Xform SampleRootRaw(int Anim, float Time) const;
	float SampleCurve(int Anim, int GlobalCurve, float Time, bool* bFound = nullptr) const;
	// UAnimSequence::ExtractRootMotion(StartTime, DeltaTime, bLoop)
	Xform ExtractRootMotion(int Anim, float StartTime, float DeltaTime, bool bLoop) const;
	Xform ExtractRootMotionFromRange(int Anim, float A, float B) const;
	// component space transform of a bone for a sampled local pose
	void LocalToComponent(const std::vector<Xform>& Local, std::vector<Xform>& OutCS) const;

	std::vector<std::string> BoneNames;
	std::vector<int> Parents;
	std::vector<Xform> RefPose;
	std::vector<std::string> CurveNames;
	std::unordered_map<std::string, int> CurveIndex;

	// blend profiles: name -> (mode WeightFactor?, per-bone scale)
	struct BlendProfile { bool bWeightFactor = true; std::vector<float> Scales; std::vector<uint8_t> Has; };
	std::unordered_map<std::string, BlendProfile> BlendProfiles;
	const BlendProfile* FindBlendProfile(const std::string& Name) const;

private:
	bool bLoaded = false;
	std::vector<char> Blob;
	std::vector<AnimClip> Clips;
	std::vector<MontageInfo> Montages;
	std::unordered_map<std::string, int> ClipIndex;
	std::unordered_map<std::string, int> MontageIndex;
	std::unordered_map<std::string, int> BoneIndex;
	int AddCurveName(const std::string& N);
};

} // namespace gasp
