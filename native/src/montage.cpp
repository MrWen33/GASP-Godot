#include "montage.h"
#include "blend_stack.h"
#include <cmath>

namespace gasp {

void MontagePlayer::SetBlend(float Desired, float Time, BlendOption Opt) {
	BlendBegin = Weight;
	BlendDesired = Desired;
	BlendTime = Time;
	BlendElapsed = 0.f;
	Option = Opt;
	LinearAlpha = Time <= 0.f ? 1.f : 0.f;
	if (Time <= 0.f) Weight = Desired;
}

void MontagePlayer::Play(int MontageIdx, float InPlayRate, float StartPosition) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (MontageIdx < 0 || MontageIdx >= Lib.NumMontages()) return;
	const MontageInfo& M = Lib.Montage(MontageIdx);
	// bShouldStopAllMontages: the playing montage keeps blending out in parallel (Stop with the new blend in settings;
	// a montage that is already blending out only switches to the new blend time when that is shorter)
	if (bPlaying && Weight > ZERO_ANIMWEIGHT_THRESH && Anim >= 0) {
		Out.bActive = true;
		Out.Montage = Montage;
		Out.Anim = Anim;
		Out.Position = Out.PreviousPosition = Position;
		Out.PlayRate = PlayRate;
		Out.Length = Montage >= 0 ? Lib.Montage(Montage).Length : Position;
		Out.Weight = Weight;
		const bool bKeep = bStopping && (BlendTime - BlendElapsed) <= M.BlendInTime;
		if (bKeep) {
			Out.BlendBegin = BlendBegin; Out.BlendDesired = BlendDesired; Out.BlendTime = BlendTime; Out.BlendElapsed = BlendElapsed;
			Out.Option = Option; Out.Profile = Profile; Out.LinearAlpha = LinearAlpha;
		} else {
			Out.BlendBegin = Weight; Out.BlendDesired = 0.f; Out.BlendTime = M.BlendInTime; Out.BlendElapsed = 0.f;
			Out.Option = bStopping ? Option : M.BlendInOption; Out.Profile = bStopping ? Profile : nullptr;
			Out.LinearAlpha = M.BlendInTime <= 0.f ? 1.f : 0.f;
			if (M.BlendInTime <= 0.f) Out.bActive = false;
		}
	}
	Montage = MontageIdx;
	Anim = M.Anim;
	PlayRate = InPlayRate;
	Position = PreviousPosition = std::max(0.f, std::min(StartPosition, M.Length));
	bPlaying = true;
	bStopping = false;
	bEndedThisFrame = false;
	bInterrupted = false;
	Profile = nullptr;
	Weight = 0.f;
	SetBlend(1.f, M.BlendInTime, M.BlendInOption);
	Modifiers.clear();
	BlendOutStatesActive.clear();
}

void MontagePlayer::Stop(float Time, BlendOption Opt, const AnimLibrary::BlendProfile* InProfile) {
	if (!bPlaying) return;
	if (!bStopping) {
		bStopping = true;
		bBlendOutStartedThisFrame = true;
		SetBlend(0.f, Time, Opt);
		Profile = InProfile;
	} else if (Time < BlendTime) {
		SetBlend(0.f, Time, Option);
	}
}

void MontagePlayer::StopImmediately() {
	Out.bActive = false;
	bPlaying = false;
	bStopping = false;
	Weight = 0.f;
	Montage = -1;
	Anim = -1;
	Modifiers.clear();
}

Xform MontagePlayer::RootRelativeToWarpPointAtTime(float Time, const std::string& Bone, const Quat& MeshRotationOffset) const {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const int B = Lib.FindBone(Bone);
	if (B < 0) return Xform();
	// component space pose without root motion extraction (root keeps its animated transform)
	Xform BoneCS = Lib.SampleBone(Anim, B, Time);
	for (int p = Lib.Parents[B]; p >= 0; p = Lib.Parents[p]) {
		const Xform Parent = p == 0 ? Lib.SampleRootRaw(Anim, Time) : Lib.SampleBone(Anim, p, Time);
		BoneCS = BoneCS * Parent;
	}
	const Xform RootCS = Lib.SampleRootRaw(Anim, Time);
	const Xform MeshInv(MeshRotationOffset.Inverse(), Vec3());
	const Xform RootT = MeshInv * RootCS;
	const Xform WarpPointT = MeshInv * BoneCS;
	return RootT.GetRelativeTransform(WarpPointT);
}

