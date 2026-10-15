#include "anim_graph.h"

namespace gasp {

static Xform RootOffsetForUpdate;

bool AnimGraph::Init() {
	const AnimLibrary& Lib = AnimLibrary::Get();
	BoneIkFootRoot = Lib.FindBone("ik_foot_root");
	BoneIkFootL = Lib.FindBone("ik_foot_l");
	BoneIkFootR = Lib.FindBone("ik_foot_r");
	SpineBones = { Lib.FindBone("spine_01"), Lib.FindBone("spine_02"), Lib.FindBone("spine_03"), Lib.FindBone("spine_04"), Lib.FindBone("spine_05") };
	SpineWeights.assign(SpineBones.size(), 1.f / SpineBones.size());
	CurveEnableOW = Lib.FindCurve("Enable_OrientationWarping");
	bFeetReady = Feet.Init() && LegIk.Init();

	std::vector<int> Collected = { Lib.FindBone("foot_r"), Lib.FindBone("foot_l"), Lib.FindBone("thigh_r"), Lib.FindBone("thigh_l"), Lib.FindBone("spine_05"), Lib.FindBone("pelvis") };
	History.Init(Collected, { Lib.FindCurve("Phase") });

	Stack.MaxActiveBlends = 4;
	Stack.MaxBlendInTimeToOverrideAnimation = 0.03f;
	Stack.PlayerDepthBlendInTimeMultiplier = 1.1f;
	Stack.MakeGraphState = [](int) { return std::make_shared<SampleGraphState>(); };
	Stack.UpdateSampleGraph = [this](BlendStackPlayer& P, float DT, bool bMain) { UpdateSampleGraph(P, DT, bMain); };
	Stack.EvaluateSampleGraph = [this](BlendStackPlayer& P, Pose& Po) { EvaluateSampleGraph(P, Po); };
	// "State Machine Blend Stack" (FAnimNode_BlendStack defaults: MaxBlendInTimeToOverrideAnimation 0.016, depth multiplier 1)
	SMStack.MaxActiveBlends = 4;
	SMStack.MaxBlendInTimeToOverrideAnimation = 0.016f;
	SMStack.PlayerDepthBlendInTimeMultiplier = 1.f;
	SMStack.MakeGraphState = [](int) { return std::make_shared<SampleGraphState>(); };
	SMStack.UpdateSampleGraph = [this](BlendStackPlayer& P, float DT, bool bMain) { UpdateSampleGraphSM(P, DT, bMain); };
	SMStack.EvaluateSampleGraph = [this](BlendStackPlayer& P, Pose& Po) { EvaluateSampleGraphSM(P, Po); };
	SMStack.PreAdvance = [this](BlendStackPlayer& P) { P.PlayRate = GetDynamicPlayRate(P); };
	CurveEnableTIPSteering = Lib.FindCurve("Enable_TurnInPlaceSteering");
	CurveEnablePlayRateWarping = Lib.FindCurve("Enable_PlayRateWarping");
	CurveMoveDataSpeed = Lib.FindCurve("MoveData_Speed");
	CurveMaxDynamicPlayRate = Lib.FindCurve("MaxDynamicPlayRate");
	CurveMinDynamicPlayRate = Lib.FindCurve("MinDynamicPlayRate");
	Output.Init(Lib.NumBones(), Lib.NumCurves());
	Output.Bones = Lib.RefPose;
	LeanBS.AssetIdx = BlendSpaceLibrary::Get().Find("BS1D_Additive_Lean_Run");
	AOBS.AssetIdx = BlendSpaceLibrary::Get().Find("BS_Neutral_AO_Stand");
	LeanAlpha.BlendInTime = 0.2f; LeanAlpha.BlendOutTime = 0.2f; LeanAlpha.Option = BlendOption::HermiteCubic;
	bInitialized = true;
	return true;
}

void AnimGraph::Reset(const Xform& ComponentTransform) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	MM.Reset();
	Montage.StopImmediately();
	Feet.Reset();
	Stack.Reset();
	SMStack.Reset();
	History.Entries.clear();
	History.Traj.Samples.clear();
	// bInitializeWithRefPose
	std::vector<Xform> CS;
	Lib.LocalToComponent(Lib.RefPose, CS);
	Pose Empty; Empty.Init(Lib.NumBones(), Lib.NumCurves());
	History.Evaluate(0.f, CS, Empty);
	OffsetComponentTransform = ComponentTransform;
	SimulatedRotation = ComponentTransform.Rotation;
	SimulatedTranslation = ComponentTransform.Translation;
	SimulatedRoot = ComponentTransform;
	bOffsetFirst = false;
	Output.Bones = Lib.RefPose;
	CurrentDatabase = -1;
	CurrentDatabaseTags.clear();
}

