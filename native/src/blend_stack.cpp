#include "blend_stack.h"

namespace gasp {

void BlendStackPlayer::GetBlendInWeights(std::vector<float>& W) const {
	const int N = (int)TotalBlendInTimePerBone.size();
	W.resize(N);
	const float WeightForZero = TimeToActivation > 0.f ? 0.f : 1.f;
	for (int b = 0; b < N; ++b) {
		const float T = TotalBlendInTimePerBone[b];
		if (T < 1e-8f) W[b] = WeightForZero;
		else W[b] = (float)AlphaToBlendOption(std::min(CurrentBlendInTime / T, 1.f), Option);
	}
}

void BlendStackPlayer::UpdateWithDeltaTime(float DeltaTime, int PlayerDepth, float Mult) {
	if (TimeToActivation > 0.f) {
		TimeToActivation -= DeltaTime;
		if (TimeToActivation < 0.f) { DeltaTime = -TimeToActivation; TimeToActivation = 0.f; }
		else DeltaTime = 0.f;
	}
	if (PlayerDepth == 0) CurrentBlendInTime += DeltaTime;
	else CurrentBlendInTime += DeltaTime * std::pow(Mult, (float)(PlayerDepth + 1));
}

float BlendStackPlayer::GetCurrentAssetLength() const {
	return Anim >= 0 ? AnimLibrary::Get().Clip(Anim).Length : 0.f;
}

int BlendStack::GetNextPoseLinkIndex() {
	++CurrentSamplePoseLink;
	if (CurrentSamplePoseLink >= NumPoseLinks) CurrentSamplePoseLink = 0;
	return CurrentSamplePoseLink;
}

void BlendStack::BlendTo(int Anim, float AccumulatedTime, bool bLoop, float BlendTime, const AnimLibrary::BlendProfile* Profile, BlendOption Option, float PlayRate, float ActivationDelay) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const bool bEmpty = Players.empty();
	if (bEmpty) BlendTime = 0.f;
	int NewLink = CurrentSamplePoseLink;
	bool bNewSlot = false;
	if (!bEmpty && !Players[0].IsActive()) {
	} else if (!bEmpty && Players[0].GetBlendInPercentage() < 1.f && Players[0].CurrentBlendInTime < MaxBlendInTimeToOverrideAnimation) {
	} else if ((int)Players.size() <= MaxActiveBlends + 2) {
		Players.insert(Players.begin(), BlendStackPlayer());
		NewLink = GetNextPoseLinkIndex();
		bNewSlot = true;
	}
	BlendStackPlayer& P = Players[0];
	P = BlendStackPlayer();
	P.Anim = Anim;
	P.AccumulatedTime = AccumulatedTime;
	P.PreviousTime = AccumulatedTime;
	P.bLoop = bLoop;
	P.PlayRate = PlayRate;
	P.Option = Option;
	P.TotalBlendInTime = BlendTime;
	P.CurrentBlendInTime = 0.f;
	P.TimeToActivation = ActivationDelay;
	P.PoseLinkIndex = NewLink;
	if (BlendTime > 1e-4f && Profile) {
		const int NB = Lib.NumBones();
		P.TotalBlendInTimePerBone.assign(NB, BlendTime);
		for (int b = 0; b < NB; ++b) {
			if (!Profile->Has[b]) continue;
			if (Profile->bWeightFactor) { if (Profile->Scales[b] > 1e-8f) P.TotalBlendInTimePerBone[b] /= Profile->Scales[b]; }
			else P.TotalBlendInTimePerBone[b] *= Profile->Scales[b];
		}
	}
	if (MakeGraphState) P.Graph = MakeGraphState(NewLink);
	(void)bNewSlot;
}

// UE MovePoseContextTo moves the stored pose buffers to the previous player and then resets them
// (Other.StoredBones.Reset()), so the previous player keeps playing its own animation: the stored pose is dropped.
void BlendStack::PopLastAnimPlayer() {
	Players.pop_back();
}

void BlendStack::Update(float DeltaTime) {
	Fired.clear();
	float CurrentWeightMultiplier = 1.f;
	const int Size = (int)Players.size();
	int i = 0;
	while (i < Size) {
		BlendStackPlayer& P = Players[i];
		const bool bLast = i == Size - 1;
		const float BlendIn = bLast ? 1.f : P.GetBlendInWeight();
		P.BlendWeight = CurrentWeightMultiplier * BlendIn;
		// UpdateSample
		P.UpdateWithDeltaTime(DeltaTime, i, PlayerDepthBlendInTimeMultiplier);
		if (P.IsActive()) {
			if (P.Anim >= 0) {
				if (PreAdvance) PreAdvance(P);
				// sequence player time advance (FAnimationRuntime::AdvanceTime)
				const float L = P.GetCurrentAssetLength();
				P.PreviousTime = P.AccumulatedTime;
				P.MoveDelta = DeltaTime * P.PlayRate;
				float NewTime = P.AccumulatedTime + DeltaTime * P.PlayRate;
				if (NewTime < 0.f || NewTime > L) {
					if (P.bLoop) { NewTime = L != 0.f ? std::fmod(NewTime, L) : 0.f; if (NewTime < 0.f) NewTime += L; }
					else NewTime = Clamp(NewTime, 0.f, L);
				}
				P.AccumulatedTime = NewTime;
				if (P.BlendWeight > ZERO_ANIMWEIGHT_THRESH)
					CollectNotifies(AnimLibrary::Get().Clip(P.Anim).Notifies, P.PreviousTime, P.MoveDelta, L, P.bLoop, P.BlendWeight, false, Fired);
			}
			if (UpdateSampleGraph && !P.bHasStoredPose) UpdateSampleGraph(P, DeltaTime, i == 0);
		}
		CurrentWeightMultiplier *= (1.f - BlendIn);
		++i;
		if (CurrentWeightMultiplier < 1e-4f) break;
	}
	const int Wanted = std::max(1, i);
	while ((int)Players.size() > Wanted) PopLastAnimPlayer();
}