// URootMotionModifier::Update + URootMotionModifier_Warp::Update, then UMotionWarpingComponent::UpdateWithContext
void MontagePlayer::UpdateWarping(float DeltaTime, const WarpOwnerState& Owner) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const MontageInfo& MI = Lib.Montage(Montage);
	for (int w = 0; w < (int)MI.Warps.size(); ++w) {
		const WarpWindow& W = MI.Warps[w];
		const float StartTime = Clamp(W.Start, 0.f, MI.Length), EndTime = Clamp(W.End, 0.f, MI.Length);
		if (PreviousPosition >= StartTime && PreviousPosition < EndTime) {
			bool bContains = false;
			for (const Modifier& M : Modifiers) bContains |= (M.Window == w);
			if (!bContains) {
				Modifier M;
				M.Window = w;
				M.PrevPos = PreviousPosition; M.CurPos = Position;
				Modifiers.push_back(M);
			}
		}
	}
	for (Modifier& M : Modifiers) {
		if (M.State == ModState::MarkedForRemoval || M.State == ModState::Disabled) continue;
		const WarpWindow& W = MI.Warps[M.Window];
		const float StartTime = W.Start, EndTime = W.End;
		M.PrevPos = PreviousPosition;
		M.CurPos = Position;
		if (M.PrevPos >= EndTime) { M.State = ModState::MarkedForRemoval; continue; }
		if (M.State == ModState::Active && M.PrevPos < EndTime && (M.CurPos > EndTime || M.CurPos < StartTime)) {
			const float Expected = DeltaTime * PlayRate, Actual = M.CurPos - M.PrevPos;
			if (std::abs(Actual - Expected) > KINDA_SMALL_NUMBER) { M.State = ModState::MarkedForRemoval; continue; }
		}
		if (M.PrevPos >= StartTime && M.PrevPos < EndTime && M.State == ModState::Waiting) {
			M.State = ModState::Active;
			M.ActualStartTime = M.PrevPos;
			M.StartTransform = Xform(Owner.ActorQuat, Owner.VisualRootLocation);
			M.TotalRootMotionWithinWindow = Lib.ExtractRootMotionFromRange(Anim, StartTime, EndTime);
		}
		if (M.State != ModState::Active) continue;
		auto It = WarpTargets.find(W.Target);
		if (It == WarpTargets.end()) { M.State = ModState::Disabled; continue; }
		Xform Target(It->second.Rotation, It->second.Location);
		if (W.bBoneProvider) {
			if (!M.bHasCachedOffset) {
				M.CachedOffsetFromWarpPoint = RootRelativeToWarpPointAtTime(EndTime, W.Bone, Owner.MeshRotationOffset);
				M.bHasCachedOffset = true;
			}
			Target = M.CachedOffsetFromWarpPoint * Target;
		}
		const bool bChanged = !M.bHasCachedTarget || !(M.CachedTargetTransform.Translation - Target.Translation).IsNearlyZero(1e-4) ||
			!M.CachedTargetTransform.Rotation.Equals(Target.Rotation, 1e-4);
		if (bChanged) {
			M.CachedTargetTransform = Target;
			M.bHasCachedTarget = true;
			// OnTargetTransformChanged
			M.ActualStartTime = M.PrevPos;
			M.StartTransform = Xform(Owner.ActorQuat, Owner.VisualRootLocation);
		}
	}
}

