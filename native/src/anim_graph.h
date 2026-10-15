// ABP_SandboxCharacter AnimGraph port (default motion matching path).
#pragma once
#include "mm.h"
#include "blend_stack.h"
#include "cs_pose.h"
#include "blendspace.h"
#include "montage.h"
#include "foot_placement.h"
#include <functional>
#include <map>
#include <algorithm>

namespace gasp {

// FInputAlphaBoolBlend (linear FAlphaBlend)
struct AlphaBoolBlend {
	float BlendInTime = 0.1f, BlendOutTime = 0.1f;
	BlendOption Option = BlendOption::Linear;
	bool bInit = false;
	float Linear = 0.f; // 0..1
	float Update(bool bEnabled, float Dt) {
		if (!bInit) { bInit = true; Linear = bEnabled ? 1.f : 0.f; }
		const float T = bEnabled ? BlendInTime : BlendOutTime;
		if (T <= 0.f) Linear = bEnabled ? 1.f : 0.f;
		else Linear = Clamp(Linear + (bEnabled ? Dt / T : -Dt / T), 0.f, 1.f);
		return (float)AlphaToBlendOption(Linear, Option);
	}
};

struct OrientationWarpingState {
	bool bIsFirstUpdate = true;
	Vec3 RootMotionDeltaDirection;
	Quat RootMotionDeltaRotation;
	float CounterCompensateTargetAngleRad = 0.f;
	float ActualOrientationAngleRad = 0.f;
};

struct SteeringState {
	bool bResetFilter = true;
	Quat FilteredTarget;
	Vec3 AngularVelocity;
	Quat PrevTarget;
	bool bPrevTargetValid = false;
	AlphaBoolBlend Alpha;
};

struct SampleGraphState {
	OrientationWarpingState OW;
	SteeringState Steer1; // main steering
	SteeringState Steer0; // turn in place steering
	float SteerAlpha1 = 0, SteerAlpha0 = 0;
};

enum class OffsetRootMode { Accumulate, Interpolate, LockOffsetAndConsumeAnimation, LockOffsetIncreaseAndConsumeAnimation, LockOffsetAndIgnoreAnimation, Release };

struct GraphInputs {
	Xform ComponentTransform;            // UE world transform of the skeletal mesh component
	Trajectory Traj;                     // world trajectory (component facing)
	std::vector<int> Databases;
	InterruptMode Interrupt = InterruptMode::DoNotInterrupt;
	float MMBlendTime = 0.5f;
	Vec3 LocomotionDirection;            // LastNonZeroVelocity (world)
	Quat DesiredFacing;                  // world
	bool bEnableSteering = false;
	bool bTurnInPlaceSteering = false;   // CurrentDatabaseTags contains TurnInPlace (computed internally if not overridden)
	OffsetRootMode OffsetTranslationMode = OffsetRootMode::Interpolate;
	OffsetRootMode OffsetRotationMode = OffsetRootMode::Accumulate;
	float OffsetTranslationHalflife = 0.2f;
	float OffsetRotationHalflife = 0.1f;
	float OffsetMaxTranslationError = 30.f;
	bool bOffsetClampToTranslationVelocity = false;
	bool bOffsetRootEnabled = true;
	QueryOverrides Overrides;
	// additive layers
	bool bIsMoving = false;
	float LeanAmount = 0.f;
	bool bEnableAO = false;
	float AOYaw = 0.f, AOPitch = 0.f;
	bool bFootPlacementEnabled = true;    // a.AnimNode.FootPlacement.Enable
	FootPlacementCharacterInputs FootChar;
	float NotifyRecencyTimeOut = 0.2f; // Get_MMNotifyRecencyTimeOut
	// experimental state machine path (TwoWayBlend 'Use Experimental State Machine' -> "State Machine Blend Stack")
	bool bUseStateMachine = false;
	int SMAnim = -1;                     // Blend Stack Inputs
	float SMStartTime = 0.f;
	bool bSMLoop = true;
	float SMBlendTime = 0.2f;
	std::string SMBlendProfile;
	bool bSMForceBlend = false;          // BlendStackAnimNodeLibrary::ForceBlendNextUpdate
	bool bSMStops = false;               // Blend Stack Inputs Tags contains 'Stop' (foot placement settings)
	float Speed2D = 0.f;                 // Get_DynamicPlayRate
	int DebugDisable = 0; // 1 OW, 2 steering, 4 lean, 8 AO, 16 offset root, 32 reset root
};

// BlendListByBool(Inertialization) -> DeadBlending, approximated as a per-bone timed crossfade
// from the snapshot of the last output to the newly active child (custom curve + blend profile).
struct InertialAdditiveSwitch {
	bool bActive = false, bInit = false;
	std::vector<Xform> Snapshot, Last;
	float Elapsed = 0.f, Duration = 0.f;
	std::vector<float> BoneDuration;
	bool bBlending = false;
};

class AnimGraph {
public:
	bool Init();
	void Reset(const Xform& ComponentTransform);
	void Update(float DeltaTime, const GraphInputs& In);