bool BlendStack::EvaluateSample(Pose& Out, int PlayerIndex) {
	BlendStackPlayer& P = Players[PlayerIndex];
	if (!P.IsActive()) return false;
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (P.bHasStoredPose) {
		Out = P.Stored;
		return true;
	}
	if (P.Anim < 0) {
		Out.Init(Lib.NumBones(), Lib.NumCurves());
		Out.Bones = Lib.RefPose;
		return true;
	}
	Lib.SampleLocal(P.Anim, P.AccumulatedTime, Out);
	if (Lib.Clip(P.Anim).bRootMotion) {
		Out.RootMotion = Lib.ExtractRootMotion(P.Anim, P.PreviousTime, P.MoveDelta, P.bLoop);
		Out.bHasRootMotion = true;
	}
	if (EvaluateSampleGraph) EvaluateSampleGraph(P, Out);
	return true;
}

void BlendWithPose(Pose& InOut, const Pose& Other, float InOutWeight) {
	const float OW = 1.f - InOutWeight;
	const int NB = (int)InOut.Bones.size();
	for (int b = 0; b < NB; ++b) {
		Xform X = InOut.Bones[b].Scaled(InOutWeight);
		X.AccumulateWithShortestRotation(Other.Bones[b], OW);
		X.NormalizeRotation();
		InOut.Bones[b] = X;
	}
	// root motion attribute (missing = identity)
	if (InOut.bHasRootMotion || Other.bHasRootMotion) {
		Xform R = InOut.RootMotion.Scaled(InOutWeight);
		R.AccumulateWithShortestRotation(Other.RootMotion, OW);
		R.NormalizeRotation();
		InOut.RootMotion = R;
		InOut.bHasRootMotion = true;
	}
	// FBlendedCurve::LerpTo (union, missing = 0)
	const int NC = (int)InOut.Curves.size();
	for (int c = 0; c < NC; ++c) {
		if (!InOut.CurveSet[c] && !Other.CurveSet[c]) continue;
		InOut.Curves[c] = InOut.Curves[c] + (Other.Curves[c] - InOut.Curves[c]) * OW;
		InOut.CurveSet[c] = 1;
	}
}

void BlendWithPosePerBone(Pose& InOut, const Pose& Other, const std::vector<float>& W) {
	const int NB = (int)InOut.Bones.size();
	for (int b = 0; b < NB; ++b) {
		const float OW = W[b];
		if (OW >= 1.f - ZERO_ANIMWEIGHT_THRESH) InOut.Bones[b] = Other.Bones[b];
		else if (OW > ZERO_ANIMWEIGHT_THRESH) {
			Xform X = InOut.Bones[b].Scaled(1.f - OW);
			X.AccumulateWithShortestRotation(Other.Bones[b], OW);
			InOut.Bones[b] = X;
		}
		InOut.Bones[b].NormalizeRotation();
	}
	if (InOut.bHasRootMotion || Other.bHasRootMotion) {
		const float OW = W[0];
		Xform R = InOut.RootMotion.Scaled(1.f - OW);
		R.AccumulateWithShortestRotation(Other.RootMotion, OW);
		R.NormalizeRotation();
		InOut.RootMotion = R;
		InOut.bHasRootMotion = true;
	}
	// FBlendedCurve::Combine: values from Other override
	const int NC = (int)InOut.Curves.size();
	for (int c = 0; c < NC; ++c) if (Other.CurveSet[c]) { InOut.Curves[c] = Other.Curves[c]; InOut.CurveSet[c] = 1; }
}

void BlendStack::Evaluate(Pose& Out) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const int Size = (int)Players.size();
	if (Size <= 0) { Out.Init(Lib.NumBones(), Lib.NumCurves()); Out.Bones = Lib.RefPose; return; }
	if (Size == 1) {
		if (!EvaluateSample(Out, 0)) { Out.Init(Lib.NumBones(), Lib.NumCurves()); Out.Bones = Lib.RefPose; }
		return;
	}
	if (!EvaluateSample(Out, Size - 1)) { Out.Init(Lib.NumBones(), Lib.NumCurves()); Out.Bones = Lib.RefPose; }
	std::vector<float> W;
	auto EvalAndBlend = [&](int Idx) {
		if (EvaluateSample(Scratch, Idx)) {
			if (!Players[Idx].TotalBlendInTimePerBone.empty()) {
				Players[Idx].GetBlendInWeights(W);
				BlendWithPosePerBone(Out, Scratch, W);
			} else {
				BlendWithPose(Out, Scratch, 1.f - Players[Idx].GetBlendInWeight());
			}
		}
	};
	int PI = Size - 2;
	for (; PI >= MaxActiveBlends; --PI) {
		EvalAndBlend(PI);
		PopLastAnimPlayer();
	}
	if (PI == MaxActiveBlends - 1 && bStoreBlendedPose && (int)Players.size() == MaxActiveBlends + 1) {
		Players.back().Stored = Out;
		Players.back().bHasStoredPose = true;
	}
	for (; PI >= 0; --PI) EvalAndBlend(PI);
}

} // namespace gasp
