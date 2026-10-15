#include "pbik.h"
#include <algorithm>

namespace pbik {

static constexpr float GLOBAL_UNITS = 100.0f;

static float CircularEaseOut(float Input) { return std::sqrt(1.0f - std::pow(Input - 1.0f, 2.0f)); }

static Quat RotatorToQuat(const Rotator& R) { return Quat::MakeFromEulerPYR(R.Pitch, R.Yaw, R.Roll); }
static Rotator QuatToRotator(const Quat& Q) { Rotator R; Q.ToRotator(R.Pitch, R.Yaw, R.Roll); return R; }

static void ToDirectionAndLength(const Vec3& V, Vec3& OutDir, double& OutLen) {
	OutLen = V.Size();
	if (OutLen > SMALL_NUMBER) OutDir = V * (1.0 / OutLen);
	else OutDir = Vec3();
}

// ---------------------------------------------------------------- Bone / RigidBody
void Bone::UpdateFromInputs() {
	if (!Parent) return;
	LocalPositionFromInput = Parent->Rotation.Inverse().RotateVector(Position - Parent->Position);
	LocalRotationFromInput = Parent->Rotation.Inverse() * Rotation;
	Length = (float)LocalPositionFromInput.Size();
}

void RigidBody::Initialize(const Bone* SolverRoot) {
	UpdateTransformAndMassFromBones();
	InitialRotation = Rotation;
	NumBonesToRoot = 0;
	const Bone* P = B;
	while (P && P != SolverRoot) { NumBonesToRoot += 1; P = P->Parent; }
}

void RigidBody::UpdateFromInputs(const SolverSettings& S) {
	UpdateTransformAndMassFromBones();
	constexpr float MinMass = 0.5f;
	InvMass = 1.0f / std::max(MinMass, (Mass * S.MassMultiplier * GLOBAL_UNITS));
	Settings = &S;
}

void RigidBody::UpdateTransformAndMassFromBones() {
	Vec3 Centroid = B->Position;
	Mass = 0.0f;
	for (const Bone* Child : B->Children) {
		Centroid += Child->Position;
		Mass += (float)(B->Position - Child->Position).Size();
	}
	Centroid = Centroid * (1.0 / (B->Children.size() + 1.0));
	Position = InputPosition = Centroid;
	Rotation = InitialRotation = B->Rotation;
	BoneLocalPosition = B->Rotation.Inverse().RotateVector(B->Position - Centroid);
	ChildLocalPositions.clear();
	for (const Bone* Child : B->Children) ChildLocalPositions.push_back(Rotation.Inverse().RotateVector(Child->Position - Centroid));
}

void RigidBody::ApplyPushToRotateBody(const Vec3& Push, const Vec3& Offset) {
	if (IsFullyLocked()) return;
	const Vec3 Omega = (Offset ^ Push) * (InvMass * (1.0f - J.RotationStiffness));
	ApplyRotationDelta(Quat(Omega.X, Omega.Y, Omega.Z, 0.0));
}

void RigidBody::ApplyPositionDelta(const Vec3& DeltaP) {
	if (IsFullyLocked()) return;
	Position += DeltaP * ((1.0f - J.PositionStiffness) * Settings->OverRelaxation);
}

void RigidBody::ApplyRotationDelta(const Quat& DeltaQ) {
	if (IsFullyLocked()) return;
	Quat ClampedDQ = DeltaQ;
	const double MaxPhi = DegToRad(Settings->MaxAngle);
	const double Phi = DeltaQ.Size();
	if (Phi > MaxPhi) ClampedDQ = ClampedDQ * (MaxPhi / Phi);
	Quat Delta = ClampedDQ * Rotation;
	Delta = Delta * 0.5;
	Rotation = Rotation + Delta;
	Rotation.Normalize();
}

// ---------------------------------------------------------------- constraints
JointConstraint::JointConstraint(RigidBody* InA, RigidBody* InB) : A(InA), Bb(InB) {
	const Vec3 PinPoint = Bb->B->Position;
	PinPointLocalToA = A->Rotation.Inverse().RotateVector(PinPoint - A->Position);
	PinPointLocalToB = Bb->Rotation.Inverse().RotateVector(PinPoint - Bb->Position);
	XOrig = Bb->InitialRotation.RotateVector(Vec3(1, 0, 0));
	YOrig = Bb->InitialRotation.RotateVector(Vec3(0, 1, 0));
	ZOrig = Bb->InitialRotation.RotateVector(Vec3(0, 0, 1));
}

void JointConstraint::Solve(const SolverSettings&) {
	if (A->bIsLockedBySubSolve && Bb->bIsLockedBySubSolve) return;
	Vec3 OffsetA, OffsetB;
	Vec3 Correction = GetPositionCorrection(OffsetA, OffsetB);
	A->ApplyPushToRotateBody(Correction, OffsetA);
	Bb->ApplyPushToRotateBody(-Correction, OffsetB);
	UpdateJointLimits();
	const float WA = A->InvMass, WB = Bb->InvMass, W = WA + WB;
	if (IsNearlyZero(W)) return;
	Correction = GetPositionCorrection(OffsetA, OffsetB);
	Vec3 N; double C;
	ToDirectionAndLength(Correction, N, C);
	A->ApplyPositionDelta(N * (C * (WA / W)));
	Bb->ApplyPositionDelta(-(N * (C * (WB / W))));
}

void JointConstraint::RemoveStretch(float Percent) {
	Vec3 ToA, ToB;
	const Vec3 Correction = GetPositionCorrection(ToA, ToB);
	Bb->Position -= Correction * Percent;
}

void JointConstraint::UpdateFromInputs() {
	const Vec3 PinPoint = Bb->B->Position;
	PinPointLocalToA = A->Rotation.Inverse().RotateVector(PinPoint - A->Position);
	PinPointLocalToB = Bb->Rotation.Inverse().RotateVector(PinPoint - Bb->Position);
}

Vec3 JointConstraint::GetPositionCorrection(Vec3& OutBodyToA, Vec3& OutBodyToB) const {
	OutBodyToA = A->Rotation.RotateVector(PinPointLocalToA);
	OutBodyToB = Bb->Rotation.RotateVector(PinPointLocalToB);
	return (Bb->Position + OutBodyToB) - (A->Position + OutBodyToA);
}

void JointConstraint::ApplyRotationCorrection(const Quat& DeltaQ) const {
	A->ApplyRotationDelta(DeltaQ * (1.0f - A->J.RotationStiffness));
	Bb->ApplyRotationDelta(DeltaQ * (-1.0f * (1.0f - Bb->J.RotationStiffness)));
}

void JointConstraint::UpdateJointLimits() {
	BoneSettings& J = Bb->J;
	if (J.X == ELimitType::Free && J.Y == ELimitType::Free && J.Z == ELimitType::Free) return;
	J.MaxX = J.MaxX < J.MinX ? J.MinX + 1 : J.MaxX;
	J.MaxY = J.MaxY < J.MinY ? J.MinY + 1 : J.MaxY;
	J.MaxZ = J.MaxZ < J.MinZ ? J.MinZ + 1 : J.MaxZ;
	const bool bLockX = (J.X == ELimitType::Locked) || (J.X == ELimitType::Limited && ((J.MaxX - J.MinX) < 2.0f));
	const bool bLockY = (J.Y == ELimitType::Locked) || (J.Y == ELimitType::Limited && ((J.MaxY - J.MinY) < 2.0f));
	const bool bLockZ = (J.Z == ELimitType::Locked) || (J.Z == ELimitType::Limited && ((J.MaxZ - J.MinZ) < 2.0f));
	if (bLockX && bLockY && bLockZ) {
		const Quat OrigOffset = A->InitialRotation * Bb->InitialRotation.Inverse();
		const Quat CurrentOffset = A->Rotation * Bb->Rotation.Inverse();
		const Quat DeltaOffset = OrigOffset * CurrentOffset.Inverse();
		ApplyRotationCorrection(Quat(DeltaOffset.X, DeltaOffset.Y, DeltaOffset.Z, 0.0) * 2.0);
		return;
	}
	const bool bXHinge = !bLockX && bLockY && bLockZ;
	const bool bYHinge = bLockX && !bLockY && bLockZ;
	const bool bZHinge = bLockX && bLockY && !bLockZ;
	if (bXHinge) { UpdateLocalRotateAxes(true, false, false); RotateToAlignAxes(XA, XB); }
	else if (bYHinge) { UpdateLocalRotateAxes(false, true, false); RotateToAlignAxes(YA, YB); }
	else if (bZHinge) { UpdateLocalRotateAxes(false, false, true); RotateToAlignAxes(ZA, ZB); }
	const bool bLimitX = J.X == ELimitType::Limited && !bLockX;
	const bool bLimitY = J.Y == ELimitType::Limited && !bLockY;
	const bool bLimitZ = J.Z == ELimitType::Limited && !bLockZ;
	if (bLimitX || bLimitY || bLimitZ) DecomposeRotationAngles();
	if (bLimitX) RotateWithinLimits(J.MinX, J.MaxX, AngleX, XA, ZBProjOnX, ZA);
	if (bLimitY) RotateWithinLimits(J.MinY, J.MaxY, AngleY, YA, ZBProjOnY, ZA);
	if (bLimitZ) RotateWithinLimits(J.MinZ, J.MaxZ, AngleZ, ZA, YBProjOnZ, YA);
}

void JointConstraint::RotateToAlignAxes(const Vec3& AxisA, const Vec3& AxisB) const {
	const Vec3 ACrossB = AxisA ^ AxisB;
	Vec3 Axis; double Magnitude;
	ToDirectionAndLength(ACrossB, Axis, Magnitude);
	const double EffectiveInvMass = Axis | Axis;
	if (EffectiveInvMass < KINDA_SMALL_NUMBER) return;
	const Vec3 Push = Axis * (Magnitude / EffectiveInvMass);
	ApplyRotationCorrection(Quat(Push.X, Push.Y, Push.Z, 0.0));
}

void JointConstraint::RotateWithinLimits(float MinAngle, float MaxAngle, float CurrentAngle, const Vec3& RotAxis, const Vec3& CurVec, const Vec3& RefVec) const {
	const bool bBeyondMin = CurrentAngle < MinAngle, bBeyondMax = CurrentAngle > MaxAngle;
	if (!(bBeyondMin || bBeyondMax)) return;
	const float TgtAngle = bBeyondMin ? MinAngle : MaxAngle;
	const Quat TgtRot(RotAxis, DegToRad(TgtAngle));
	RotateToAlignAxes(TgtRot.RotateVector(RefVec), CurVec);
}

void JointConstraint::UpdateLocalRotateAxes(bool bX, bool bY, bool bZ) {
	const Quat ARot = A->Rotation * A->InitialRotation.Inverse();
	const Quat BRot = Bb->Rotation * Bb->InitialRotation.Inverse();
	if (bX) { XA = ARot.RotateVector(XOrig); XB = BRot.RotateVector(XOrig); }
	if (bY) { YA = ARot.RotateVector(YOrig); YB = BRot.RotateVector(YOrig); }
	if (bZ) { ZA = ARot.RotateVector(ZOrig); ZB = BRot.RotateVector(ZOrig); }
}

void JointConstraint::DecomposeRotationAngles() {
	const Quat ARot = A->Rotation * A->InitialRotation.Inverse();
	const Quat BRot = Bb->Rotation * Bb->InitialRotation.Inverse();
	XA = ARot.RotateVector(XOrig); YA = ARot.RotateVector(YOrig); ZA = ARot.RotateVector(ZOrig);
	XB = BRot.RotateVector(XOrig); YB = BRot.RotateVector(YOrig); ZB = BRot.RotateVector(ZOrig);
	ZBProjOnX = Vec3::VectorPlaneProject(ZB, XA).GetSafeNormal();
	ZBProjOnY = Vec3::VectorPlaneProject(ZB, YA).GetSafeNormal();
	YBProjOnZ = Vec3::VectorPlaneProject(YB, ZA).GetSafeNormal();
	AngleX = SignedAngleBetweenNormals(ZA, ZBProjOnX, XA);
	AngleY = SignedAngleBetweenNormals(ZA, ZBProjOnY, YA);
	AngleZ = SignedAngleBetweenNormals(YA, YBProjOnZ, ZA);
}

float JointConstraint::SignedAngleBetweenNormals(const Vec3& From, const Vec3& To, const Vec3& Axis) {
	const double FromDotTo = From | To;
	const double Angle = RadToDeg(std::acos(Clamp(FromDotTo, -1.0, 1.0))); // FMath::Acos clamps
	const double Dot = (From ^ To) | Axis;
	return (float)(Dot >= 0 ? Angle : -Angle);
}

PinConstraint::PinConstraint(RigidBody* InBody, const Vec3& InPinPositionOrig, const Quat& InPinRotationOrig)
	: GoalPosition(InPinPositionOrig), GoalRotation(InPinRotationOrig), A(InBody) {
	PinPointLocalToA = A->Rotation.Inverse().RotateVector(GoalPosition - A->Position);
}

void PinConstraint::Solve(const SolverSettings& S) {
	if (!bEnabled || Alpha <= KINDA_SMALL_NUMBER) return;
	Vec3 AToPinPoint;
	Vec3 Correction = GetPositionCorrection(AToPinPoint);
	A->ApplyPushToRotateBody(Correction, AToPinPoint);
	Correction = GetPositionCorrection(AToPinPoint);
	A->Position += Correction * S.OverRelaxation;
}

Vec3 PinConstraint::GetPositionCorrection(Vec3& OutBodyToPinPoint) const {
	OutBodyToPinPoint = A->Rotation.RotateVector(PinPointLocalToA);
	return (GoalPosition - (A->Position + OutBodyToPinPoint)) * Alpha;
}

// ---------------------------------------------------------------- effector
Effector::Effector(Bone* InBone) : B(InBone) { SetGoal(B->Position, B->Rotation, Settings); }

void Effector::SetGoal(const Vec3& P, const Quat& R, const EffectorSettings& S) {
	PositionOrig = B->Position;
	RotationOrig = B->Rotation;
	Position = PositionGoal = P;
	Rotation = RotationGoal = R;
	Settings = S;
}

void Effector::UpdateChainStates() {
	ChainRootBody = nullptr;
	const int MaxDepth = Settings.ChainDepth;
	const Bone* Parent = B->Parent;
	int Depth = 1;
	while (Parent) {
		if (!Parent->bIsSolved) break;
		const bool bIsAtAnyRoot = Parent->bIsSubRoot || Parent->bIsSolverRoot;
		const bool bIsAtChainRoot = MaxDepth > 0 ? (Depth > MaxDepth || bIsAtAnyRoot) : bIsAtAnyRoot;
		if (bIsAtChainRoot) { ChainRootBody = Parent->Body; break; }
		Parent->Body->bIsPartOfSubChain = true;
		++Depth;
		Parent = Parent->Parent;
	}
	ChainDepthInitializedWith = Settings.ChainDepth;
}

void Effector::UpdateFromInputs(const Bone&) {
	// FMath::Lerp on vectors / quaternions (component-wise)
	Position = Vec3::Lerp(PositionOrig, PositionGoal, Settings.PositionAlpha);
	Rotation = RotationOrig * (1.0 - Settings.RotationAlpha) + RotationGoal * Settings.RotationAlpha;
	Pin->SetGoal(Position, Rotation, Settings.StrengthAlpha);
	DistToChainRootInInputPose = CalculateDistanceToChainRoot();
	if (ChainRootBody) {
		DistancesFromEffector.clear();
		DistancesFromEffector.push_back(0.0f);
		DistToChainRootAlongBones = B->Length;
		const Bone* Parent = B->Parent;
		while (Parent && Parent->Body != ChainRootBody) {
			DistancesFromEffector.push_back(DistToChainRootAlongBones);
			DistToChainRootAlongBones += Parent->Length;
			Parent = Parent->Parent;
		}
	}
}

float Effector::CalculateDistanceToChainRoot() const {
	if (ChainRootBody) {
		const Effector* ParentEffector = ChainRootBody->Eff;
		const Vec3 ParentSubRootPosition = ParentEffector ? ParentEffector->PositionOrig : ChainRootBody->Position;
		return (float)(ParentSubRootPosition - B->Position).Size();
	}
	return 0.f;
}

void Effector::ApplyPreferredAngles() const {
	if (!ChainRootBody) return;
	if (DistToChainRootInInputPose <= SMALL_NUMBER) return;
	const Effector* ParentEffector = ChainRootBody->Eff;
	const Vec3 ParentSubRootPosition = ParentEffector ? ParentEffector->Position : ChainRootBody->Position;
	const float DistToSubRootCurrent = (float)(ParentSubRootPosition - Position).Size();
	if (DistToSubRootCurrent >= DistToChainRootInInputPose) return;
	const float DeltaSquash = DistToChainRootInInputPose - DistToSubRootCurrent;
	float SquashPercent = DeltaSquash / DistToChainRootInInputPose;
	SquashPercent = CircularEaseOut(SquashPercent);
	if (IsNearlyZero(SquashPercent)) return;
	const Bone* Parent = B->Parent;
	while (Parent && Parent->bIsSolved) {
		RigidBody* Body = Parent->Body;
		if (Body->J.bUsePreferredAngles) {
			const RigidBody* CurrentParent = Body->GetParentBody();
			const Quat CurrentLocalRotation = CurrentParent ? CurrentParent->Rotation.Inverse() * Body->Rotation : Body->Rotation;
			const Quat InitialLocalRotation = CurrentParent ? CurrentParent->InitialRotation.Inverse() * Body->InitialRotation : Body->InitialRotation;
			const Quat LocalOffsetRotation = CurrentLocalRotation * InitialLocalRotation.Inverse();
			const Rotator AnglesFromInput = QuatToRotator(LocalOffsetRotation);
			Rotator Preferred = Body->J.PreferredAngles;
			Preferred.Pitch *= SquashPercent; Preferred.Yaw *= SquashPercent; Preferred.Roll *= SquashPercent;
			auto CalcDeltaAngle = [](double PreferredAngle, double InputAngle) -> double {
				if (std::abs(PreferredAngle) <= SMALL_NUMBER) return 0.0;
				if (std::abs(PreferredAngle) > std::abs(InputAngle)) return PreferredAngle - InputAngle;
				return 0.0;
			};
			Rotator LocalDelta;
			LocalDelta.Pitch = CalcDeltaAngle(Preferred.Pitch, AnglesFromInput.Pitch);
			LocalDelta.Yaw = CalcDeltaAngle(Preferred.Yaw, AnglesFromInput.Yaw);
			LocalDelta.Roll = CalcDeltaAngle(Preferred.Roll, AnglesFromInput.Roll);
			Body->Rotation = Body->Rotation * RotatorToQuat(LocalDelta);
			Body->Rotation.Normalize();
		}
		if (Parent == ChainRootBody->B) return;
		Parent = Parent->Parent;
	}
}

// ---------------------------------------------------------------- solver
void Solver::Solve(const SolverSettings& S) {
	if (!Initialize()) return;
	for (Bone& B : Bones) B.UpdateFromInputs();
	for (RigidBody& Body : Bodies) Body.UpdateFromInputs(S);
	for (auto& C : Constraints) C->UpdateFromInputs();
	if (S.RootBehavior == ERootBehavior::PinToInput) SolverRoot->Body->InvMass = 0.0f;
	UpdateEffectorDepths(false);
	for (Effector& E : Effectors) E.UpdateFromInputs(*SolverRoot);
	UpdateBodies(S);
	UpdateBonesFromBodies();
}

void Solver::UpdateBodies(const SolverSettings& S) {
	ApplyRootPrePull(S.RootBehavior, S.PrePullRootSettings);
	ApplyPreferredAngles();
	ApplyPullChainAlpha(S.GlobalPullChainAlpha);
	SolveConstraints(S);
}

void Solver::UpdateBonesFromBodies() {
	for (RigidBody& Body : Bodies) {
		Body.B->Position = Body.Position + Body.Rotation.RotateVector(Body.BoneLocalPosition);
		Body.B->Rotation = Body.Rotation;
	}
	for (const Effector& E : Effectors) {
		Bone* B = E.B;
		if (B->bIsSolverRoot) continue;
		if (!B->Body) {
			if (B->Parent) B->Position = B->Parent->Position + B->Parent->Rotation.RotateVector(B->LocalPositionFromInput);
			else B->Position = E.Position;
		}
		const Quat RotationWithoutEffector = B->Parent ? B->Parent->Rotation * B->LocalRotationFromInput : B->Rotation;
		const float RotAmount = Clamp(E.Settings.PinRotation, 0.0f, 1.0f);
		B->Rotation = Quat::FastLerp(RotationWithoutEffector, E.Rotation, RotAmount).GetNormalized();
	}
	for (Bone& B : Bones) {
		if (B.bIsSolved || !B.Parent) continue;
		B.Position = B.Parent->Position + B.Parent->Rotation.RotateVector(B.LocalPositionFromInput);
		B.Rotation = B.Parent->Rotation * B.LocalRotationFromInput;
	}
}

void Solver::ApplyRootPrePull(ERootBehavior RootBehavior, const RootPrePullSettings& P) {
	if (RootBehavior != ERootBehavior::PrePull) return;
	std::vector<Vec3> InitialPoints, CurrentPoints;
	for (Effector& E : Effectors) { InitialPoints.push_back(E.PositionOrig); CurrentPoints.push_back(E.Position); }
	Vec3 InitialCentroid, CurrentCentroid;
	const Quat RotationOffset = GetRotationFromDeformedPoints(InitialPoints, CurrentPoints, InitialCentroid, CurrentCentroid);
	Vec3 PositionOffset = (CurrentCentroid - InitialCentroid) * Vec3(P.PositionAlphaX, P.PositionAlphaY, P.PositionAlphaZ);
	PositionOffset *= P.PositionAlpha;
	// FQuat::Euler() = (Roll, Pitch, Yaw); MakeFromEuler takes the same layout
	double Pitch, Yaw, Roll;
	RotationOffset.ToRotator(Pitch, Yaw, Roll);
	Quat FinalRotationOffset = Quat::MakeFromEulerPYR(Pitch * P.RotationAlphaY, Yaw * P.RotationAlphaZ, Roll * P.RotationAlphaX);
	FinalRotationOffset = Quat::FastLerp(Quat(), FinalRotationOffset, P.RotationAlpha).GetNormalized();
	const RigidBody* RootBody = SolverRoot->Body;
	const Vec3 RootPosition = RootBody->Position;
	const Quat RootRotation = RootBody->Rotation;
	const Vec3 NewRootPosition = RootPosition + PositionOffset;
	const Quat NewRootRotation = FinalRotationOffset * RootBody->Rotation;
	for (RigidBody& Body : Bodies) {
		const Vec3 LocalPos = RootRotation.Inverse().RotateVector(Body.Position - RootPosition);
		const Quat LocalRot = RootRotation.Inverse() * Body.Rotation;
		Body.Position = NewRootPosition + NewRootRotation.RotateVector(LocalPos);
		Body.Rotation = NewRootRotation * LocalRot;
	}
}

void Solver::ApplyPullChainAlpha(float GlobalPullChainAlpha) {
	if (IsNearlyZero(GlobalPullChainAlpha)) return;
	for (Effector& E : Effectors) {
		if (!E.ChainRootBody) continue;
		if (E.DistToChainRootAlongBones < SMALL_NUMBER) continue;
		if (E.Settings.PullChainAlpha < SMALL_NUMBER) continue;
		const Vec3 ChainStartOrig = E.ChainRootBody->InputPosition;
		const Vec3 ChainEndOrig = E.PositionOrig;
		Vec3 ChainVecOrig; double ChainLenOrig;
		ToDirectionAndLength(ChainEndOrig - ChainStartOrig, ChainVecOrig, ChainLenOrig);
		Vec3 ChainStartNew;
		if (E.ChainRootBody->Eff) {
			const Effector* RootEffector = E.ChainRootBody->Eff;
			ChainStartNew = E.ChainRootBody->Position + (RootEffector->Position - RootEffector->PositionOrig);
		} else {
			ChainStartNew = E.ChainRootBody->Position;
		}
		Vec3 ChainVecNew; double ChainLenNew;
		ToDirectionAndLength(E.Position - ChainStartNew, ChainVecNew, ChainLenNew);
		Quat ChainDeltaRotation = Quat::FindBetweenNormals(ChainVecOrig, ChainVecNew);
		const float ChainDeltaAlpha = E.Settings.PullChainAlpha * E.Settings.StrengthAlpha * GlobalPullChainAlpha;
		ChainDeltaRotation = Quat::FastLerp(Quat(), ChainDeltaRotation, ChainDeltaAlpha);
		const Vec3 ChainDeltaPosition = ChainVecNew * ((ChainLenNew - ChainLenOrig) * ChainDeltaAlpha);
		const Bone* B = E.B->Parent;
		int ChainIndex = 0;
		const float InvChainLength = 1.0f / E.DistToChainRootAlongBones;
		while (B && B->Body != E.ChainRootBody) {
			const Vec3 BodyRelativeToChain = B->Body->Position - ChainStartNew;
			B->Body->Position = ChainStartNew + ChainDeltaRotation.RotateVector(BodyRelativeToChain);
			B->Body->Rotation = ChainDeltaRotation * B->Body->Rotation;
			const float Strength = 1.0f - (E.DistancesFromEffector[ChainIndex] * InvChainLength);
			B->Body->Position += ChainDeltaPosition * Strength;
			B = B->Parent;
			++ChainIndex;
		}
	}
}

void Solver::ApplyPreferredAngles() {
	for (Effector& E : Effectors) E.ApplyPreferredAngles();
	for (int C = (int)Constraints.size() - 1; C >= 0; --C) Constraints[C]->RemoveStretch(1.0f);
}

void Solver::SolveConstraints(const SolverSettings& S) {
	auto RunConstraintPass = [this, &S](int CurrentIteration, int MaxIterations) {
		for (auto& C : Constraints) C->Solve(S);
		if (!S.bAllowStretch) {
			const float StretchAmount = std::min(1.0f, (float)(CurrentIteration + 1) / (MaxIterations / 2.0f));
			for (int C = (int)Constraints.size() - 1; C >= 0; --C) Constraints[C]->RemoveStretch(StretchAmount);
		}
	};
	if (bHasSubChains && S.SubIterations > 0) {
		for (RigidBody& Body : Bodies) Body.bIsLockedBySubSolve = !Body.bIsPartOfSubChain;
		for (int I = 0; I < S.SubIterations; ++I) RunConstraintPass(I, S.SubIterations);
		for (RigidBody& Body : Bodies) Body.bIsLockedBySubSolve = false;
	}
	for (int I = 0; I < S.Iterations; ++I) RunConstraintPass(I, S.Iterations);
}

bool Solver::Initialize() {
	if (bReadyToSimulate) return true;
	bReadyToSimulate = false;
	if (!InitBones()) return false;
	if (!InitBodies()) return false;
	if (!InitConstraints()) return false;
	bReadyToSimulate = true;
	return true;
}

bool Solver::InitBones() {
	if (Bones.empty() || Effectors.empty()) return false;
	int NumSolverRoots = 0;
	for (Bone& B : Bones) if (B.bIsSolverRoot) { SolverRoot = &B; ++NumSolverRoots; }
	if (!SolverRoot || NumSolverRoots > 1) return false;
	for (Bone& B : Bones) {
		if (B.ParentIndex == -1) continue;
		if (B.ParentIndex < 0 || B.ParentIndex >= (int)Bones.size()) return false;
		B.Parent = &Bones[B.ParentIndex];
	}
	for (const Effector& E : Effectors) {
		Bone* Current = E.B;
		Current->bIsSolved = true;
		while (Current) {
			if (Current->bIsSolverRoot) { Current->bIsSolved = true; break; }
			Current->bIsSolved = true;
			Current = Current->Parent;
		}
	}
	for (Bone& Parent : Bones)
		for (Bone& Child : Bones)
			if (Child.bIsSolved && Child.Parent == &Parent) Parent.Children.push_back(&Child);
	for (Bone& B : Bones) B.bIsSubRoot = B.Children.size() > 1 || B.bIsSolverRoot;
	return true;
}

bool Solver::InitBodies() {
	Bodies.clear();
	Bodies.reserve(Bones.size());
	for (const Effector& E : Effectors) {
		Bone* NextBone = E.B;
		while (true) {
			Bone* BodyBone = NextBone->bIsSolverRoot ? NextBone : NextBone->Parent;
			if (!BodyBone) return false;
			AddBodyForBone(BodyBone);
			NextBone = BodyBone;
			if (NextBone == SolverRoot) break;
		}
	}
	for (RigidBody& Body : Bodies) Body.Initialize(SolverRoot);
	// Bodies.Sort() (ascending NumBonesToRoot, TArray::Sort is not stable) then Algo::Reverse
	std::stable_sort(Bodies.begin(), Bodies.end(), [](const RigidBody& L, const RigidBody& R) { return L.NumBonesToRoot < R.NumBonesToRoot; });
	std::reverse(Bodies.begin(), Bodies.end());
	for (RigidBody& Body : Bodies) Body.B->Body = &Body;
	return true;
}

void Solver::AddBodyForBone(Bone* B) {
	for (const RigidBody& Body : Bodies) if (Body.B == B) return;
	Bodies.emplace_back(B);
}

bool Solver::InitConstraints() {
	Constraints.clear();
	for (Effector& E : Effectors) {
		const Bone* BodyBone = E.B->bIsSolverRoot ? E.B : E.B->Parent;
		if (!BodyBone) return false;
		RigidBody* Body = BodyBone->Body;
		auto C = std::make_unique<PinConstraint>(Body, E.Position, E.Rotation);
		E.Pin = C.get();
		Body->Eff = &E;
		Body->Pin = C.get();
		Constraints.push_back(std::move(C));
	}
	for (RigidBody& Body : Bodies) {
		RigidBody* ParentBody = Body.GetParentBody();
		if (!ParentBody) continue;
		Constraints.push_back(std::make_unique<JointConstraint>(ParentBody, &Body));
	}
	UpdateEffectorDepths(true);
	return true;
}

void Solver::UpdateEffectorDepths(bool bForceUpdate) {
	bool bDirty = false;
	bHasSubChains = false;
	for (const Effector& E : Effectors) {
		if (bForceUpdate || E.ChainDepthInitializedWith != E.Settings.ChainDepth) bDirty = true;
		if (E.Settings.ChainDepth > 0) bHasSubChains = true;
	}
	if (bDirty) {
		for (RigidBody& Body : Bodies) { Body.bIsPartOfSubChain = false; Body.bIsLockedBySubSolve = false; }
		for (Effector& E : Effectors) E.UpdateChainStates();
	}
}

void Solver::Reset() {
	bReadyToSimulate = false;
	SolverRoot = nullptr;
	Constraints.clear();
	Bodies.clear();
	Effectors.clear();
	Bones.clear();
}

int Solver::AddBone(const std::string& Name, int ParentIndex, const Vec3& Pos, const Quat& Rot, bool bIsSolverRoot) {
	Bones.emplace_back(Name, ParentIndex, Pos, Rot, bIsSolverRoot);
	return (int)Bones.size() - 1;
}

int Solver::AddEffector(const std::string& BoneName) {
	for (Bone& B : Bones) if (B.Name == BoneName) { Effectors.emplace_back(&B); return (int)Effectors.size() - 1; }
	return -1;
}

void Solver::SetBoneTransform(int Index, const Xform& T) {
	Bones[Index].Position = T.Translation;
	Bones[Index].Rotation = T.Rotation;
	Bones[Index].Scale = T.Scale3D;
}

BoneSettings* Solver::GetBoneSettings(int Index) {
	if (!bReadyToSimulate || Index < 0 || Index >= (int)Bones.size() || !Bones[Index].Body) return nullptr;
	return &Bones[Index].Body->J;
}

void Solver::GetBoneGlobalTransform(int Index, Xform& Out) const {
	const Bone& B = Bones[Index];
	Out = Xform(B.Rotation, B.Position, B.Scale);
}

int Solver::GetBoneIndex(const std::string& Name) const {
	for (int i = 0; i < (int)Bones.size(); ++i) if (Bones[i].Name == Name) return i;
	return -1;
}

void Solver::SetEffectorGoal(int Index, const Vec3& P, const Quat& R, const EffectorSettings& S) { Effectors[Index].SetGoal(P, R, S); }

Quat Solver::GetRotationFromDeformedPoints(const std::vector<Vec3>& Initial, const std::vector<Vec3>& Current, Vec3& OutInitialCentroid, Vec3& OutCurrentCentroid) {
	if (Initial.size() <= 1) {
		OutInitialCentroid = Initial.size() == 1 ? Initial[0] : Vec3();
		OutCurrentCentroid = Current.size() == 1 ? Current[0] : Vec3();
		return Quat();
	}
	OutInitialCentroid = Vec3(); OutCurrentCentroid = Vec3();
	for (size_t i = 0; i < Initial.size(); ++i) { OutInitialCentroid += Initial[i]; OutCurrentCentroid += Current[i]; }
	const double Inv = 1.0 / (double)Initial.size();
	OutInitialCentroid *= Inv; OutCurrentCentroid *= Inv;
	Vec3 DX, DY, DZ;
	for (size_t i = 0; i < Initial.size(); ++i) {
		const Vec3 P = (Initial[i] - OutInitialCentroid).GetSafeNormal();
		const Vec3 Q = (Current[i] - OutCurrentCentroid).GetSafeNormal();
		DX += Vec3(P.X * Q.X, P.X * Q.Y, P.X * Q.Z);
		DY += Vec3(P.Y * Q.X, P.Y * Q.Y, P.Y * Q.Z);
		DZ += Vec3(P.Z * Q.X, P.Z * Q.Y, P.Z * Q.Z);
	}
	Quat Q;
	for (int Iter = 0; Iter < 50; ++Iter) {
		// FRotationMatrix::Make(Q): rows are the rotated basis axes
		const Vec3 RCol0 = Q.RotateVector(Vec3(1, 0, 0)), RCol1 = Q.RotateVector(Vec3(0, 1, 0)), RCol2 = Q.RotateVector(Vec3(0, 0, 1));
		Vec3 Omega = (RCol0 ^ DX) + (RCol1 ^ DY) + (RCol2 ^ DZ);
		Omega *= 1.0 / (std::fabs((RCol0 | DX) + (RCol1 | DY) + (RCol2 | DZ)) + SMALL_NUMBER);
		const double W = Omega.Size();
		if (W < SMALL_NUMBER) break;
		Q = Quat(Omega * (1.0 / W), W) * Q;
		Q.Normalize();
	}
	return Q;
}

} // namespace pbik
