#include "foot_placement.h"

namespace gasp {

static constexpr double DELTA = 0.00001;
static constexpr double BIG_NUMBER = 3.4e+38;

// ------------------------------------------------------------------ FMath::SpringDamper / UKismetMathLibrary springs
template <typename T>
static void SpringDamper(T& InOutValue, T& InOutValueRate, const T& InTargetValue, const T& InTargetValueRate, double InDeltaTime, double InUndampedFrequency, double InDampingRatio) {
	if (InDeltaTime <= 0.0) return;
	const double W = InUndampedFrequency * 2.0 * PI;
	if (W < SMALL_NUMBER) { InOutValue = InOutValue + InOutValueRate * InDeltaTime; return; }
	if (InDampingRatio < SMALL_NUMBER) {
		const T Err = InOutValue - InTargetValue;
		const T B = InOutValueRate * (1.0 / W);
		const double S = std::sin(W * InDeltaTime), Cc = std::cos(W * InDeltaTime);
		InOutValue = InTargetValue + Err * Cc + B * S;
		InOutValueRate = InOutValueRate * Cc - Err * (W * S);
		return;
	}
	const double SmoothingTime = 2.0 / W;
	const T AdjustedTarget = InTargetValue + InTargetValueRate * (InDampingRatio * SmoothingTime);
	const T Err = InOutValue - AdjustedTarget;
	if (InDampingRatio > 1.0) {
		const double WD = W * std::sqrt(InDampingRatio * InDampingRatio - 1.0);
		const T C2 = (InOutValueRate + Err * (W * InDampingRatio - WD)) * (-1.0 / (2.0 * WD));
		const T C1 = Err - C2;
		const double A1 = (WD - InDampingRatio * W), A2 = -(WD + InDampingRatio * W);
		const double E1 = InvExpApprox(-A1 * InDeltaTime), E2 = InvExpApprox(-A2 * InDeltaTime);
		InOutValue = AdjustedTarget + C1 * E1 + C2 * E2;
		InOutValueRate = C1 * (E1 * A1) + C2 * (E2 * A2);
	} else if (InDampingRatio < 1.0) {
		const double WD = W * std::sqrt(1.0 - InDampingRatio * InDampingRatio);
		const T A = Err;
		const T B = (InOutValueRate + Err * (InDampingRatio * W)) * (1.0 / WD);
		const double S = std::sin(WD * InDeltaTime), Cc = std::cos(WD * InDeltaTime);
		const double E = InvExpApprox(InDampingRatio * W * InDeltaTime);
		InOutValue = (A * Cc + B * S) * E;
		InOutValueRate = InOutValue * (-InDampingRatio * W);
		InOutValueRate = InOutValueRate + (B * (WD * Cc) - A * (WD * S)) * E;
		InOutValue = InOutValue + AdjustedTarget;
	} else {
		const T C1 = Err;
		const T C2 = InOutValueRate + Err * W;
		const double E = InvExpApprox(W * InDeltaTime);
		InOutValue = AdjustedTarget + (C1 + C2 * InDeltaTime) * E;
		InOutValueRate = (C2 - C1 * W - C2 * (W * InDeltaTime)) * E;
	}
}

// GenericSpringInterp with Mass = 1, TargetVelocityAmount = 0 (as called by FAnimNode_FootPlacement)
template <typename T>
static void GenericSpringInterp(T& Current, const T& Target, T& PrevTarget, bool& bPrevTargetValid, T& Velocity, double Stiffness, double Damping, double Dt, const T& Zero) {
	if (Dt > SMALL_NUMBER) {
		const T TargetVel = Zero;
		const double Omega = std::sqrt(Stiffness);
		const double Frequency = Omega / (2.0 * PI);
		SpringDamper(Current, Velocity, Target, TargetVel, Dt, Frequency, Damping);
		PrevTarget = Target;
		bPrevTargetValid = true;
	}
}

double FloatSpringInterp(double Current, double Target, FloatSpringState& S, double Stiffness, double Damping, double Dt) {
	GenericSpringInterp(Current, Target, S.PrevTarget, S.bPrevTargetValid, S.Velocity, Stiffness, Damping, Dt, 0.0);
	return Current;
}

Vec3 VectorSpringInterp(const Vec3& InCurrent, const Vec3& Target, VectorSpringState& S, double Stiffness, double Damping, double Dt) {
	Vec3 Current = InCurrent;
	GenericSpringInterp(Current, Target, S.PrevTarget, S.bPrevTargetValid, S.Velocity, Stiffness, Damping, Dt, Vec3());
	return Current;
}

Quat QuaternionSpringInterp(const Quat& InCurrent, const Quat& InTarget, QuatSpringState& S, double Stiffness, double Damping, double Dt) {
	Quat Current = InCurrent, Target = InTarget;
	if ((Target | Current) < 0.0) Target = Target * -1.0;
	Quat VelocityQuat(S.AngularVelocity.X, S.AngularVelocity.Y, S.AngularVelocity.Z, 0.0);
	Quat Velocity = (Current * 0.5) * VelocityQuat;
	GenericSpringInterp(Current, Target, S.PrevTarget, S.bPrevTargetValid, Velocity, Stiffness, Damping, Dt, Quat(0, 0, 0, 0));
	Current.Normalize();
	VelocityQuat = (Current.Inverse() * 2.0) * Velocity;
	S.AngularVelocity = Vec3(VelocityQuat.X, VelocityQuat.Y, VelocityQuat.Z);
	return Current;
}

// ------------------------------------------------------------------ helpers (UE::Anim::FootPlacement namespace)
static Vec3 PointDirectionPlaneIntersection(const Vec3& Point, const Vec3& Direction, const Plane& P) {
	return Point + Direction * ((P.W - (Point | P.N)) / (Direction | P.N));
}

static double GetDistanceToPlaneAlongDirection(const Vec3& Location, const Plane& P, const Vec3& ApproachDir) {
	const Vec3 IntersectionLoc = PointDirectionPlaneIntersection(Location, ApproachDir * -1.0, P);
	return (Location - IntersectionLoc) | (ApproachDir * -1.0);
}

static void SphereDistToLine(const Vec3& SphereOrigin, double SphereRadius, const Vec3& LineOrigin, const Vec3& Dir, Vec3& Out) {
	const Vec3 LineOriginToSphereOrigin = SphereOrigin - LineOrigin;
	const double B = -2.0 * (Dir | LineOriginToSphereOrigin);
	const double Cc = LineOriginToSphereOrigin.SizeSquared() - SphereRadius * SphereRadius;
	const double D = B * B - 4.0 * Cc;
	if (D <= KINDA_SMALL_NUMBER) {
		const Vec3 PointOnLine = LineOrigin + Dir * (-B * 0.5);
		Out = SphereOrigin + (PointOnLine - SphereOrigin).GetSafeNormal() * SphereRadius;
	} else {
		const double E = std::sqrt(D);
		const double T1 = (-B + E) * 0.5, T2 = (-B - E) * 0.5;
		const double T = std::abs(T1) == std::abs(T2) ? std::abs(T1) : (std::abs(T1) < std::abs(T2) ? T1 : T2);
		Out = LineOrigin + Dir * T;
	}
}

// A.GetRelativeTransformReverse(B) = A^-1 * B
static Xform RelativeReverse(const Xform& A, const Xform& B) {
	Xform R;
	R.Scale3D = Vec3(B.Scale3D.X / A.Scale3D.X, B.Scale3D.Y / A.Scale3D.Y, B.Scale3D.Z / A.Scale3D.Z);
	R.Rotation = B.Rotation * A.Rotation.Inverse();
	R.Translation = B.Translation - R.Scale3D * R.Rotation.RotateVector(A.Translation);
	return R;
}

// ------------------------------------------------------------------ FootPlacement
bool FootPlacement::Init() {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const char* FK[2] = { "foot_r", "foot_l" };
	const char* IK[2] = { "ik_foot_r", "ik_foot_l" };
	const char* Ball[2] = { "ball_r", "ball_l" };
	const char* Speed[2] = { "footspeed_r", "footspeed_l" };
	for (int i = 0; i < 2; ++i) {
		Leg& L = Legs[i];
		L.FK = Lib.FindBone(FK[i]); L.IK = Lib.FindBone(IK[i]); L.Ball = Lib.FindBone(Ball[i]);
		L.SpeedCurve = Lib.FindCurve(Speed[i]);
		if (L.FK < 0 || L.IK < 0 || L.Ball < 0) return false;
		// FindChainLengthRootBoneIndex, NumBonesInLimb = 2
		L.LimbLength = 0;
		int B = L.FK, P = Lib.Parents[B];
		for (int n = 2; n > 0 && P >= 0; --n) { L.LimbLength += Lib.RefPose[B].Translation.Size(); B = P; P = Lib.Parents[B]; }
		L.Hip = B;
		L.FootLength = Lib.RefPose[L.Ball].Translation.Size();
	}
	Pelvis = Lib.FindBone("pelvis");
	IKRoot = Lib.FindBone("ik_foot_root");
	ResetRuntimeData();
	return Pelvis >= 0 && IKRoot >= 0;
}

void FootPlacement::ResetRuntimeData() {
	PelvisTranslationOffset = Vec3();
	PelvisTranslationSpringState.Reset();
	for (Leg& L : Legs) {
		L.UnalignedFootOffset = Xform();
		L.PlantOffsetTranslationSpringState.Reset();
		L.PlantOffsetRotationSpringState.Reset();
		L.GroundHeightSpringState.Reset();
		L.GroundRotationSpringState.Reset();
	}
	bIsFirstUpdate = true;
}

double FootPlacement::GetMaxLimbExtension(double Desired, double LimbLength) const {
	if (Desired > LimbLength) return Desired;
	return Desired + (LimbLength - Desired) * PlantSettings.MaxExtensionRatio;
}
double FootPlacement::GetMinLimbExtension(double Desired, double LimbLength) const {
	return std::min(Desired, LimbLength * PlantSettings.MinExtensionRatio);
}

double FootPlacement::CalcTargetPlantPlaneDistance(const Leg& L) const {
	const Xform IKBallBoneCS = L.FootToBall * L.FootTransformCS;
	const Plane IKGroundPlaneCS(IKRootTransformCS.Translation, IKRootTransformCS.TransformVectorNoScale(Vec3(0, 0, 1)));
	const double FootBase = GetDistanceToPlaneAlongDirection(L.FootTransformCS.Translation, IKGroundPlaneCS, ApproachDirCS);
	const double BallBase = GetDistanceToPlaneAlongDirection(IKBallBoneCS.Translation, IKGroundPlaneCS, ApproachDirCS);
	return std::min(FootBase, BallBase);
}

bool FootPlacement::WantsToPlant(const Leg& L) const {
	if (IsNearlyZero(L.LockAlpha)) return false;
	return L.DistanceToPlant < PlantSettings.DistanceToGround && L.Speed < PlantSettings.SpeedThreshold;
}

double FootPlacement::GetAlignmentAlpha(const Leg& L) const {
	// FMath::GetRangePct(Unalignment, SpeedThreshold, Speed)
	const double A = PlantSettings.UnalignmentSpeedThreshold, B = PlantSettings.SpeedThreshold;
	const double Div = B - A;
	const double Pct = IsNearlyZero(Div) ? (L.Speed >= B ? 1.0 : 0.0) : (L.Speed - A) / Div;
	return Clamp(Pct, 0.0, 1.0);
}

void FootPlacement::GatherLeg(const CSPose& Pose, const struct Pose& Curves, Leg& L) {
	L.FootFKTransformCS = Pose.Get(L.FK);
	const Xform BallTransformCS = Pose.Get(L.Ball);
	L.FootTransformCS = Pose.Get(L.IK);
	L.HipTransformCS = Pose.Get(L.Hip);
	L.BallToFoot = L.FootFKTransformCS.GetRelativeTransform(BallTransformCS);
	L.FootToBall = BallTransformCS.GetRelativeTransform(L.FootFKTransformCS);
	L.BallTransformCS = L.FootToBall * L.FootTransformCS;
	if (bIsFirstUpdate) {
		L.AlignedFootTransformRS = L.FootTransformCS.GetRelativeTransform(RootToComponent());
		L.AlignedFootTransformWS = L.FootTransformCS * C->ComponentToWorld;
		L.UnalignedFootTransformRS = L.AlignedFootTransformRS;
		L.UnalignedFootTransformWS = L.AlignedFootTransformWS;
		const Vec3 IKFootRootLocationRS = RootToComponent().InverseTransformPosition(IKRootTransformCS.Translation);
		L.PlantPlaneRS = Plane(IKFootRootLocationRS, ApproachDirCS * -1.0);
		L.Type = L.LastType = PlantType::Unplanted;
	}
	// Manual speed mode: curve value, default SpeedThreshold when the curve is missing
	L.Speed = Curves.HasCurve(L.SpeedCurve) ? Curves.GetCurve(L.SpeedCurve) : PlantSettings.SpeedThreshold;
	L.LockAlpha = 1.0;
	L.DistanceToPlant = CalcTargetPlantPlaneDistance(L);
	L.AlignmentAlpha = GetAlignmentAlpha(L);
}

void FootPlacement::ProcessCharacterState() {
	const Vec3 LastComponentLocationWS = bIsFirstUpdate ? C->ComponentToWorld.Translation : ComponentTransformWS.Translation;
	if (bIsFirstUpdate) {
		SmoothCapsuleGroundNormalWS = ApproachDirWS * -1.0;
		SmoothCapsuleGroundNormalSpringState.Reset();
	}
	ComponentTransformWS = C->ComponentToWorld;
	const Vec3 ComponentLocationWS = ComponentTransformWS.Translation;
	const bool bWasOnGround = bIsOnGround;
	bIsOnGround = C->bWalkingWithFloor;
	ComponentMoveDeltaWS = Vec3();
	const bool bOnGround = bIsOnGround && bWasOnGround; // bDisablePelvisOffsetInAir
	if (bOnGround) {
		// SuddenMotionOnly
		const Vec3 FloorNormal = C->bHasFloor ? C->FloorNormal : ApproachDirWS * -1.0;
		Quat SlopeDelta = Quat::FindBetweenNormals(SmoothCapsuleGroundNormalWS, FloorNormal);
		SlopeDelta = QuaternionSpringInterp(Quat(), SlopeDelta, SmoothCapsuleGroundNormalSpringState, InterpolationSettings.FloorAngularStiffness, 1.0, Dt);
		SmoothCapsuleGroundNormalWS = SlopeDelta.RotateVector(SmoothCapsuleGroundNormalWS);
		const Vec3 CapsuleFloorNormalWS = SmoothCapsuleGroundNormalWS;
		const Vec3 AdjustedLast = (std::abs(ApproachDirWS | CapsuleFloorNormalWS) > DELTA)
			? PointDirectionPlaneIntersection(ComponentLocationWS, ApproachDirWS, Plane(LastComponentLocationWS, CapsuleFloorNormalWS))
			: ComponentLocationWS;
		const Vec3 CapsuleMoveOffsetWS = (ComponentLocationWS - AdjustedLast) * (ApproachDirWS * -1.0);
		ComponentMoveDeltaWS = ComponentMoveDeltaWS - CapsuleMoveOffsetWS;
		if (!CapsuleMoveOffsetWS.IsNearlyZero(KINDA_SMALL_NUMBER)) {
			const Vec3 CapsuleMoveOffsetCS = C->ComponentToWorld.InverseTransformVectorNoScale(CapsuleMoveOffsetWS);
			PelvisTranslationOffset = PelvisTranslationOffset - CapsuleMoveOffsetCS;
			for (Leg& L : Legs) L.PlantPlaneRS = L.PlantPlaneRS.TranslateBy(CapsuleMoveOffsetCS * -1.0);
		}
	}
	ComponentMoveDeltaWS += ComponentLocationWS - LastComponentLocationWS;
}

void FootPlacement::DeterminePlantType(const Xform& FKTransformWS, const Xform& CurrentBoneTransformWS, Leg& L) {
	const bool bWasPlanted = L.Type != PlantType::Unplanted;
	const bool bWantedToPlant = L.bWantsToPlant;
	L.bWantsToPlant = WantsToPlant(L);
	L.Type = PlantType::Unplanted;
	if (!L.bWantsToPlant) return;
	if (bWasPlanted) {
		const Vec3 D = CurrentBoneTransformWS.Translation - FKTransformWS.Translation;
		const bool bTransExceeded = PlantSettings.ReplantRadiusRatio < 1.0 && D.SizeSquared2D() > UnplantRadiusSqrd;
		const bool bRotExceeded = PlantSettings.ReplantAngleRatio < 1.0 && std::abs(L.TwistCorrection.W) < CosHalfUnplantAngle;
		if (!bTransExceeded && !bRotExceeded) L.Type = L.LastType;
	} else if (!bWantedToPlant) {
		L.Type = PlantType::Planted;
	} else {
		const Vec3 D = CurrentBoneTransformWS.Translation - FKTransformWS.Translation;
		if (D.SizeSquared2D() <= ReplantRadiusSqrd && std::abs(L.TwistCorrection.W) >= CosHalfReplantAngle) L.Type = PlantType::Replanted;
	}
}

Xform FootPlacement::GetFootPivotAroundBallWS(const Leg& L, const Xform& LastPlantTransformWS) const {
	const Xform BallTransformWS = L.BallTransformCS * C->ComponentToWorld;
	const Xform Pinned(BallTransformWS.Rotation, (L.FootToBall * LastPlantTransformWS).Translation, BallTransformWS.Scale3D);
	return L.BallToFoot * Pinned;
}

Xform FootPlacement::UpdatePlantOffsetInterpolation(Leg& L) const {
	const Vec3 T = VectorSpringInterp(L.UnalignedFootOffset.Translation, Vec3(), L.PlantOffsetTranslationSpringState,
		InterpolationSettings.UnplantLinearStiffness, InterpolationSettings.UnplantLinearDamping, Dt);
	const Quat R = QuaternionSpringInterp(L.UnalignedFootOffset.Rotation, Quat(), L.PlantOffsetRotationSpringState,
		InterpolationSettings.UnplantAngularStiffness, InterpolationSettings.UnplantAngularDamping, Dt);
	return Xform(R, T);
}

bool FootPlacement::FindPlantPlane(const Vec3& StartWS, Plane& OutPlaneWS, Vec3& OutImpactWS) const {
	Vec3 Normal = C->ComponentToWorld.Rotation.GetUpVector();
	OutImpactWS = C->ComponentToWorld.Translation;
	bool bFound = false;
	Vec3 HitP, HitN;
	const Vec3 TraceStart = StartWS + ApproachDirWS * TraceStartOffset;
	const Vec3 TraceEnd = StartWS + ApproachDirWS * TraceEndOffset;
	const bool bHit = TraceFn && *TraceFn && (*TraceFn)(TraceStart, TraceEnd, SweepRadius, HitP, HitN);
	if (!bHit || HitN.Z < C->WalkableFloorZ) {
		const Vec3 FloorLoc = C->bHasFloor ? C->FloorLocation : C->ComponentToWorld.Translation;
		OutImpactWS = PointDirectionPlaneIntersection(StartWS, ApproachDirWS, Plane(FloorLoc, ApproachDirWS * -1.0));
		Normal = ApproachDirWS * -1.0;
	} else {
		OutImpactWS = HitP;
		Normal = HitN;
		bFound = true;
	}
	OutPlaneWS = Plane(OutImpactWS, Normal);
	return bFound;
}

void FootPlacement::UpdatePlantingPlaneInterpolation(const Xform& FootTransformWS, const Xform& LastAlignedFootTransform, Plane& InOutPlantPlane, Leg& L) const {
	const Vec3 TraceDirection = ApproachDirWS;
	const Plane LastPlantPlane = InOutPlantPlane;
	Vec3 ImpactLocationWS;
	const bool bFoundGround = FindPlantPlane(FootTransformWS.Translation, InOutPlantPlane, ImpactLocationWS);
	if (!bIsOnGround) {
		const Vec3 SourceGroundPoint = C->ComponentToWorld.TransformPosition(IKRootTransformCS.Translation);
		InOutPlantPlane = Plane(SourceGroundPoint, ApproachDirWS * -1.0);
	}
	if (InterpolationSettings.bEnableFloorInterpolation && !bIsFirstUpdate) {
		Vec3 CurrPlaneIntersection = PointDirectionPlaneIntersection(FootTransformWS.Translation, TraceDirection, InOutPlantPlane);
		const Vec3 LastPlaneIntersection = PointDirectionPlaneIntersection(LastAlignedFootTransform.Translation, TraceDirection, LastPlantPlane);
		const Vec3 PrevPlaneIntersection = PointDirectionPlaneIntersection(FootTransformWS.Translation, TraceDirection, LastPlantPlane);
		const double LastPlaneDeltaZ = LastPlaneIntersection.Z - CurrPlaneIntersection.Z;
		const double PrevPlaneDeltaZ = PrevPlaneIntersection.Z - CurrPlaneIntersection.Z;
		double AdjustedPrevZ = std::abs(LastPlaneDeltaZ) < std::abs(PrevPlaneDeltaZ) ? LastPlaneIntersection.Z : PrevPlaneIntersection.Z;
		if (bIsOnGround) {
			const double GroundPlaneDelta = CurrPlaneIntersection.Z - AdjustedPrevZ;
			if (GroundPlaneDelta > 0.0) AdjustedPrevZ += std::min(std::abs(ComponentMoveDeltaWS.Z), GroundPlaneDelta);
			else AdjustedPrevZ += std::max(-std::abs(ComponentMoveDeltaWS.Z), GroundPlaneDelta);
		}
		CurrPlaneIntersection.Z = FloatSpringInterp(AdjustedPrevZ, CurrPlaneIntersection.Z, L.GroundHeightSpringState,
			InterpolationSettings.FloorLinearStiffness, InterpolationSettings.FloorLinearDamping, Dt);
		if (MaxGroundPenetration >= 0.0 && bFoundGround) {
			const Plane GroundPlane(ImpactLocationWS, InOutPlantPlane.N);
			const double DistanceToGroundPlane = GetDistanceToPlaneAlongDirection(CurrPlaneIntersection, GroundPlane, ApproachDirCS);
			const double PenetrationAmount = -DistanceToGroundPlane - MaxGroundPenetration;
			if (PenetrationAmount > 0.0) CurrPlaneIntersection = CurrPlaneIntersection - ApproachDirCS * PenetrationAmount;
		}
		const Quat FloorNormalRotation = Quat::FindBetweenNormals(LastPlantPlane.N, InOutPlantPlane.N);
		const Quat FloorSpringNormalRotation = QuaternionSpringInterp(Quat(), FloorNormalRotation, L.GroundRotationSpringState,
			InterpolationSettings.FloorAngularStiffness, InterpolationSettings.FloorAngularDamping, Dt);
		InOutPlantPlane = Plane(CurrPlaneIntersection, FloorSpringNormalRotation.RotateVector(LastPlantPlane.N));
	}
}

void FootPlacement::AlignPlantToGround(const Plane& PlantPlaneWS, const Leg& L, Xform& InOutFootTransformWS, Quat& OutTwistCorrection) const {
	const Xform InputPoseFootTransformWS = L.FootTransformCS * C->ComponentToWorld;
	const Xform IKFootRootWS = IKRootTransformCS * C->ComponentToWorld;
	const Plane IKFootRootPlaneWS(IKFootRootWS.Translation, IKFootRootWS.TransformVectorNoScale(Vec3(0, 0, 1)));
	const double IKFootRootToFootRootTargetDistance = GetDistanceToPlaneAlongDirection(InputPoseFootTransformWS.Translation, IKFootRootPlaneWS, ApproachDirWS);
	const Vec3 CorrectedPlaneIntersectionWS = PointDirectionPlaneIntersection(InOutFootTransformWS.Translation, ApproachDirWS, PlantPlaneWS);
	const Vec3 CorrectedLocationWS = CorrectedPlaneIntersectionWS - ApproachDirWS * IKFootRootToFootRootTargetDistance;
	const Quat PlanePlaneDeltaRotation = Quat::FindBetweenNormals(IKFootRootPlaneWS.N, PlantPlaneWS.N);
	const Quat InputPoseAlignedRotationWS = PlanePlaneDeltaRotation * InputPoseFootTransformWS.Rotation;
	const Quat UnalignedDelta = InputPoseAlignedRotationWS.Inverse() * InOutFootTransformWS.Rotation;
	const Vec3 IKReferenceNormalFootSpace = InputPoseAlignedRotationWS.UnrotateVector(PlantPlaneWS.N);
	Quat Swing;
	UnalignedDelta.ToSwingTwist(IKReferenceNormalFootSpace, Swing, OutTwistCorrection);
	const Quat AlignedRotationWS = InputPoseAlignedRotationWS * OutTwistCorrection;
	const Quat AlignedToUnaligned = AlignedRotationWS.Inverse() * InOutFootTransformWS.Rotation;
	const Vec3 FootToBallDir = L.FootToBall.Translation.GetSafeNormal();
	Quat AnkleTwist;
	AlignedToUnaligned.ToSwingTwist(FootToBallDir, Swing, AnkleTwist);
	const Quat TwistCorrectedRotationWS = AlignedRotationWS * Quat::Slerp(Quat(), AnkleTwist, PlantSettings.AnkleTwistReduction);
	InOutFootTransformWS = Xform(TwistCorrectedRotationWS, CorrectedLocationWS);
}

void FootPlacement::ProcessFootAlignment(Leg& L) {
	const Xform& R2C = RootToComponent();
	const Xform InputPoseFootTransformWS = L.FootTransformCS * C->ComponentToWorld;
	const Xform LastAlignedFootTransformWS = L.AlignedFootTransformWS;
	const Xform LastUnalignedFootTransformWS = L.UnalignedFootTransformWS;
	const Xform InputPoseFootTransformRS = L.FootTransformCS.GetRelativeTransform(R2C);

	L.LastType = L.Type;
	DeterminePlantType(InputPoseFootTransformWS, LastAlignedFootTransformWS, L);
	const bool bIsPlanted = L.Type != PlantType::Unplanted;

	if (bIsPlanted) {
		// PivotAroundBall
		const Xform CurrentPlantedTransformWS = GetFootPivotAroundBallWS(L, LastUnalignedFootTransformWS);
		const Xform PlantedFootTransformCS = CurrentPlantedTransformWS * C->ComponentToWorld.Inverse();
		Xform PlantedFootTransformRS = PlantedFootTransformCS.GetRelativeTransform(R2C);
		const Vec3 AlignedBoneLocationRS = PlantedFootTransformRS.Translation;
		// FPlane(InputPoseFootTransformRS.GetLocation(), ApproachDirWS)
		PlantedFootTransformRS.Translation = Vec3::PointPlaneProject(AlignedBoneLocationRS, InputPoseFootTransformRS.Translation, ApproachDirWS);
		L.UnalignedFootOffset = RelativeReverse(InputPoseFootTransformRS, PlantedFootTransformRS);
		L.PlantOffsetTranslationSpringState.Reset();
		L.PlantOffsetRotationSpringState.Reset();
		L.TimeSinceFullyUnaligned = 0.0;
	} else {
		L.UnalignedFootOffset = UpdatePlantOffsetInterpolation(L);
		if (L.TimeSinceFullyUnaligned > 0.0 || IsNearlyZero(L.AlignmentAlpha)) L.TimeSinceFullyUnaligned += Dt;
	}

	const Xform FootUnalignedTransformRS = L.FootTransformCS.GetRelativeTransform(R2C) * L.UnalignedFootOffset;
	Xform Blended;
	Blended.Blend(InputPoseFootTransformRS, FootUnalignedTransformRS, L.LockAlpha);
	L.UnalignedFootTransformRS = Blended;
	L.UnalignedFootTransformWS = Blended * R2C * C->ComponentToWorld;

	const Xform ComponentToWorldInv = C->ComponentToWorld.Inverse();
	Plane PlantPlaneWS = L.PlantPlaneRS.TransformBy(R2C).TransformBy(C->ComponentToWorld);
	UpdatePlantingPlaneInterpolation(L.UnalignedFootTransformWS, LastAlignedFootTransformWS, PlantPlaneWS, L);
	const Plane PlantPlaneCS = PlantPlaneWS.TransformBy(ComponentToWorldInv);
	L.PlantPlaneRS = PlantPlaneCS.TransformBy(R2C.Inverse());

	L.AlignedFootTransformWS = L.UnalignedFootTransformWS;
	AlignPlantToGround(PlantPlaneWS, L, L.AlignedFootTransformWS, L.TwistCorrection);
	const Xform AlignedFootTransformCS = L.AlignedFootTransformWS * ComponentToWorldInv;
	L.AlignedFootTransformRS = AlignedFootTransformCS.GetRelativeTransform(R2C);
	L.AlignedFootTransformWS = AlignedFootTransformCS * C->ComponentToWorld;
}

void FootPlacement::FindPelvisOffsetRangeForLimb(const Leg& L, const Vec3& InPlantTargetLocationCS, const Xform& PelvisTransformCS, double& OutMax, double& OutMin, double& OutDesired) const {
	Vec3 PlantTargetLocationCS = InPlantTargetLocationCS;
	const Xform HipToPelvis = L.HipTransformCS.GetRelativeTransform(PelvisFKTransformCS);
	const Xform HipTransformCS = HipToPelvis * PelvisTransformCS;
	const Vec3 HipLocationCS = HipTransformCS.Translation;
	const Vec3 DesiredExtensionDelta = L.FootTransformCS.Translation - L.HipTransformCS.Translation;
	const double DesiredExtension = DesiredExtensionDelta.Size();
	const double MaxExtension = GetMaxLimbExtension(DesiredExtension, L.LimbLength);
	const Plane FootPlane(PlantTargetLocationCS, ApproachDirCS * -1.0);
	const double HipHeight = FootPlane.PlaneDot(HipLocationCS);
	Vec3 DesiredPlantTargetLocationCS = PlantTargetLocationCS;
	Vec3 MaxPlantTargetLocationCS = PlantTargetLocationCS;
	if (HipHeight > 0.0) {
		auto Project = [&](const Vec3& P) { return P - FootPlane.N * FootPlane.PlaneDot(P); };
		const Vec3 FKFootProjected = Project(L.FootTransformCS.Translation);
		const Vec3 HipProjected = Project(HipLocationCS);
		const double InitialFootOffset = Vec3::Dist(HipProjected, FKFootProjected);
		const Vec3 ToFKFoot = FKFootProjected - PlantTargetLocationCS;
		const Vec3 ToFKFootDir = ToFKFoot.GetSafeNormal();
		const double ToFKDistance = ToFKFoot.Size();
		PlantTargetLocationCS = PlantTargetLocationCS + ToFKFootDir * (std::min(ToFKDistance, L.FootLength) * HeelLiftRatio);
		const Vec3 TargetFootToHip = HipProjected - PlantTargetLocationCS;
		const double TargetFootOffset = Vec3::Dist(HipProjected, PlantTargetLocationCS);
		auto Adjust = [&](double LegLength, double MaxHipOffset, const Vec3& FootLocation) {
			Vec3 Adjusted = FootLocation;
			const double MinHeight = HipHeight - MaxHipOffset;
			const double MaxFootOffset = std::sqrt(std::max(0.0, LegLength * LegLength - MinHeight * MinHeight));
			const double MaxFootOffsetClamped = std::max(InitialFootOffset, MaxFootOffset);
			if (TargetFootOffset > MaxFootOffsetClamped) Adjusted += TargetFootToHip.GetSafeNormal() * (TargetFootOffset - MaxFootOffsetClamped);
			return Adjusted;
		};
		MaxPlantTargetLocationCS = Adjust(MaxExtension, MaxOffsetHorizontal, PlantTargetLocationCS);
		DesiredPlantTargetLocationCS = Adjust(DesiredExtension, MaxOffsetHorizontal, PlantTargetLocationCS);
	}
	Vec3 MaxOffsetLocation, DesiredOffsetLocation;
	SphereDistToLine(MaxPlantTargetLocationCS, MaxExtension, HipLocationCS - ApproachDirCS * TraceEndOffset, ApproachDirCS, MaxOffsetLocation);
	SphereDistToLine(DesiredPlantTargetLocationCS, DesiredExtension, HipLocationCS - ApproachDirCS * TraceEndOffset, ApproachDirCS, DesiredOffsetLocation);
	OutMax = (MaxOffsetLocation - HipLocationCS) | (ApproachDirCS * -1.0);
	OutDesired = (DesiredOffsetLocation - HipLocationCS) | (ApproachDirCS * -1.0);
	const double MinExtension = GetMinLimbExtension(DesiredExtension, L.LimbLength);
	const Vec3 MinOffsetLocation = DesiredPlantTargetLocationCS + ApproachDirCS * -MinExtension;
	const double MinOffset = (MinOffsetLocation - HipLocationCS) | (ApproachDirCS * -1.0);
	OutMin = MinOffset - L.DistanceToPlant;
}

Xform FootPlacement::SolvePelvis() {
	Xform Rebalanced = PelvisFKTransformCS;
	Vec3 PelvisOffsetDelta;
	{
		Vec3 OffsetAverage;
		for (const Leg& L : Legs)
			OffsetAverage += (RootToComponent().TransformPosition(L.AlignedFootTransformRS.Translation) - L.FootTransformCS.Translation) * 0.5;
		PelvisOffsetDelta = (OffsetAverage - ApproachDirCS * (ApproachDirCS | OffsetAverage)) * HorizontalRebalancingWeight;
		Rebalanced.Translation += PelvisOffsetDelta;
	}
	double MaxOffsetMin = BIG_NUMBER, DesiredOffsetMin = BIG_NUMBER, DesiredOffsetAvg = 0, MinOffsetMax = -BIG_NUMBER;
	for (const Leg& L : Legs) {
		double Mx, Mn, Ds;
		FindPelvisOffsetRangeForLimb(L, RootToComponent().TransformPosition(L.AlignedFootTransformRS.Translation), Rebalanced, Mx, Mn, Ds);
		DesiredOffsetAvg += Ds * 0.5;
		DesiredOffsetMin = std::min(DesiredOffsetMin, Ds);
		MaxOffsetMin = std::min(MaxOffsetMin, Mx);
		MinOffsetMax = std::max(MinOffsetMax, Mn);
	}
	const double MinToAvg = DesiredOffsetAvg - DesiredOffsetMin;
	const double MinToMax = MaxOffsetMin - DesiredOffsetMin;
	DesiredOffsetMin -= 0.05;
	const double Divisor = MinToAvg + MinToMax;
	double PelvisOffsetZ = IsNearlyZero(Divisor) ? DesiredOffsetMin : DesiredOffsetMin + (MinToAvg * MinToMax) / Divisor;
	PelvisOffsetZ = Clamp(PelvisOffsetZ, MinOffsetMax, MaxOffsetMin);
	PelvisOffsetDelta += ApproachDirCS * -PelvisOffsetZ;
	Xform Out = PelvisFKTransformCS;
	Out.Translation += PelvisOffsetDelta;
	return Out;
}

Xform FootPlacement::UpdatePelvisInterpolationRootSpace(const Xform& TargetPelvisTransformRS) {
	const Vec3 PelvisLocationRS = RootToComponent().InverseTransformPosition(PelvisFKTransformCS.Translation);
	Xform Out = TargetPelvisTransformRS;
	Vec3 DesiredPelvisOffset = TargetPelvisTransformRS.Translation - PelvisLocationRS;
	if (DesiredPelvisOffset.SizeSquared() > PelvisMaxOffset * PelvisMaxOffset) DesiredPelvisOffset = DesiredPelvisOffset.GetClampedToMaxSize(PelvisMaxOffset);
	PelvisTranslationOffset = VectorSpringInterp(PelvisTranslationOffset, DesiredPelvisOffset, PelvisTranslationSpringState, PelvisLinearStiffness, PelvisLinearDamping, Dt);
	Out.Translation = PelvisLocationRS + PelvisTranslationOffset;
	return Out;
}

Xform FootPlacement::FinalizeFootAlignment(Leg& L, const Xform& PelvisTransformCS) {
	const Xform FKPelvisToHipCS = L.HipTransformCS.GetRelativeTransform(PelvisFKTransformCS);
	const Xform FinalHipTransformCS = FKPelvisToHipCS * PelvisTransformCS;
	Xform Corrected = L.AlignedFootTransformRS * RootToComponent();
	const Vec3 CorrectedBallLocationCS = (L.FootToBall * Corrected).Translation;
	const Vec3 InputPoseHipToFoot = L.FootTransformCS.Translation - L.HipTransformCS.Translation;
	const Vec3 CorrectedHipToFootDir = (Corrected.Translation - FinalHipTransformCS.Translation).GetSafeNormal();
	if (!InputPoseHipToFoot.IsNearlyZero() && !CorrectedHipToFootDir.IsNearlyZero()) {
		const double DesiredExtension = InputPoseHipToFoot.Size();
		const double MaxExtension = GetMaxLimbExtension(DesiredExtension, L.LimbLength);
		const double CurrentExtension = Vec3::Dist(Corrected.Translation, FinalHipTransformCS.Translation);
		double HyperExtensionRemaining = CurrentExtension - MaxExtension;
		if (CurrentExtension > MaxExtension) {
			const bool bIsPlanted = L.Type != PlantType::Unplanted;
			if (!bIsPlanted) L.bCanReachTarget = false;
			const bool bRecentlyUnplanted = !bIsPlanted && L.TimeSinceFullyUnaligned == 0.0;
			const bool bCanLiftHeel = bRecentlyUnplanted || (bIsPlanted && L.bCanReachTarget) || PlantSettings.bAdjustHeelBeforePlanting;
			if (bCanLiftHeel) {
				const double MaxPull = std::min(L.FootLength, HyperExtensionRemaining) * L.AlignmentAlpha;
				HyperExtensionRemaining -= MaxPull;
				const Vec3 CorrectedFootLocationCS = Corrected.Translation - CorrectedHipToFootDir * MaxPull;
				const Vec3 InitialFootToToe = CorrectedBallLocationCS - Corrected.Translation;
				const Vec3 CorrectedFootToToe = CorrectedBallLocationCS - CorrectedFootLocationCS;
				const Quat DeltaSlopeRotation = Quat::FindBetweenVectors(InitialFootToToe, CorrectedFootToToe);
				Corrected.Rotation = DeltaSlopeRotation * Corrected.Rotation;
				Corrected.NormalizeRotation();
				Corrected.Translation = CorrectedFootLocationCS;
			}
			if (HyperExtensionRemaining > 0.0) {
				Vec3 NotHyper;
				SphereDistToLine(FinalHipTransformCS.Translation, MaxExtension, Corrected.Translation, CorrectedHipToFootDir, NotHyper);
				Corrected.Translation = NotHyper;
			}
		} else {
			L.bCanReachTarget = true;
		}
	}
	const Plane PlantPlaneCS = L.PlantPlaneRS.TransformBy(RootToComponent());
	const double FootDistance = GetDistanceToPlaneAlongDirection(CorrectedBallLocationCS, PlantPlaneCS, ApproachDirCS);
	const double BallDistance = GetDistanceToPlaneAlongDirection(Corrected.Translation, PlantPlaneCS, ApproachDirCS);
	double MinDistance = std::min(FootDistance, BallDistance);
	MinDistance -= std::min(0.0, L.DistanceToPlant);
	if (MinDistance < 0.0) Corrected.Translation += ApproachDirCS * MinDistance;
	{
		const double MinExtension = GetMinLimbExtension(std::abs(InputPoseHipToFoot | ApproachDirCS), L.LimbLength);
		const Plane HipPlane(FinalHipTransformCS.Translation + ApproachDirCS * MinExtension, ApproachDirCS);
		const double DistanceToHipPlane = HipPlane.PlaneDot(Corrected.Translation);
		if (DistanceToHipPlane < 0.0) Corrected.Translation = Corrected.Translation - ApproachDirCS * DistanceToHipPlane;
	}
	return Corrected;
}

void FootPlacement::Evaluate(CSPose& Pose, const struct Pose& Curves, double DeltaTime, const FootPlacementCharacterInputs& Char, const FootTraceFn& Trace) {
	C = &Char;
	TraceFn = &Trace;
	Dt = DeltaTime;
	// teleport check
	if (!bIsFirstUpdate && TeleportDistanceThreshold > 0.0) {
		const Vec3 RootWS = Char.ComponentToWorld.TransformPosition(Pose.Get(0).Translation);
		const Vec3 PrevRootWS = ComponentTransformWS.TransformPosition(RootToComponent().Translation);
		if (Vec3::DistSquared(RootWS, PrevRootWS) > TeleportDistanceThreshold * TeleportDistanceThreshold) ResetRuntimeData();
	}
	ApproachDirCS = Vec3(0, 0, -1);
	ApproachDirWS = Char.ComponentToWorld.TransformVectorNoScale(ApproachDirCS);

	PelvisFKTransformCS = Pose.Get(Pelvis);
	IKRootTransformCS = Pose.Get(IKRoot);
	RootTransformCS = Pose.Get(0);
	for (Leg& L : Legs) GatherLeg(Pose, Curves, L);
	UnplantRadiusSqrd = PlantSettings.UnplantRadius * PlantSettings.UnplantRadius;
	ReplantRadiusSqrd = UnplantRadiusSqrd * PlantSettings.ReplantRadiusRatio * PlantSettings.ReplantRadiusRatio;
	CosHalfUnplantAngle = std::cos(DegToRad(PlantSettings.UnplantAngle / 2.0));
	CosHalfReplantAngle = std::cos(DegToRad((PlantSettings.UnplantAngle * PlantSettings.ReplantAngleRatio) / 2.0));

	ProcessCharacterState();
	for (Leg& L : Legs) ProcessFootAlignment(L);

	Xform PelvisTransformCS = SolvePelvis();
	{
		const Xform TargetRS = PelvisTransformCS.GetRelativeTransform(RootToComponent());
		PelvisTransformCS = UpdatePelvisInterpolationRootSpace(TargetRS) * RootToComponent();
	}
	// OutBoneTransforms (sorted by bone index): pelvis, then the ik feet
	Xform FootOut[2];
	for (int i = 0; i < 2; ++i) FootOut[i] = FinalizeFootAlignment(Legs[i], PelvisTransformCS);
	struct BT { int Bone; Xform X; };
	BT Out[3] = { { Pelvis, PelvisTransformCS }, { Legs[0].IK, FootOut[0] }, { Legs[1].IK, FootOut[1] } };
	std::sort(std::begin(Out), std::end(Out), [](const BT& a, const BT& b) { return a.Bone < b.Bone; });
	for (const BT& b : Out) Pose.Set(b.Bone, b.X);

	for (int i = 0; i < 2; ++i) {
		Debug[i].InputFootWS = Char.ComponentToWorld.TransformPosition(Legs[i].FootTransformCS.Translation);
		Debug[i].OutputFootWS = Char.ComponentToWorld.TransformPosition(FootOut[i].Translation);
		Debug[i].Plant = Legs[i].Type;
		Debug[i].Speed = Legs[i].Speed;
		Debug[i].AlignmentAlpha = Legs[i].AlignmentAlpha;
	}
	DebugPelvisInWS = Char.ComponentToWorld.TransformPosition(PelvisFKTransformCS.Translation);
	DebugPelvisOutWS = Char.ComponentToWorld.TransformPosition(PelvisTransformCS.Translation);
	bIsFirstUpdate = false;
}

// ------------------------------------------------------------------ LegIK
bool LegIK::Init() {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const char* IK[2] = { "ik_foot_l", "ik_foot_r" };
	const char* FK[2] = { "foot_l", "foot_r" };
	for (int i = 0; i < 2; ++i) {
		LegData& L = Legs[i];
		L.IKFoot = Lib.FindBone(IK[i]);
		L.Bones[0] = Lib.FindBone(FK[i]);
		if (L.IKFoot < 0 || L.Bones[0] < 0) return false;
		L.Bones[1] = Lib.Parents[L.Bones[0]];
		L.Bones[2] = L.Bones[1] >= 0 ? Lib.Parents[L.Bones[1]] : -1;
		if (L.Bones[2] < 0) return false;
	}
	return true;
}

static bool RotateLegByQuat(const Quat& Delta, Xform* T) {
	if (Delta.IsIdentity()) return false;
	const Vec3 Hip = T[2].Translation;
	for (int i = 0; i < 3; ++i) {
		T[i].Rotation = Delta * T[i].Rotation;
		T[i].Translation = Hip + Delta.RotateVector(T[i].Translation - Hip);
	}
	return true;
}

static bool RotateLegByDeltaNormals(const Vec3& A, const Vec3& B, Xform* T) {
	const bool bEquals = std::abs(A.X - B.X) <= KINDA_SMALL_NUMBER && std::abs(A.Y - B.Y) <= KINDA_SMALL_NUMBER && std::abs(A.Z - B.Z) <= KINDA_SMALL_NUMBER;
	if (!A.IsZero() && !bEquals) return RotateLegByQuat(Quat::FindBetweenNormals(A, B), T);
	return false;
}

void LegIK::Evaluate(CSPose& Pose) {
	for (LegData& L : Legs) {
		const Xform IKFoot = Pose.Get(L.IKFoot);
		Xform T[3] = { Pose.Get(L.Bones[0]), Pose.Get(L.Bones[1]), Pose.Get(L.Bones[2]) };
		// OrientLegTowardsIK
		bool bModified = false;
		{
			const Vec3 Hip = T[2].Translation;
			bModified |= RotateLegByDeltaNormals((T[0].Translation - Hip).GetSafeNormal(), (IKFoot.Translation - Hip).GetSafeNormal(), T);
		}
		// DoLegReachIK (bEnableRotationLimit -> solver always runs; two-bone path)
		{
			Vec3 Links[3] = { T[0].Translation, T[1].Translation, T[2].Translation };
			const double Len0 = Vec3::Dist(Links[0], Links[1]); // knee to foot
			const double Len1 = Vec3::Dist(Links[1], Links[2]); // hip to knee
			const double Total = Len0 + Len1;
			const Vec3 Root = Links[2];
			Vec3 Target = IKFoot.Translation;
			// SoftenIKEffectorPosition
			if (SoftPercentLength < 1.0 && SoftAlpha > 0.0 && Total > KINDA_SMALL_NUMBER) {
				Vec3 ToEff = Target - Root;
				const double CurLen = ToEff.Size();
				ToEff = CurLen > SMALL_NUMBER ? ToEff * (1.0 / CurLen) : Vec3();
				const double SoftDistance = Total * (1.0 - std::min(1.0, SoftPercentLength));
				const double HardLength = Total - SoftDistance;
				const double CurrentDelta = CurLen - HardLength;
				if (CurrentDelta > KINDA_SMALL_NUMBER && SoftDistance > KINDA_SMALL_NUMBER) {
					const double Softened = HardLength + SoftDistance * (1.0 - std::exp(-CurrentDelta / SoftDistance));
					Target = Root + ToEff * Softened;
				}
			}
			// SolveTwoBoneIK (HingeRotationAxis = thigh Z axis)
			const Vec3 Hinge = T[2].Rotation.GetAxisZ();
			Vec3& pA = Links[0]; Vec3& pB = Links[1]; const Vec3& pC = Links[2];
			pA = Target;
			const Vec3 HipToFoot = pA - pC;
			const double a = Len1, b = HipToFoot.Size(), c = Len0;
			const double TwoAB = 2.0 * a * b;
			const double CosC = !IsNearlyZero(TwoAB) ? (a * a + b * b - c * c) / TwoAB : 0.0;
			const double Cang = std::acos(Clamp(CosC, -1.0, 1.0));
			const Vec3 HipToFootDir = !IsNearlyZero(b) ? HipToFoot * (1.0 / b) : Vec3();
			const Vec3 HipToKnee = pB - pC;
			const Vec3 ProjKnee = pC + HipToKnee.ProjectOnToNormal(HipToFootDir);
			Vec3 BendDir = (pB - ProjKnee).GetSafeNormal(KINDA_SMALL_NUMBER);
			if (!HipToFootDir.IsZero() && !IsNearlyZero(a)) {
				const double KneeBendDot = (HipToKnee * (1.0 / a)) | HipToFootDir;
				if (!BendDir.IsZero() && KneeBendDot < 0.99) {
					L.CachedRealBendDir = BendDir;
					L.CachedBaseBendDir = Hinge ^ HipToFootDir;
				} else if (!L.CachedRealBendDir.IsZero()) {
					const Vec3 CurrentBase = Hinge ^ HipToFootDir;
					BendDir = Quat::FindBetweenNormals(L.CachedBaseBendDir, CurrentBase).RotateVector(L.CachedRealBendDir);
				}
			}
			pB = pC + (HipToFootDir * CosC + BendDir * std::sin(Cang)) * a;
			// rotations
			for (int i = 1; i >= 0; --i) {
				const Vec3 InitialDir = (T[i].Translation - T[i + 1].Translation).GetSafeNormal();
				const Vec3 TargetDir = (Links[i] - Links[i + 1]).GetSafeNormal();
				T[i + 1].Rotation = Quat::FindBetweenNormals(InitialDir, TargetDir) * T[i + 1].Rotation;
			}
			for (int i = 1; i >= 0; --i) T[i].Translation = Links[i];
			bModified = true;
		}
		// AdjustKneeTwist (foot forward axis Y)
		{
			const Vec3 Hip = T[2].Translation;
			const Vec3 FootAxisZ = (IKFoot.Translation - Hip).GetSafeNormal();
			Vec3 FKX = T[0].Rotation.GetAxisY(), IKX = IKFoot.Rotation.GetAxisY();
			FKX = (FootAxisZ ^ FKX) ^ FootAxisZ;
			IKX = (FootAxisZ ^ IKX) ^ FootAxisZ;
			bModified |= RotateLegByDeltaNormals(FKX, IKX, T);
		}
		T[0].Rotation = IKFoot.Rotation;
		(void)bModified;
		// SafeSetCSBoneTransforms, sorted by index: thigh, calf, foot
		Pose.Set(L.Bones[2], T[2]);
		Pose.Set(L.Bones[1], T[1]);
		Pose.Set(L.Bones[0], T[0]);
	}
}

} // namespace gasp