// ---------------------------------------------------------------------------------------------
// MM sample graph: OrientationWarping -> ResetRoot -> Steering (main) -> Steering (turn in place)
void AnimGraph::UpdateSampleGraph(BlendStackPlayer& P, float DeltaTime, bool bMain) {
	SampleGraphState& G = *P.Graph;
	G.Steer1.Alpha.BlendInTime = 0.1f; G.Steer1.Alpha.BlendOutTime = 0.1f;
	G.Steer0.Alpha.BlendInTime = 0.1f; G.Steer0.Alpha.BlendOutTime = 0.1f;
	G.SteerAlpha1 = G.Steer1.Alpha.Update(In.bEnableSteering, DeltaTime);
	G.SteerAlpha0 = G.Steer0.Alpha.Update(In.bTurnInPlaceSteering, DeltaTime);
}

static float SignedAngleRadBetweenNormals(const Vec3& From, const Vec3& To, const Vec3& Axis) {
	const double FromDotTo = From | To;
	const double Angle = std::acos(Clamp(FromDotTo, -1.0, 1.0));
	const Vec3 Cross = From ^ To;
	return (float)((Cross | Axis) >= 0 ? Angle : -Angle);
}

static Quat QuaternionSpringInterp(Quat Current, Quat Target, SteeringState& S, float Stiffness, float Damping, float DeltaTime) {
	if ((Target | Current) < 0.0) Target = Target * -1.0;
	Quat VelocityQuat(S.AngularVelocity.X, S.AngularVelocity.Y, S.AngularVelocity.Z, 0.0);
	Quat Velocity = (Current * VelocityQuat) * 0.5;
	// GenericSpringInterp (bInitializeFromTarget = true, Mass = 1, TargetVelocityAmount = 0)
	if (!S.bPrevTargetValid) Current = Target;
	if (DeltaTime > 1e-8f) {
		const double Omega = std::sqrt((double)Stiffness);
		const double W = Omega; // Frequency*2PI
		if (W < 1e-8) {
			Current = Current + Velocity * DeltaTime;
		} else {
			// critical damping (Damping == 1)
			const Quat Err = Current - Target;
			const Quat C2 = Velocity + Err * W;
			const double E = InvExpApprox(W * DeltaTime);
			Current = Target + (Err + C2 * DeltaTime) * E;
			Velocity = (C2 - Err * W - C2 * (W * DeltaTime)) * E;
		}
		S.PrevTarget = Target;
		S.bPrevTargetValid = true;
	}
	(void)Damping;
	Current.Normalize();
	VelocityQuat = (Current.Inverse() * 2.0) * Velocity;
	S.AngularVelocity = Vec3(VelocityQuat.X, VelocityQuat.Y, VelocityQuat.Z);
	return Current;
}

static void EvaluateSteering(SteeringState& S, float Alpha, Quat TargetOrientation, float Stiffness, float DisableBelowSpeed, float ProceduralTargetTime, float AnimatedTargetTime,
	int Anim, float AnimTime, const Xform& RootBoneTransform, Pose& Po, float DeltaSeconds) {
	if (DeltaSeconds <= 0) return;
	if (Alpha <= ZERO_ANIMWEIGHT_THRESH) return; // SkeletalControlBase skips evaluation at zero alpha
	if (S.bResetFilter) {
		S.FilteredTarget = TargetOrientation;
		S.AngularVelocity = Vec3(); S.bPrevTargetValid = false;
		S.bResetFilter = false;
	}
	S.FilteredTarget = QuaternionSpringInterp(S.FilteredTarget, TargetOrientation, S, Stiffness, 1.f, DeltaSeconds);
	TargetOrientation = S.FilteredTarget;
	if (!Po.bHasRootMotion) return;
	Xform ThisFrame = Po.RootMotion;
	if (ThisFrame.Translation.Length() / DeltaSeconds <= DisableBelowSpeed) return;
	const Quat RootBoneRotation = RootBoneTransform.Rotation;
	Quat DeltaToTarget = RootBoneRotation.Inverse() * TargetOrientation;
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (AnimatedTargetTime > 0 && Anim >= 0) {
		const Xform Predicted = Lib.ExtractRootMotion(Anim, AnimTime, AnimatedTargetTime, true);
		const double PredictedYaw = Predicted.Rotation.Yaw();
		if (std::abs(PredictedYaw) > 1.0) {
			double YawToTarget = DeltaToTarget.Yaw();
			if (PredictedYaw - YawToTarget > 180) YawToTarget += 360;
			else if (YawToTarget - PredictedYaw > 180) YawToTarget -= 360;
			const double Ratio = YawToTarget / PredictedYaw;
			double Pi, Ya, Ro; ThisFrame.Rotation.ToRotator(Pi, Ya, Ro);
			Ya *= Ratio;
			ThisFrame.Rotation = Quat::MakeFromEulerPYR(Pi, Ya, Ro);
			Po.RootMotion = ThisFrame;
			return;
		}
	}
	if (ProceduralTargetTime > DeltaSeconds) DeltaToTarget = Quat::Slerp(Quat(), DeltaToTarget, DeltaSeconds / ProceduralTargetTime);
	ThisFrame.Rotation = Quat::Slerp(ThisFrame.Rotation, DeltaToTarget, Alpha);
	Po.RootMotion = ThisFrame;
}