	// results
	Pose Output;                    // final local pose (UE space)
	std::vector<Xform> OutputCS;
	MotionMatchingState MM;
	BlendStack Stack;
	BlendStack SMStack;             // "State Machine Blend Stack"
	std::vector<FiredNotify> SMNotifies;
	PoseHistory History;
	Xform SimulatedRoot;            // OffsetRootBone simulated transform (world)
	std::vector<std::string> CurrentDatabaseTags;
	int CurrentDatabase = -1;

	// hooks for later layers
	std::function<void(Pose&, float)> PostMMLayers;    // additive lean / AO / slot
	std::function<void(CSPose&, float)> PostOffsetRoot; // foot placement / leg IK
	BlendSpacePlayer LeanBS, AOBS;
	AlphaBoolBlend LeanAlpha;
	InertialAdditiveSwitch AOSwitch;
	MontagePlayer Montage;          // DefaultSlot
	FootPlacement Feet;
	LegIK LegIk;
	FootTraceFn FootTrace;
	std::vector<FiredNotify> MMNotifies;
	bool bSourceUpdated = true;
	float SourceWeight = 1.f; // FAnimInstanceProxy::GetSlotWeight SourceWeight of DefaultSlot this tick // slot source (locomotion) updated this tick (FAnimNode_Slot::Update_AnyThread) // blend stack notifies after FBlendStackAnimEventsFilterScope filtering
	const GraphInputs& GetInputs() const { return In; }

private:
	GraphInputs In;
	float Dt = 0.f;
	double GameTime = 0.0;
	std::vector<std::string> NotifiesFiredLastTick;
	std::map<std::string, double> NotifyRecencyMap;
	bool bInitialized = false;
	bool bFeetReady = false;
	Xform OffsetComponentTransform;
	Quat SimulatedRotation;
	Vec3 SimulatedTranslation;
	bool bOffsetFirst = true;
	int BoneIkFootRoot = -1, BoneIkFootL = -1, BoneIkFootR = -1;
	std::vector<int> SpineBones;
	std::vector<float> SpineWeights;
	int CurveEnableOW = -1;
	int CurveEnableTIPSteering = -1, CurveEnablePlayRateWarping = -1, CurveMoveDataSpeed = -1, CurveMaxDynamicPlayRate = -1, CurveMinDynamicPlayRate = -1;
	double SMGameTime = 0.0;
	int LastPathSM = -1; // TwoWayBlend(bool, blend time 0): the child that becomes relevant is re-initialized
	std::vector<std::string> SMNotifiesFiredLastTick;
	std::map<std::string, double> SMNotifyRecencyMap;
	void UpdateSampleGraph(BlendStackPlayer& P, float DeltaTime, bool bMain);
	void EvaluateSampleGraph(BlendStackPlayer& P, Pose& Pose);
	void UpdateSampleGraphSM(BlendStackPlayer& P, float DeltaTime, bool bMain);
	void EvaluateSampleGraphSM(BlendStackPlayer& P, Pose& Pose);
	void EvaluateOrientationWarping(SampleGraphState& G, BlendStackPlayer& P, Pose& Po, CSPose& CS, const Xform& WarpingSpace);
	void EvaluateResetRoot(CSPose& CS);
	float GetDynamicPlayRate(const BlendStackPlayer& P) const;
	void UpdateNotifyFilter(BlendStack& S, double& Time, std::vector<std::string>& LastTick, std::map<std::string, double>& Recency, float TimeOut, std::vector<FiredNotify>& Out, float DeltaTime);
	void EvaluateOffsetRoot(Pose& Pose);
	void EvaluateAdditiveLayers(Pose& Pose, float DeltaTime);
};

} // namespace gasp
