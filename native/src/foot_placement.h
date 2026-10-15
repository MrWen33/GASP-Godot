// Port of FAnimNode_FootPlacement (AnimationWarping plugin, UE 5.5) and FAnimNode_LegIK (two-bone path + soft IK),
// configured as in ABP_SandboxCharacter. Works on a component-space pose (CSPose) in UE units.
#pragma once
#include "cs_pose.h"
#include <functional>

namespace gasp {

struct Plane {
	Vec3 N = Vec3(0, 0, 1);
	double W = 0;
	Plane() = default;
	Plane(const Vec3& Point, const Vec3& Normal) : N(Normal), W(Point | Normal) {}
	double PlaneDot(const Vec3& P) const { return (N | P) - W; }
	Plane TransformBy(const Xform& X) const {
		Plane R;
		R.N = X.TransformVectorNoScale(N);
		R.W = R.N | X.TransformPosition(N * W);
		return R;
	}
	Plane TranslateBy(const Vec3& V) const { Plane R = *this; R.W = W + (N | V); return R; }
};

// UKismetMathLibrary spring states
struct FloatSpringState { double PrevTarget = 0, Velocity = 0; bool bPrevTargetValid = false; void Reset() { *this = FloatSpringState(); } };
struct VectorSpringState { Vec3 PrevTarget, Velocity; bool bPrevTargetValid = false; void Reset() { *this = VectorSpringState(); } };
struct QuatSpringState { Quat PrevTarget; Vec3 AngularVelocity; bool bPrevTargetValid = false; void Reset() { *this = QuatSpringState(); } };

double FloatSpringInterp(double Current, double Target, FloatSpringState& S, double Stiffness, double Damping, double Dt);
Vec3 VectorSpringInterp(const Vec3& Current, const Vec3& Target, VectorSpringState& S, double Stiffness, double Damping, double Dt);
Quat QuaternionSpringInterp(const Quat& Current, const Quat& Target, QuatSpringState& S, double Stiffness, double Damping, double Dt);

struct FootPlacementPlantSettings {
	double SpeedThreshold = 60, DistanceToGround = 10, UnplantRadius = 35, ReplantRadiusRatio = 0.35, UnplantAngle = 45, ReplantAngleRatio = 0.5;
	double MaxExtensionRatio = 0.5, MinExtensionRatio = 0.2, SeparatingDistance = 0, UnalignmentSpeedThreshold = 200, AnkleTwistReduction = 0.75;
	bool bAdjustHeelBeforePlanting = false;
};
struct FootPlacementInterpolationSettings {
	double UnplantLinearStiffness = 250, UnplantLinearDamping = 1, UnplantAngularStiffness = 450, UnplantAngularDamping = 1;
	double FloorLinearStiffness = 1000, FloorLinearDamping = 1, FloorAngularStiffness = 450, FloorAngularDamping = 1;
	bool bEnableFloorInterpolation = true;
};

// Character / movement component state needed by the node (CharacterMovementComponent accessors)
struct FootPlacementCharacterInputs {
	Xform ComponentToWorld;
	Vec3 Velocity;
	bool bWalkingWithFloor = true;     // MovementMode Walking/NavWalking && CurrentFloor.bBlockingHit
	bool bHasFloor = true;
	Vec3 FloorLocation, FloorNormal = Vec3(0, 0, 1);
	double WalkableFloorZ = 0.71;      // cos(WalkableFloorAngle)
};

// Sphere sweep (UE world space). Returns hit + impact point/normal.
using FootTraceFn = std::function<bool(const Vec3& Start, const Vec3& End, double Radius, Vec3& OutPoint, Vec3& OutNormal)>;

class FootPlacement {
public:
	enum class PlantType { Unplanted, Planted, Replanted };
	FootPlacementPlantSettings PlantSettings;
	FootPlacementInterpolationSettings InterpolationSettings;
	// node settings (ABP_SandboxCharacter)
	double PelvisMaxOffset = 250, PelvisLinearStiffness = 100, PelvisLinearDamping = 1, HorizontalRebalancingWeight = 0.3, MaxOffsetHorizontal = 10, HeelLiftRatio = 0.5;
	double TraceStartOffset = -75, TraceEndOffset = 100, SweepRadius = 5, MaxGroundPenetration = 20;
	double TeleportDistanceThreshold = 300;

	bool Init();
	void Reset() { bIsFirstUpdate = true; ResetRuntimeData(); }
	void Evaluate(CSPose& Pose, const struct Pose& Curves, double DeltaTime, const FootPlacementCharacterInputs& Char, const FootTraceFn& Trace);