void AnimGraph::EvaluateOrientationWarping(SampleGraphState& G, BlendStackPlayer& P, Pose& Po, CSPose& CS, const Xform& WarpingSpace) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const float DeltaSeconds = Dt;
	float OWAlpha = Clamp(Lib.SampleCurve(P.Anim, CurveEnableOW, P.AccumulatedTime), 0.f, 1.f);
	if (In.DebugDisable & 1) OWAlpha = 0.f;
	if (OWAlpha > ZERO_ANIMWEIGHT_THRESH && Po.bHasRootMotion) {
		OrientationWarpingState& S = G.OW;
		const Vec3 Axis(0, 0, 1);
		const float LocomotionAngleDeltaThreshold = 135.f, MinRootMotionSpeedThreshold = 10.f, RotationInterpSpeed = 8.f;
		const float CounterCompensateInterpSpeed = 45.f, MaxRootMotionDeltaToCompensateDegrees = 45.f, DistributedAlpha = 0.5f;
		const float MaxAngleCorrectionRad = (float)DegToRad(180.0);
		Vec3 LocomotionForward;
		if (In.LocomotionDirection.SizeSquared() > 1e-8) {
			LocomotionForward = WarpingSpace.InverseTransformVector(In.LocomotionDirection);
			LocomotionForward.Normalize();
		} else {
			// LocomotionAngle = 0 path: mesh relative rotation (yaw -90) unrotate of forward
			LocomotionForward = Quat::MakeFromYaw(-90).UnrotateVector(Vec3(1, 0, 0)).GetSafeNormal();
		}
		LocomotionForward = (LocomotionForward - Axis * (Axis | LocomotionForward)).GetSafeNormal();
		Xform RMDelta = Po.RootMotion;
		Vec3 RMT = RMDelta.Translation;
		const Quat PrevRMRot = S.RootMotionDeltaRotation;
		S.RootMotionDeltaRotation = RMDelta.Rotation;
		RMT = RMT - Axis * (Axis | RMT);
		float TargetAngle = 0.f;
		const float Speed = DeltaSeconds > 0 ? (float)(RMT.Size() / DeltaSeconds) : 0.f;
		if (Speed < MinRootMotionSpeedThreshold) {
			TargetAngle = 0.f;
		} else {
			const Vec3 PrevDir = S.RootMotionDeltaDirection;
			{
				const double SS = RMT.SizeSquared();
				S.RootMotionDeltaDirection = SS < 1e-8 ? PrevDir : RMT * (1.0 / std::sqrt(SS));
			}
			TargetAngle = SignedAngleRadBetweenNormals(S.RootMotionDeltaDirection, LocomotionForward, Axis);
			if (std::abs(RadToDeg(TargetAngle)) > LocomotionAngleDeltaThreshold) {
				TargetAngle = (float)UnwindRadians(TargetAngle + DegToRad(180.0));
				S.RootMotionDeltaDirection = -S.RootMotionDeltaDirection;
			}
			if (!PrevDir.IsNearlyZero(1e-8)) {
				const float RootMotionDeltaAngle = SignedAngleRadBetweenNormals(S.RootMotionDeltaDirection, PrevRMRot.UnrotateVector(PrevDir), Axis);
				if (std::abs(RootMotionDeltaAngle) < DegToRad(MaxRootMotionDeltaToCompensateDegrees)) {
					S.CounterCompensateTargetAngleRad += RootMotionDeltaAngle;
					const float CC = (float)FInterpTo(0.0, S.CounterCompensateTargetAngleRad, DeltaSeconds, CounterCompensateInterpSpeed);
					S.ActualOrientationAngleRad = (float)UnwindRadians(S.ActualOrientationAngleRad + CC);
					S.CounterCompensateTargetAngleRad -= CC;
				}
			}
			RMDelta.Translation = Quat(Axis, TargetAngle).RotateVector(RMT);
		}
		Po.RootMotion = RMDelta;
		if (!S.bIsFirstUpdate) {
			const float Smooth = (float)FInterpTo(S.ActualOrientationAngleRad, TargetAngle, DeltaSeconds, RotationInterpSpeed);
			S.ActualOrientationAngleRad = Clamp(Smooth, S.ActualOrientationAngleRad - MaxAngleCorrectionRad, S.ActualOrientationAngleRad + MaxAngleCorrectionRad);
		} else S.ActualOrientationAngleRad = TargetAngle;
		S.ActualOrientationAngleRad = Clamp(S.ActualOrientationAngleRad, -MaxAngleCorrectionRad, MaxAngleCorrectionRad);
		S.ActualOrientationAngleRad *= OWAlpha;
		const float A = S.ActualOrientationAngleRad;
		const float RootOffset = (float)UnwindRadians(A * DistributedAlpha);
		if (!IsNearlyZero(RootOffset, KINDA_SMALL_NUMBER)) {
			Xform R = CS.Get(0);
			R.Rotation = (Quat(Axis, RootOffset) * R.Rotation).GetNormalized();
			CS.Set(0, R);
		}
		for (size_t i = 0; i < SpineBones.size(); ++i) {
			Xform X = CS.Get(SpineBones[i]);
			X.Rotation = (Quat(Axis, -A * DistributedAlpha * SpineWeights[i]) * X.Rotation).GetNormalized();
			CS.Set(SpineBones[i], X);
		}
		const float FootAlpha = 1.f - DistributedAlpha;
		if (BoneIkFootRoot >= 0 && !IsNearlyZero(FootAlpha, KINDA_SMALL_NUMBER)) {
			Xform X = CS.Get(BoneIkFootRoot);
			X.Rotation = (Quat(Axis, A * FootAlpha) * X.Rotation).GetNormalized();
			CS.Set(BoneIkFootRoot, X);
			for (int Fb : { BoneIkFootL, BoneIkFootR }) {
				Xform F = CS.Get(Fb);
				F.Rotation = (Quat(Axis, -A * FootAlpha) * F.Rotation).GetNormalized();
				CS.Set(Fb, F);
			}
		}
		S.bIsFirstUpdate = false;
	}

}