static Vec3 SkewWarpTranslation(const Vec3& DeltaTranslation, const Vec3& TotalTranslation, const Vec3& TargetLocation) {
	if (DeltaTranslation.IsNearlyZero()) return Vec3();
	const Vec3 CurrentLocation;
	const Vec3 FutureLocation = CurrentLocation + TotalTranslation;
	const Vec3 CurrentToWorldOffset = TargetLocation - CurrentLocation;
	const Vec3 CurrentToRootOffset = FutureLocation - CurrentLocation;
	const Vec3 ToRootNormalized = CurrentToRootOffset.GetSafeNormal();
	// CurrentTransform is identity: axes X/Y/Z
	auto MakeFromXZ = [](const Vec3& XIn, const Vec3& ZIn, Vec3& AX, Vec3& AY, Vec3& AZ) {
		const Vec3 NewX = XIn.GetSafeNormal();
		Vec3 Norm = ZIn.GetSafeNormal();
		if (std::abs(NewX | Norm) > 1.0 - KINDA_SMALL_NUMBER) Norm = std::abs(NewX.Z) < 1.0 - KINDA_SMALL_NUMBER ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		const Vec3 NewY = (Norm ^ NewX).GetSafeNormal();
		const Vec3 NewZ = NewX ^ NewY;
		AX = NewX; AY = NewY; AZ = NewZ;
	};
	Vec3 AX, AY, AZ;
	float BestMatchDot = (float)std::abs(ToRootNormalized | Vec3(1, 0, 0));
	MakeFromXZ(ToRootNormalized, Vec3(0, 0, 1), AX, AY, AZ);
	const float ZDot = (float)std::abs(ToRootNormalized | Vec3(0, 0, 1));
	if (ZDot > BestMatchDot) { MakeFromXZ(ToRootNormalized, Vec3(1, 0, 0), AX, AY, AZ); BestMatchDot = ZDot; }
	const float YDot = (float)std::abs(ToRootNormalized | Vec3(0, 1, 0));
	if (YDot > BestMatchDot) MakeFromXZ(ToRootNormalized, Vec3(0, 0, 1), AX, AY, AZ);
	auto InvT = [&](const Vec3& V) { return Vec3(V | AX, V | AY, V | AZ); };
	auto Fwd = [&](const Vec3& V) { return AX * V.X + AY * V.Y + AZ * V.Z; };

	const Vec3 RootMotionInSyncSpace = InvT(DeltaTranslation);
	const Vec3 CurrentToWorldSync = InvT(CurrentToWorldOffset);
	const Vec3 CurrentToRootMotionSync = InvT(CurrentToRootOffset);
	const Vec3 CurrentToWorldSyncNorm = CurrentToWorldSync.GetSafeNormal();
	const Vec3 CurrentToRootMotionSyncNorm = CurrentToRootMotionSync.GetSafeNormal();

	const Vec3 FlatToWorld = Vec3(CurrentToWorldSyncNorm.X, CurrentToWorldSyncNorm.Y, 0).GetSafeNormal();
	const Vec3 FlatToRoot = Vec3(CurrentToRootMotionSyncNorm.X, CurrentToRootMotionSyncNorm.Y, 0).GetSafeNormal();
	float AngleAboutZ = (float)std::acos(Clamp(FlatToWorld | FlatToRoot, -1.0, 1.0));
	float AngleAboutZNorm = (float)DegToRad(NormalizeAxis(RadToDeg(AngleAboutZ)));
	if (FlatToWorld.Y < 0.0) AngleAboutZNorm *= -1.f;

	const Vec3 ToWorldNoY = Vec3(CurrentToWorldSyncNorm.X, 0, CurrentToWorldSyncNorm.Z).GetSafeNormal();
	const Vec3 ToRootNoY = Vec3(CurrentToRootMotionSyncNorm.X, 0, CurrentToRootMotionSyncNorm.Z).GetSafeNormal();
	const float AngleAboutY = (float)std::acos(Clamp(ToWorldNoY | ToRootNoY, -1.0, 1.0));
	float AngleAboutYNorm = (float)DegToRad(NormalizeAxis(RadToDeg(AngleAboutY)));
	if (ToWorldNoY.Z < 0.0) AngleAboutYNorm *= -1.f;

	Vec3 Skewed;
	const double RootSize = CurrentToRootMotionSync.Size();
	const float ProjectedScale = RootSize > 0.0 ? (float)((CurrentToWorldSync | CurrentToRootMotionSyncNorm) / RootSize) : 0.f;
	if (ProjectedScale != 0.f) {
		// v * (Scale * ShearXAlongY * ShearXAlongZ), row-vector convention
		const double s = ProjectedScale, tz = std::tan(AngleAboutZNorm), ty = std::tan(AngleAboutYNorm);
		const Vec3& v = RootMotionInSyncSpace;
		Skewed = Vec3(s * v.X, s * tz * v.X + v.Y, s * ty * v.X + v.Z);
	} else if (!CurrentToRootMotionSync.IsNearlyZero(0) && !CurrentToWorldSync.IsNearlyZero(0) && !RootMotionInSyncSpace.IsNearlyZero(0)) {
		const double Scale = CurrentToWorldSync.Size() / CurrentToRootMotionSync.Size();
		const double Step = RootMotionInSyncSpace.Size();
		Skewed = CurrentToWorldSyncNorm * (Scale * Step);
	}
	return Fwd(Skewed);
}