	// debug output (world space)
	struct LegDebug { Vec3 InputFootWS, OutputFootWS; PlantType Plant = PlantType::Unplanted; double Speed = 0, AlignmentAlpha = 0; };
	LegDebug Debug[2];
	Vec3 DebugPelvisInWS, DebugPelvisOutWS;

private:
	struct Leg {
		int FK = -1, Ball = -1, IK = -1, Hip = -1, SpeedCurve = -1;
		double LimbLength = 0, FootLength = 0;
		// input pose
		Xform FootFKTransformCS, FootTransformCS, BallTransformCS, HipTransformCS, BallToFoot, FootToBall;
		double Speed = 0, LockAlpha = 1, DistanceToPlant = 0, AlignmentAlpha = 0;
		// plant
		PlantType Type = PlantType::Unplanted, LastType = PlantType::Unplanted;
		Plane PlantPlaneRS;
		Quat TwistCorrection;
		double TimeSinceFullyUnaligned = 0;
		bool bCanReachTarget = false, bWantsToPlant = false;
		Xform AlignedFootTransformWS, AlignedFootTransformRS, UnalignedFootTransformWS, UnalignedFootTransformRS;
		// interpolation
		Xform UnalignedFootOffset;
		VectorSpringState PlantOffsetTranslationSpringState;
		QuatSpringState PlantOffsetRotationSpringState;
		FloatSpringState GroundHeightSpringState;
		QuatSpringState GroundRotationSpringState;
	};
	Leg Legs[2];
	int Pelvis = -1, IKRoot = -1;
	Xform PelvisFKTransformCS, IKRootTransformCS, RootTransformCS;
	Vec3 PelvisTranslationOffset;
	VectorSpringState PelvisTranslationSpringState;
	Xform ComponentTransformWS;
	Vec3 ComponentMoveDeltaWS;
	Vec3 SmoothCapsuleGroundNormalWS = Vec3(0, 0, 1);
	QuatSpringState SmoothCapsuleGroundNormalSpringState;
	bool bIsOnGround = false;
	bool bIsFirstUpdate = true;
	double UnplantRadiusSqrd = 0, ReplantRadiusSqrd = 0, CosHalfUnplantAngle = 0, CosHalfReplantAngle = 0;

	// evaluation context
	const FootPlacementCharacterInputs* C = nullptr;
	const FootTraceFn* TraceFn = nullptr;
	double Dt = 0;
	Vec3 ApproachDirCS = Vec3(0, 0, -1), ApproachDirWS = Vec3(0, 0, -1);

	void ResetRuntimeData();
	const Xform& RootToComponent() const { return RootTransformCS; }
	void GatherLeg(const CSPose& Pose, const struct Pose& Curves, Leg& L);
	double CalcTargetPlantPlaneDistance(const Leg& L) const;
	bool WantsToPlant(const Leg& L) const;
	double GetAlignmentAlpha(const Leg& L) const;
	void ProcessCharacterState();
	void ProcessFootAlignment(Leg& L);
	void DeterminePlantType(const Xform& FKTransformWS, const Xform& CurrentBoneTransformWS, Leg& L);
	Xform GetFootPivotAroundBallWS(const Leg& L, const Xform& LastPlantTransformWS) const;
	Xform UpdatePlantOffsetInterpolation(Leg& L) const;
	void UpdatePlantingPlaneInterpolation(const Xform& FootTransformWS, const Xform& LastAlignedFootTransform, Plane& InOutPlantPlane, Leg& L) const;
	bool FindPlantPlane(const Vec3& StartWS, Plane& OutPlaneWS, Vec3& OutImpactWS) const;
	void AlignPlantToGround(const Plane& PlantPlaneWS, const Leg& L, Xform& InOutFootTransformWS, Quat& OutTwistCorrection) const;
	Xform SolvePelvis();
	void FindPelvisOffsetRangeForLimb(const Leg& L, const Vec3& PlantTargetLocationCS, const Xform& PelvisTransformCS, double& OutMax, double& OutMin, double& OutDesired) const;
	Xform UpdatePelvisInterpolationRootSpace(const Xform& TargetPelvisTransformRS);
	Xform FinalizeFootAlignment(Leg& L, const Xform& PelvisTransformCS);
	double GetMaxLimbExtension(double Desired, double LimbLength) const;
	double GetMinLimbExtension(double Desired, double LimbLength) const;
};

// FAnimNode_LegIK: legs ik_foot_l/foot_l and ik_foot_r/foot_r, NumBonesInLimb 2, HingeRotationAxis Z,
// rotation limit on (unused by the two-bone path), knee twist correction on (foot forward axis Y), SoftPercentLength 0.99.
class LegIK {
public:
	double SoftPercentLength = 0.99, SoftAlpha = 1.0, ReachPrecision = 0.01;
	bool Init();
	void Evaluate(CSPose& Pose);
private:
	struct LegData {
		int IKFoot = -1;
		int Bones[3] = { -1, -1, -1 }; // foot, calf, thigh
		Vec3 CachedRealBendDir, CachedBaseBendDir;
	};
	LegData Legs[2];
};

} // namespace gasp