void AnimGraph::EvaluateResetRoot(CSPose& CS) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (!(In.DebugDisable & 32)) {
		std::vector<std::pair<int, Xform>> Children;
		for (int b = 1; b < Lib.NumBones(); ++b) if (Lib.Parents[b] == 0) Children.push_back({ b, CS.Get(b) });
		CS.Set(0, Lib.RefPose[0]);
		for (auto& C : Children) CS.Set(C.first, C.second);
	}

}

void AnimGraph::EvaluateSampleGraph(BlendStackPlayer& P, Pose& Po) {
	SampleGraphState& G = *P.Graph;
	const AnimLibrary& Lib = AnimLibrary::Get();
	const float DeltaSeconds = Dt;
	CSPose CS;
	CS.Init(Po.Bones);

	// ---- Orientation warping (WarpingSpace bound to Get_OrientationWarpingWarpingSpace) ----
	EvaluateOrientationWarping(G, P, Po, CS, In.bOffsetRootEnabled ? RootOffsetForUpdate : In.ComponentTransform);
	EvaluateResetRoot(CS);

	// ---- Steering (main) ----
	if (!(In.DebugDisable & 2)) {
	EvaluateSteering(G.Steer1, G.SteerAlpha1, In.DesiredFacing, 700.f, 10.f, 0.2f, 0.5f, P.Anim, P.AccumulatedTime, RootOffsetForUpdate, Po, DeltaSeconds);
	// ---- Steering (turn in place) ----
	EvaluateSteering(G.Steer0, G.SteerAlpha0, In.DesiredFacing, 0.f, -1.f, 1000000.f, 0.5f, P.Anim, P.AccumulatedTime, RootOffsetForUpdate, Po, DeltaSeconds);
	}

	CS.ToLocal(Po.Bones);
}

// ---------------------------------------------------------------------------------------------
// "State Machine Blend Stack" sample graph: BlendStackInput (PlayRate = Get_DynamicPlayRate) -> Steering (EnableSteering)
// -> Steering (alpha = Enable_TurnInPlaceSteering curve) -> OrientationWarping (RootBoneTransform) -> ResetRoot
float AnimGraph::GetDynamicPlayRate(const BlendStackPlayer& P) const {
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (P.Anim < 0) return 1.f;
	// GetCurveValueFromAnimation returns false when the curve does not exist in the sequence -> play rate 1
	auto Get = [&](int Curve, float& V) { bool bFound = false; V = Curve >= 0 ? Lib.SampleCurve(P.Anim, Curve, P.AccumulatedTime, &bFound) : 0.f; return Curve >= 0 && bFound; };
	float AlphaCurve, SpeedCurve, MaxRate, MinRate;
	if (!Get(CurveEnablePlayRateWarping, AlphaCurve) || !Get(CurveMoveDataSpeed, SpeedCurve)) return 1.f;
	if (!Get(CurveMaxDynamicPlayRate, MaxRate)) MaxRate = 1.25f;
	if (!Get(CurveMinDynamicPlayRate, MinRate)) MinRate = 0.75f;
	const float Den = Clamp(SpeedCurve, 1.f, 999.f);
	const float Ratio = In.Speed2D / Den;
	return 1.f + (Clamp(Ratio, MinRate, MaxRate) - 1.f) * AlphaCurve;
}