Xform MontagePlayer::ProcessSkewWarp(Modifier& M, const Xform& InRootMotion, float DeltaTime, const WarpOwnerState& Owner) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const WarpWindow& W = Lib.Montage(Montage).Warps[M.Window];
	const float EndTime = W.End;
	Xform Final = InRootMotion;
	const Xform RootMotionTotal = Lib.ExtractRootMotionFromRange(Anim, M.PrevPos, EndTime);
	const Xform RootMotionDelta = Lib.ExtractRootMotionFromRange(Anim, M.PrevPos, std::min(M.CurPos, EndTime));
	Xform Extra;
	if (M.CurPos > EndTime) Extra = Lib.ExtractRootMotionFromRange(Anim, EndTime, M.CurPos);

	if (W.bWarpTranslation) {
		const Vec3 CurrentLocation = Owner.VisualRootLocation;
		const Quat CurrentRotation = Owner.ActorQuat;
		const Vec3 DeltaTranslation = RootMotionDelta.Translation;
		const Vec3 TotalTranslation = RootMotionTotal.Translation;
		Vec3 TargetLocation = M.CachedTargetTransform.Translation;
		if (W.bIgnoreZAxis) TargetLocation.Z = CurrentLocation.Z;
		if (!M.TotalRootMotionWithinWindow.Translation.IsNearlyZero()) {
			if (!DeltaTranslation.IsNearlyZero()) {
				TargetLocation = Owner.MeshTransform.InverseTransformPositionNoScale(TargetLocation);
				Final.Translation = SkewWarpTranslation(DeltaTranslation, TotalTranslation, TargetLocation) + Extra.Translation;
			}
		} else {
			const Vec3 DeltaToTarget = TargetLocation - CurrentLocation;
			if (DeltaToTarget.IsNearlyZero()) {
				Final.Translation = Vec3();
			} else {
				float Alpha = Clamp((M.CurPos - M.ActualStartTime) / (EndTime - M.ActualStartTime), 0.f, 1.f);
				const Vec3 NextLocation = Vec3::Lerp(M.StartTransform.Translation, TargetLocation, Alpha);
				Vec3 FinalDelta = NextLocation - CurrentLocation;
				FinalDelta = (CurrentRotation.Inverse() * Quat::FromDirection(DeltaToTarget)).GetForwardVector() * FinalDelta.Size();
				FinalDelta = Owner.MeshRotationOffset.UnrotateVector(FinalDelta);
				Final.Translation = FinalDelta + Extra.Translation;
			}
		}
	}
	if (W.bWarpRotation) {
		// URootMotionModifier_Warp::WarpRotation (Default rotation type, Slerp)
		const Quat CurrentRotation = Owner.ActorQuat * Owner.MeshRotationOffset;
		const Quat TargetRotation = CurrentRotation.Inverse() * (M.CachedTargetTransform.Rotation * Owner.MeshRotationOffset);
		const Quat TotalRootMotionRotation = RootMotionTotal.Rotation;
		const float TimeRemaining = (EndTime - M.PrevPos) * W.RotationTimeMultiplier;
		const float Alpha = TimeRemaining > 0.f ? Clamp(DeltaTime / TimeRemaining, 0.f, 1.f) : 1.f;
		const Quat TargetRotThisFrame = Quat::Slerp(TotalRootMotionRotation, TargetRotation, Alpha);
		const Quat DeltaOut = TargetRotThisFrame * TotalRootMotionRotation.Inverse();
		Final.Rotation = (Extra.Rotation * (DeltaOut * RootMotionDelta.Rotation)).GetNormalized();
	}
	return Final;
}