void AnimGraph::UpdateSampleGraphSM(BlendStackPlayer& P, float DeltaTime, bool bMain) {
	SampleGraphState& G = *P.Graph;
	G.Steer1.Alpha.BlendInTime = 0.1f; G.Steer1.Alpha.BlendOutTime = 0.1f;
	G.SteerAlpha1 = G.Steer1.Alpha.Update(In.bEnableSteering, DeltaTime);
	// float alpha input (FAnimNode_SkeletalControlBase AlphaScaleBias default -> clamped curve value)
	G.SteerAlpha0 = Clamp(AnimLibrary::Get().SampleCurve(P.Anim, CurveEnableTIPSteering, P.AccumulatedTime), 0.f, 1.f);
	(void)bMain;
}

void AnimGraph::EvaluateSampleGraphSM(BlendStackPlayer& P, Pose& Po) {
	SampleGraphState& G = *P.Graph;
	CSPose CS;
	CS.Init(Po.Bones);
	if (!(In.DebugDisable & 2)) {
		EvaluateSteering(G.Steer1, G.SteerAlpha1, In.DesiredFacing, 700.f, 10.f, 0.2f, 0.5f, P.Anim, P.AccumulatedTime, RootOffsetForUpdate, Po, Dt);
		EvaluateSteering(G.Steer0, G.SteerAlpha0, In.DesiredFacing, 700.f, -1.f, 100000.f, 0.5f, P.Anim, P.AccumulatedTime, RootOffsetForUpdate, Po, Dt);
	}
	EvaluateOrientationWarping(G, P, Po, CS, RootOffsetForUpdate);
	EvaluateResetRoot(CS);
	CS.ToLocal(Po.Bones);
}

// FAnimNode_BlendStack_Standalone notify filtering (bShouldFilterNotifies, FBlendStackAnimEventsFilterScope)
void AnimGraph::UpdateNotifyFilter(BlendStack& S, double& Time, std::vector<std::string>& LastTick, std::map<std::string, double>& Recency, float TimeOut, std::vector<FiredNotify>& Out, float DeltaTime) {
	Time += DeltaTime;
	for (const std::string& N : LastTick) Recency[N] = Time + TimeOut;
	LastTick.clear();
	for (auto It = Recency.begin(); It != Recency.end();) {
		if (Time >= It->second) It = Recency.erase(It); else ++It;
	}
	S.Update(DeltaTime);
	Out.clear();
	for (const FiredNotify& F : S.Fired) {
		const std::string& N = F.Event->Name;
		if (Recency.count(N) || std::find(LastTick.begin(), LastTick.end(), N) != LastTick.end()) continue;
		LastTick.push_back(N);
		Out.push_back(F);
	}
}

// ---------------------------------------------------------------------------------------------
static float DamperImplicit(float Halflife, float DeltaTime, float Eps = 1e-8f) {
	Halflife = std::max(Halflife, DeltaTime);
	return Clamp((float)(1.0 - InvExpApprox((0.69314718056 * DeltaTime) / (Halflife + Eps))), 0.f, 1.f);
}
static bool ShouldExtractRM(OffsetRootMode M) { return M == OffsetRootMode::Accumulate || M == OffsetRootMode::Interpolate || M == OffsetRootMode::LockOffsetAndConsumeAnimation || M == OffsetRootMode::LockOffsetIncreaseAndConsumeAnimation; }
static bool ShouldCounterDelta(OffsetRootMode M) { return !(M == OffsetRootMode::Accumulate || M == OffsetRootMode::Interpolate); }

void AnimGraph::EvaluateOffsetRoot(Pose& Po) {
	const Xform InputBone = Po.Bones[0];
	const Xform LastComponent = OffsetComponentTransform;
	OffsetComponentTransform = In.ComponentTransform;
	const Xform& Comp = OffsetComponentTransform;
	const OffsetRootMode TM = In.OffsetTranslationMode, RM = In.OffsetRotationMode;
	const Xform RMDelta = Po.bHasRootMotion ? Po.RootMotion : Xform();
	float MaxTranslationOffset = In.OffsetMaxTranslationError;
	Xform Consumed;
	if (ShouldExtractRM(TM)) Consumed.Translation = RMDelta.Translation;
	if (ShouldExtractRM(RM)) Consumed.Rotation = RMDelta.Rotation;
	if (ShouldCounterDelta(RM)) SimulatedRotation = (LastComponent.Rotation.Inverse() * Comp.Rotation) * SimulatedRotation;
	if (ShouldCounterDelta(TM)) SimulatedTranslation += Comp.Translation - LastComponent.Translation;
	if (TM == OffsetRootMode::LockOffsetIncreaseAndConsumeAnimation) {
		Vec3 DeltaDir = (SimulatedTranslation - Comp.Translation).GetSafeNormal();
		DeltaDir = SimulatedRotation.UnrotateVector(DeltaDir).GetSafeNormal();
		Consumed.Translation = DeltaDir * (Consumed.Translation | DeltaDir);
	}
	Xform Sim(SimulatedRotation, SimulatedTranslation);
	Sim = Consumed * Sim;
	SimulatedTranslation = Sim.Translation;
	SimulatedRotation = Sim.Rotation;
	SimulatedTranslation = Vec3::PointPlaneProject(SimulatedTranslation, Comp.Translation, Vec3(0, 0, -1));
	if (TM == OffsetRootMode::Release || TM == OffsetRootMode::Interpolate) {
		const Vec3 Offset = Comp.Translation - SimulatedTranslation;
		const float DA = DamperImplicit(In.OffsetTranslationHalflife, Dt);
		Vec3 OffsetDelta = Offset * DA;
		if (In.bOffsetClampToTranslationVelocity) {
			const double MaxDelta = 0.5 * RMDelta.Translation.Size();
			if (OffsetDelta.Size() > MaxDelta) OffsetDelta = OffsetDelta.GetSafeNormal2D() * MaxDelta;
		}
		SimulatedTranslation = SimulatedTranslation + OffsetDelta;
	}
	if (RM == OffsetRootMode::Release || RM == OffsetRootMode::Interpolate) {
		Quat RotOffset = Comp.Rotation * SimulatedRotation.Inverse();
		RotOffset.Normalize();
		if (RotOffset.W < 0) RotOffset = RotOffset * -1.0;
		const float DA = DamperImplicit(In.OffsetRotationHalflife, Dt);
		SimulatedRotation = Quat::Slerp(Quat(), RotOffset, DA) * SimulatedRotation;
	}
	if (MaxTranslationOffset >= 0.f) {
		Vec3 Offset = Comp.Translation - SimulatedTranslation;
		if (Offset.Size() > MaxTranslationOffset) {
			Offset = Offset.GetClampedToMaxSize(MaxTranslationOffset);
			SimulatedTranslation = Comp.Translation - Offset;
		}
	}
	Sim = Xform(SimulatedRotation, SimulatedTranslation);
	SimulatedRoot = Sim;
	// TargetBoneTransform.Accumulate(Sim * Comp^-1)
	const Xform Rel = Sim * Comp.Inverse();
	Xform Target = InputBone;
	Target.Rotation = Rel.Rotation * Target.Rotation;
	Target.Translation += Rel.Translation;
	Target.Scale3D = InputBone.Scale3D;
	Target.NormalizeRotation();
	Po.Bones[0] = Target;
}

// ---------------------------------------------------------------------------------------------
void AnimGraph::EvaluateAdditiveLayers(Pose& P, float DeltaTime) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	// ApplyMeshSpaceAdditive_2: Lean BS1D, alpha = IsMoving (bool blend 0.2/0.2 HermiteCubic)
	const float LA = LeanAlpha.Update(In.bIsMoving, DeltaTime);
	if (LeanBS.AssetIdx >= 0) {
		LeanBS.Update(In.LeanAmount, 0.0, DeltaTime);
		if (LA > ZERO_ANIMWEIGHT_THRESH && !(In.DebugDisable & 4)) {
			std::vector<Xform> Add;
			LeanBS.EvaluateAdditive(Add);
			AccumulateMeshSpaceRotationAdditive(P.Bones, Add, LA);
		}
	}
	// ApplyMeshSpaceAdditive_0 (Aim Offset): DeadBlending(BlendListByBool(Enable_AO: AO BS, Identity))
	if (AOBS.AssetIdx < 0) return;
	InertialAdditiveSwitch& Sw = AOSwitch;
	const bool bWant = In.bEnableAO;
	if (!Sw.bInit) { Sw.bInit = true; Sw.bActive = bWant; MakeIdentityAdditive(Sw.Last); }
	if (bWant != Sw.bActive) {
		Sw.bActive = bWant;
		if (bWant) AOBS.Reset(); // bResetChildOnActivation
		Sw.Snapshot = Sw.Last;
		Sw.Elapsed = 0.f;
		Sw.Duration = bWant ? 0.75f : 1.5f;
		static const AnimLibrary::BlendProfile* Profile = Lib.FindBlendProfile("FastHead_Weight");
		Sw.BoneDuration.assign(Lib.NumBones(), Sw.Duration);
		if (Profile) for (int b = 0; b < Lib.NumBones(); ++b) {
			if (!Profile->Has[b]) continue;
			if (Profile->bWeightFactor) { if (Profile->Scales[b] > 1e-8f) Sw.BoneDuration[b] /= Profile->Scales[b]; }
			else Sw.BoneDuration[b] *= Profile->Scales[b];
		}
		Sw.bBlending = true;
	}
	std::vector<Xform> Cur;
	if (Sw.bActive) {
		AOBS.Update(In.AOYaw, In.AOPitch, DeltaTime);
		AOBS.EvaluateAdditive(Cur);
	} else MakeIdentityAdditive(Cur);
	if (Sw.bBlending) {
		Sw.Elapsed += DeltaTime;
		bool bDone = true;
		for (int b = 0; b < Lib.NumBones(); ++b) {
			const float D = Sw.BoneDuration[b];
			const float T = D > 1e-5f ? Clamp(Sw.Elapsed / D, 0.f, 1.f) : 1.f;
			if (T < 1.f) bDone = false;
			const float A = (float)BlendSpaceLibrary::Get().EvalAOBlendCurve(T);
			Xform X = Sw.Snapshot[b];
			X.BlendWith(Cur[b], A);
			if (A > ZERO_ANIMWEIGHT_THRESH && A < 1.f - ZERO_ANIMWEIGHT_THRESH) X.Scale3D = Vec3::Lerp(Sw.Snapshot[b].Scale3D, Cur[b].Scale3D, A);
			Cur[b] = X;
		}
		if (bDone) Sw.bBlending = false;
	}
	Sw.Last = Cur;
	if (!(In.DebugDisable & 8)) AccumulateMeshSpaceRotationAdditive(P.Bones, Cur, 1.f);
}