Xform MontagePlayer::Advance(float DeltaTime, const WarpOwnerState& Owner, bool& bOutHasRootMotion) {
	bOutHasRootMotion = false;
	bEndedThisFrame = false;
	bBlendOutStartedThisFrame = false;
	Fired.clear();
	if (Out.bActive) {
		if (Out.BlendTime > 0.f && Out.BlendElapsed < Out.BlendTime) {
			Out.BlendElapsed = std::min(Out.BlendElapsed + DeltaTime, Out.BlendTime);
			Out.LinearAlpha = Out.BlendElapsed / Out.BlendTime;
			Out.Weight = Out.BlendBegin + (Out.BlendDesired - Out.BlendBegin) * (float)AlphaToBlendOption(Out.LinearAlpha, Out.Option);
		} else {
			Out.LinearAlpha = 1.f;
			Out.Weight = Out.BlendDesired;
		}
		Out.PreviousPosition = Out.Position;
		if (Out.Weight <= ZERO_ANIMWEIGHT_THRESH) Out.bActive = false;
		else Out.Position = Clamp(Out.Position + DeltaTime * Out.PlayRate, 0.f, Out.Length);
	}
	if (!bPlaying) return Xform();
	// FAnimMontageInstance::Stop -> UAnimInstance::OnMontageInstanceStopped -> ClearMontageInstanceReferences clears
	// RootMotionMontageInstance: once blending out, the montage gives no more root motion (the tick that stopped it
	// still does, TickCharacterPose consumes it through bWasPlayingRootMotion)
	const bool bRootMotionActive = !bStopping;
	const AnimLibrary& Lib = AnimLibrary::Get();
	const MontageInfo& MI = Lib.Montage(Montage);

	// UpdateWeight
	if (BlendTime > 0.f && BlendElapsed < BlendTime) {
		BlendElapsed = std::min(BlendElapsed + DeltaTime, BlendTime);
		LinearAlpha = BlendElapsed / BlendTime;
		Weight = BlendBegin + (BlendDesired - BlendBegin) * (float)AlphaToBlendOption(LinearAlpha, Option);
	} else {
		LinearAlpha = 1.f;
		Weight = BlendDesired;
	}
	if (bStopping && Weight <= ZERO_ANIMWEIGHT_THRESH) {
		// montage ended (OnCompleted / OnInterrupted)
		bPlaying = false;
		bEndedThisFrame = true;
		Weight = 0.f;
		Modifiers.clear();
		return Xform();
	}

	// Advance position
	PreviousPosition = Position;
	Position = Clamp(Position + DeltaTime * PlayRate, 0.f, MI.Length);
	if (Weight > ZERO_ANIMWEIGHT_THRESH)
		CollectNotifies(MI.Notifies, PreviousPosition, Position - PreviousPosition, MI.Length, false, Weight, true, Fired);
	const bool bMoved = Position != PreviousPosition;

	// auto blend out near the end of the section
	if (!bStopping && MI.bEnableAutoBlendOut) {
		const float PlayTimeToEnd = (MI.Length - Position) / std::max(PlayRate, 1e-4f);
		const bool bCustom = MI.BlendOutTriggerTime >= 0.f;
		const float DefaultBlendOutTime = MI.BlendOutTime;
		const float Trigger = bCustom ? MI.BlendOutTriggerTime : DefaultBlendOutTime;
		if (PlayTimeToEnd <= std::max(Trigger, (float)KINDA_SMALL_NUMBER)) {
			Stop(bCustom ? DefaultBlendOutTime : PlayTimeToEnd, MI.BlendOutOption, nullptr);
		}
	} else if (!bStopping && Position >= MI.Length) {
		Stop(0.f, MI.BlendOutOption, nullptr);
	}

	// BP_NotifyState_MontageBlendOut::Received_NotifyTick
	if (!bStopping) {
		for (const BlendOutWindow& B : MI.BlendOuts) {
			if (Position >= B.Start && PreviousPosition < B.End) {
				bool bShould = false;
				switch (B.Condition) {
					case 0: bShould = true; break;
					case 1: bShould = Owner.bHasAcceleration; break;
					case 2: bShould = Owner.bIsFalling; break;
				}
				if (bShould) {
					// Montage_StopWithBlendSettings: FAnimMontageInstance::Stop(Settings, bInterrupt = true)
					bInterrupted = true;
					Stop(B.BlendOutTime, BlendOption::HermiteCubic, B.Profile.empty() ? nullptr : Lib.FindBlendProfile(B.Profile));
					break;
				}
			}
		}
	}

	if (!bMoved || !bRootMotionActive) return Xform();
	// root motion (RootMotionFromMontagesOnly: full weight) + motion warping (ProcessRootMotionPreConvertToWorld)
	Xform RM = Lib.ExtractRootMotionFromRange(Anim, PreviousPosition, Position);
	UpdateWarping(DeltaTime, Owner);
	for (Modifier& M : Modifiers) {
		if (M.State == ModState::Active) RM = ProcessSkewWarp(M, RM, DeltaTime, Owner);
	}
	bOutHasRootMotion = true;
	return RM;
}