void AnimGraph::Update(float DeltaTime, const GraphInputs& Inputs) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	In = Inputs;
	Dt = DeltaTime;
	RootOffsetForUpdate = Xform(SimulatedRotation, SimulatedTranslation);

	// ---- update phase ----
	History.Traj = In.Traj; // PoseHistoryCollector::Update_AnyThread SetTrajectory
	if (LastPathSM != (In.bUseStateMachine ? 1 : 0)) {
		if (LastPathSM >= 0) {
			if (In.bUseStateMachine) { SMStack.Reset(); SMNotifyRecencyMap.clear(); SMNotifiesFiredLastTick.clear(); }
			else { MM.Reset(); Stack.Reset(); NotifyRecencyMap.clear(); NotifiesFiredLastTick.clear(); CurrentDatabase = -1; CurrentDatabaseTags.clear(); }
		}
		LastPathSM = In.bUseStateMachine ? 1 : 0;
	}
	// FAnimNode_Slot::Update_AnyThread (DefaultSlot, bAlwaysUpdateSourcePose = false): the source pose - the whole
	// locomotion branch (motion matching / state machine, blend stacks, notifies) - is not updated at all while the
	// montages on the slot leave it no relevant weight (FAnimInstanceProxy::GetSlotWeight: SourceWeight = 1 - montage
	// weight, normalized when the sum exceeds 1). Its time freezes, no searches run and no footstep notifies fire.
	{
		const float MontageW = Montage.IsSlotActive() ? Montage.GetWeight() + Montage.GetOutgoingWeight() : 0.f;
		SourceWeight = 1.f - std::min(MontageW, 1.f);
		bSourceUpdated = SourceWeight > ZERO_ANIMWEIGHT_THRESH;
		if (!bSourceUpdated) {
			MMNotifies.clear();
			SMNotifies.clear();
			goto Evaluate;
		}
	}
	if (In.bUseStateMachine) {
		// TwoWayBlend(bool) -> B: TwoWayBlend(always update children) -> state machine (logic in GDScript) + "State Machine Blend Stack"
		// FAnimNode_BlendStack::UpdateAssetPlayer: blend when there is no player, when forced or when the asset changed
		const bool bBlend = SMStack.Players.empty() || In.bSMForceBlend || SMStack.GetAnim() != In.SMAnim;
		if (In.SMAnim >= 0 && bBlend) {
			const AnimLibrary::BlendProfile* Profile = In.SMBlendProfile.empty() ? nullptr : Lib.FindBlendProfile(In.SMBlendProfile);
			SMStack.BlendTo(In.SMAnim, std::max(0.f, In.SMStartTime), In.bSMLoop, In.SMBlendTime, Profile, BlendOption::QuadraticInOut, 1.f);
		}
		UpdateNotifyFilter(SMStack, SMGameTime, SMNotifiesFiredLastTick, SMNotifyRecencyMap, 0.1f, SMNotifies, DeltaTime);
		MMNotifies = SMNotifies;
		goto Evaluate;
	}
	// FAnimNode_MotionMatching::UpdateAssetPlayer
	MM.AdjustAssetTime(Stack.GetAccumulatedTime());
	MM.Update(DeltaTime, In.Databases, In.Interrupt, &History, 0.3f, 0.f, 0.85f, 1.15f, &In.Overrides);
	if (MM.CurrentSearchResult.IsValid()) {
		CurrentDatabase = MM.CurrentSearchResult.Database;
		CurrentDatabaseTags = MotionMatchingLibrary::Get().Db(CurrentDatabase).Tags;
	}
	if (MM.bJumpedToPose) {
		const DbAsset* A = GetSearchIndexAsset(MM.CurrentSearchResult);
		if (A && A->Anim >= 0) {
			static const AnimLibrary::BlendProfile* Profile = AnimLibrary::Get().FindBlendProfile("FastFeet+Root_Weight");
			Stack.BlendTo(A->Anim, MM.CurrentSearchResult.AssetTime, A->bLooping, In.MMBlendTime, Profile, BlendOption::HermiteCubic, MM.WantedPlayRate);
		}
	}
	Stack.UpdatePlayRate(MM.WantedPlayRate);
	// FAnimNode_BlendStack_Standalone::UpdateAssetPlayer notify filtering (bShouldFilterNotifies)
	GameTime += DeltaTime;
	for (const std::string& N : NotifiesFiredLastTick) NotifyRecencyMap[N] = GameTime + In.NotifyRecencyTimeOut;
	NotifiesFiredLastTick.clear();
	for (auto It = NotifyRecencyMap.begin(); It != NotifyRecencyMap.end();) {
		if (GameTime >= It->second) It = NotifyRecencyMap.erase(It); else ++It;
	}
	Stack.Update(DeltaTime);
	MMNotifies.clear();
	for (const FiredNotify& F : Stack.Fired) {
		// the tick record weight includes the slot's source weight (Context.FractionalWeight(SourceWeight))
		if (F.Weight * SourceWeight < F.Event->TriggerWeightThreshold) continue;
		const std::string& N = F.Event->Name;
		if (NotifyRecencyMap.count(N) || std::find(NotifiesFiredLastTick.begin(), NotifiesFiredLastTick.end(), N) != NotifiesFiredLastTick.end()) continue;
		NotifiesFiredLastTick.push_back(N);
		MMNotifies.push_back(F);
	}

	// ---- evaluate phase ----