void MontagePlayer::PerBoneWeights(const AnimLibrary::BlendProfile* Profile, bool bStopping, float Weight, float LinearAlpha,
	float BlendBegin, float BlendDesired, BlendOption Option, std::vector<float>& W) {
	const int NB = (int)W.size();
	if (Profile && !Profile->bWeightFactor && bStopping) {
		for (int b = 0; b < NB; ++b) {
			const float F = Profile->Scales[b];
			if (F >= 1.f - ZERO_ANIMWEIGHT_THRESH) { W[b] = Weight; continue; }
			const float CF = Clamp(F, 0.f, 1.f);
			const float La = CF > ZERO_ANIMWEIGHT_THRESH ? Clamp(LinearAlpha / CF, 0.f, 1.f) : 1.f;
			const float Real = (float)AlphaToBlendOption(La, Option);
			W[b] = Clamp(BlendBegin + Real * (BlendDesired - BlendBegin), 0.f, 1.f);
		}
	} else {
		std::fill(W.begin(), W.end(), Weight);
	}
}

void MontagePlayer::EvaluateSlot(Pose& Source) const {
	const bool bNew = bPlaying && Weight > 0.f && Anim >= 0;
	if (!bNew && !Out.bActive) return;
	const AnimLibrary& Lib = AnimLibrary::Get();
	const int NB = Lib.NumBones();
	const bool bNewProfile = bNew && Profile && !Profile->bWeightFactor && bStopping;
	const bool bOutProfile = Out.bActive && Out.Profile && !Out.Profile->bWeightFactor;
	Pose OP, MP;
	if (Out.bActive) {
		OP.Init(NB, Lib.NumCurves());
		Lib.SampleLocal(Out.Anim, Out.Position, OP);
		if (Out.Montage >= 0) Lib.Montage(Out.Montage).EvaluateCurves(Out.Position, OP.Curves, OP.CurveSet);
		// FAnimExtractContext(MontagePosition, Montage->HasRootMotion(), DeltaTimeRecord): the montage pose carries this
		// tick's root motion delta attribute, blended with the source pose's (read by OffsetRootBone below the slot)
		if (Lib.Clip(Out.Anim).bRootMotion) {
			OP.RootMotion = Lib.ExtractRootMotionFromRange(Out.Anim, Out.PreviousPosition, Out.Position);
			OP.bHasRootMotion = true;
		}
	}
	if (bNew) {
		MP.Init(NB, Lib.NumCurves());
		Lib.SampleLocal(Anim, Position, MP);
		if (Montage >= 0) Lib.Montage(Montage).EvaluateCurves(Position, MP.Curves, MP.CurveSet);
		if (Lib.Clip(Anim).bRootMotion) {
			MP.RootMotion = Lib.ExtractRootMotionFromRange(Anim, PreviousPosition, Position);
			MP.bHasRootMotion = true;
		}
	}
	// slot: base * (1 - sum) + old * w_old + new * w_new, normalised when the montage weights exceed 1; applied as
	// base -> old with w_old / (1 - w_new), then -> new with w_new
	auto Split = [](float WO, float& WN) {
		const float Sum = WO + WN;
		if (Sum > 1.f) { WN = WN / Sum; return 1.f; }
		return WN < 1.f - ZERO_ANIMWEIGHT_THRESH ? std::min(WO / (1.f - WN), 1.f) : 0.f;
	};
	if (!bNewProfile && !bOutProfile) {
		float WN = bNew ? Weight : 0.f;
		if (Out.bActive) BlendWithPose(Source, OP, 1.f - Split(Out.Weight, WN));
		if (bNew) BlendWithPose(Source, MP, 1.f - WN);
		return;
	}
	std::vector<float> WN(NB, 0.f), WO(NB, 0.f), W1(NB, 0.f);
	if (bNew) PerBoneWeights(Profile, bStopping, Weight, LinearAlpha, BlendBegin, BlendDesired, Option, WN);
	if (Out.bActive) {
		PerBoneWeights(Out.Profile, true, Out.Weight, Out.LinearAlpha, Out.BlendBegin, Out.BlendDesired, Out.Option, WO);
		for (int b = 0; b < NB; ++b) W1[b] = Split(WO[b], WN[b]);
		BlendWithPosePerBone(Source, OP, W1);
	}
	if (bNew) BlendWithPosePerBone(Source, MP, WN);
}

} // namespace gasp