Evaluate:
	Pose P;
	if (In.bUseStateMachine) SMStack.Evaluate(P);
	else Stack.Evaluate(P);
	EvaluateAdditiveLayers(P, DeltaTime);
	if (PostMMLayers) PostMMLayers(P, DeltaTime);
	Montage.EvaluateSlot(P); // Slot 'DefaultSlot'
	if (In.bOffsetRootEnabled && !(In.DebugDisable & 16)) EvaluateOffsetRoot(P);
	CSPose CS;
	CS.Init(P.Bones);
	// FootPlacement (PlantSettings/InterpolationSettings _Stops when the current database is tagged 'Stops') -> LegIK
	if (In.bFootPlacementEnabled && bFeetReady) {
		// Get_FootPlacementPlantSettings: Select(CurrentDatabaseTags has 'Stops', BlendStackInputs.Tags has 'Stop', UseExperimentalStateMachine)
		const bool bStops = In.bUseStateMachine ? In.bSMStops : std::find(CurrentDatabaseTags.begin(), CurrentDatabaseTags.end(), "Stops") != CurrentDatabaseTags.end();
		FootPlacementPlantSettings& PS = Feet.PlantSettings;
		PS.SpeedThreshold = bStops ? 20 : 60; PS.DistanceToGround = 10; PS.UnplantRadius = bStops ? 40 : 20; PS.ReplantRadiusRatio = bStops ? 0.75 : 0.2;
		PS.UnplantAngle = 60; PS.ReplantAngleRatio = 0.2; PS.MaxExtensionRatio = 0.5; PS.MinExtensionRatio = 0.2; PS.SeparatingDistance = 0;
		PS.UnalignmentSpeedThreshold = 200; PS.AnkleTwistReduction = 0.75;
		Feet.InterpolationSettings.UnplantLinearStiffness = bStops ? 250 : 100;
		Feet.InterpolationSettings.UnplantAngularStiffness = 450;
		FootPlacementCharacterInputs FC = In.FootChar;
		FC.ComponentToWorld = In.ComponentTransform;
		Feet.Evaluate(CS, P, DeltaTime, FC, FootTrace);
	} else {
		Feet.Reset();
	}
	if (bFeetReady) LegIk.Evaluate(CS);
	if (PostOffsetRoot) PostOffsetRoot(CS, DeltaTime);
	CS.ToLocal(P.Bones);
	Output = P;
	OutputCS = CS.CS;
	History.Evaluate(DeltaTime, OutputCS, Output);
	(void)Lib;
}

} // namespace gasp
