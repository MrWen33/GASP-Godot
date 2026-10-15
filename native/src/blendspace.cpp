#include "blendspace.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace gasp {

static std::string S(const Variant& v) { return std::string(String(v).utf8().get_data()); }

BlendSpaceLibrary& BlendSpaceLibrary::Get() {
	static BlendSpaceLibrary L;
	return L;
}

int BlendSpaceLibrary::Find(const std::string& Name) const {
	for (int i = 0; i < (int)Assets.size(); ++i) if (Assets[i].Name == Name) return i;
	return -1;
}

bool BlendSpaceLibrary::Load(const std::string& DataDir) {
	Ref<FileAccess> f = FileAccess::open(String((DataDir + "/blendspaces.json").c_str()), FileAccess::READ);
	if (f.is_null()) return false;
	Dictionary D = JSON::parse_string(f->get_as_text());
	const AnimLibrary& Lib = AnimLibrary::Get();
	Assets.clear();
	Array Keys = D.keys();
	for (int k = 0; k < Keys.size(); ++k) {
		const std::string Name = S(Keys[k]);
		if (Name[0] == '_') continue;
		Dictionary B = D[Keys[k]];
		BlendSpaceAsset A;
		A.Name = Name;
		Array Smp = B["samples"];
		for (int i = 0; i < Smp.size(); ++i) {
			Dictionary s = Smp[i];
			BlendSpaceAsset::Sample X;
			X.Anim = Lib.FindAnim(S(s["anim"]));
			if (X.Anim < 0) UtilityFunctions::push_error(String("gasp: blendspace sample anim missing ") + String(s["anim"]));
			Array v = s["value"];
			X.X = (double)v[0]; X.Y = (double)v[1];
			A.Samples.push_back(X);
		}
		Array P = B["params"];
		A.NumDims = (int)P.size();
		for (int i = 0; i < P.size() && i < 2; ++i) {
			Dictionary p = P[i];
			A.Min[i] = (double)p["min"]; A.Max[i] = (double)p["max"];
		}
		Array T = B["triangles"];
		for (int i = 0; i < T.size(); ++i) { Array t = T[i]; A.Triangles.push_back({ (int)t[0], (int)t[1], (int)t[2] }); }
		Array G = B["segments"];
		for (int i = 0; i < G.size(); ++i) { Array t = G[i]; A.Segments.push_back({ (int)t[0], (int)t[1] }); }
		A.Speed = (float)(double)B["speed"];
		A.PerBoneIndex.assign(Lib.NumBones(), -1);
		Array PB = B["per_bone"];
		for (int i = 0; i < PB.size(); ++i) {
			Dictionary pb = PB[i];
			const int Bone = Lib.FindBone(S(pb["bone"]));
			A.PerBoneSpeed.push_back((float)(double)pb["speed"]);
			if (Bone < 0) continue;
			// bone and all of its descendants
			for (int b = 0; b < Lib.NumBones(); ++b) {
				if (A.PerBoneIndex[b] >= 0) continue;
				int p = b;
				while (p >= 0 && p != Bone) p = Lib.Parents[p];
				if (p == Bone) A.PerBoneIndex[b] = i;
			}
		}
		Assets.push_back(A);
	}
	// AO_Blend_Curve keys (from the T3D): (0,0,t0) (0.445375,0.822032,t1.073247) (1,1,t0)
	AOBlendCurve = { { 0.0, 0.0, 0.0, 0.0 }, { 0.445375, 0.822032, 1.073247, 1.073247 }, { 1.0, 1.0, 0.0, 0.0 } };
	return true;
}

// FRichCurve cubic segment evaluation (BezierInterp with tangents scaled by key distance / 3)
double BlendSpaceLibrary::EvalAOBlendCurve(double T) const {
	const auto& K = AOBlendCurve;
	if (T <= K.front().T) return K.front().V;
	if (T >= K.back().T) return K.back().V;
	for (size_t i = 1; i < K.size(); ++i) {
		if (T <= K[i].T) {
			const CurveKey& A = K[i - 1]; const CurveKey& B = K[i];
			const double Diff = B.T - A.T;
			const double Alpha = (T - A.T) / Diff;
			const double P0 = A.V, P3 = B.V;
			const double P1 = P0 + A.Leave * Diff / 3.0;
			const double P2 = P3 - B.Arrive * Diff / 3.0;
			const double u = 1 - Alpha;
			return u * u * u * P0 + 3 * u * u * Alpha * P1 + 3 * u * Alpha * Alpha * P2 + Alpha * Alpha * Alpha * P3;
		}
	}
	return K.back().V;
}

void ConvertPoseToMeshRotation(std::vector<Xform>& Pose) {
	const auto& Parents = AnimLibrary::Get().Parents;
	for (int b = 1; b < (int)Pose.size(); ++b) {
		const int P = Parents[b];
		Pose[b].Rotation = Pose[P].Rotation * Pose[b].Rotation;
	}
}

void ConvertMeshRotationPoseToLocalSpace(std::vector<Xform>& Pose) {
	const auto& Parents = AnimLibrary::Get().Parents;
	for (int b = (int)Pose.size() - 1; b > 0; --b) {
		const int P = Parents[b];
		Pose[b].Rotation = Pose[P].Rotation.Inverse() * Pose[b].Rotation;
	}
}

void MakeIdentityAdditive(std::vector<Xform>& Out) {
	Out.assign(AnimLibrary::Get().NumBones(), Xform(Quat(), Vec3(), Vec3(0, 0, 0)));
}

const std::vector<Xform>& BlendSpaceLibrary::AdditiveDelta(int Anim) {
	auto It = DeltaCache.find(Anim);
	if (It != DeltaCache.end()) return It->second;
	const AnimLibrary& Lib = AnimLibrary::Get();
	const AnimClip& C = Lib.Clip(Anim);
	Pose T, B;
	Lib.SampleLocal(Anim, 0.f, T);
	const int BaseAnim = C.AdditiveBaseAnim >= 0 ? C.AdditiveBaseAnim : Anim;
	Lib.SampleLocal(BaseAnim, C.AdditiveBaseFrame / std::max(1.f, Lib.Clip(BaseAnim).Fps), B);
	std::vector<Xform> TP = T.Bones, BP = B.Bones;
	if (C.AdditiveType == 2) { ConvertPoseToMeshRotation(TP); ConvertPoseToMeshRotation(BP); }
	std::vector<Xform> D(TP.size());
	for (size_t i = 0; i < TP.size(); ++i) {
		D[i].Rotation = (TP[i].Rotation * BP[i].Rotation.Inverse()).GetNormalized();
		D[i].Translation = TP[i].Translation - BP[i].Translation;
		const Vec3& BS = BP[i].Scale3D;
		D[i].Scale3D = Vec3(TP[i].Scale3D.X / BS.X - 1.0, TP[i].Scale3D.Y / BS.Y - 1.0, TP[i].Scale3D.Z / BS.Z - 1.0);
	}
	return DeltaCache.emplace(Anim, std::move(D)).first->second;
}

void AccumulateMeshSpaceRotationAdditive(std::vector<Xform>& Base, const std::vector<Xform>& Additive, float Weight) {
	if (Weight <= ZERO_ANIMWEIGHT_THRESH) return;
	ConvertPoseToMeshRotation(Base);
	for (size_t i = 0; i < Base.size(); ++i) {
		const Xform& A = Additive[i];
		Quat Blended = A.Rotation;
		if (Weight < 1.f - ZERO_ANIMWEIGHT_THRESH) {
			// VectorLerpQuat(Identity, A, Weight)
			const Quat Id;
			const double Bias = (Id | A.Rotation) >= 0 ? 1.0 : -1.0;
			Blended = Quat(A.Rotation.X * Weight * Bias, A.Rotation.Y * Weight * Bias, A.Rotation.Z * Weight * Bias, A.Rotation.W * Weight * Bias + (1.0 - Weight));
		}
		Base[i].Rotation = Blended * Base[i].Rotation;
		Base[i].Translation += A.Translation * Weight;
		Base[i].Scale3D = Base[i].Scale3D * (Vec3(1, 1, 1) + A.Scale3D * Weight);
	}
	ConvertMeshRotationPoseToLocalSpace(Base);
	for (auto& X : Base) X.NormalizeRotation();
}

// ---------------------------------------------------------------------------------------------
static float SmoothingTimeFromSpeed(float Speed) { return Speed > 1.1920929e-07f ? 1.0f / (2.71828182845904523536f * Speed) : 0.0f; }

static void CriticallyDampedSmoothing(float& Value, float& Rate, float Target, float DeltaTime, float SmoothingTime) {
	if (SmoothingTime > KINDA_SMALL_NUMBER) {
		const float AdjustedTarget = Target;
		const float W = 2.0f / SmoothingTime;
		const float A = W * DeltaTime;
		const float Exp = (float)InvExpApprox(A);
		const float X0 = Value - AdjustedTarget;
		const float B = (Rate + X0 * W) * DeltaTime;
		Value = AdjustedTarget + (X0 + B) * Exp;
		Rate = (Rate - B * W) * Exp;
	} else if (DeltaTime > 0.f) {
		Rate = (Target - Value) / DeltaTime;
		Value = Target;
	} else {
		Value = Target;
		Rate = 0.f;
	}
}

static void SmoothWeight(float& Output, float& OutputRate, float Input, float InputRate, float Target, float DeltaTime, float Speed) {
	if (Speed <= 0.f) { Output = Target; return; }
	Output = Input;
	OutputRate = InputRate;
	CriticallyDampedSmoothing(Output, OutputRate, Target, DeltaTime, SmoothingTimeFromSpeed(Speed));
}

void BlendSpacePlayer::GetTargetWeights(double X, double Y, std::vector<BlendSampleData>& Out) const {
	const BlendSpaceAsset& A = BlendSpaceLibrary::Get().Asset(AssetIdx);
	const double NX = Clamp((X - A.Min[0]) / (A.Max[0] - A.Min[0]), 0.0, 1.0);
	const double NY = A.NumDims > 1 ? Clamp((Y - A.Min[1]) / (A.Max[1] - A.Min[1]), 0.0, 1.0) : 0.0;
	auto NormS = [&](int i, int d) { return d == 0 ? (A.Samples[i].X - A.Min[0]) / (A.Max[0] - A.Min[0]) : (A.Samples[i].Y - A.Min[1]) / (A.Max[1] - A.Min[1]); };
	auto Add = [&](int i, double W) {
		if (W <= ZERO_ANIMWEIGHT_THRESH) return;
		for (auto& s : Out) if (s.Index == i) { s.TotalWeight += (float)W; return; }
		BlendSampleData D; D.Index = i; D.TotalWeight = (float)W; Out.push_back(D);
	};
	if (A.NumDims == 1) {
		for (auto& Seg : A.Segments) {
			const double V0 = NormS(Seg[0], 0), V1 = NormS(Seg[1], 0);
			if (NX >= std::min(V0, V1) - 1e-6 && NX <= std::max(V0, V1) + 1e-6) {
				const double T = (V1 != V0) ? (NX - V0) / (V1 - V0) : 0.0;
				Add(Seg[0], 1.0 - T); Add(Seg[1], T);
				return;
			}
		}
		return;
	}
	int Best = -1; double BestErr = 1e30; double BW[3] = { 0, 0, 0 };
	for (int t = 0; t < (int)A.Triangles.size(); ++t) {
		const auto& Tr = A.Triangles[t];
		const double ax = NormS(Tr[0], 0), ay = NormS(Tr[0], 1);
		const double bx = NormS(Tr[1], 0), by = NormS(Tr[1], 1);
		const double cx = NormS(Tr[2], 0), cy = NormS(Tr[2], 1);
		const double Den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
		if (std::abs(Den) < 1e-12) continue;
		const double w0 = ((by - cy) * (NX - cx) + (cx - bx) * (NY - cy)) / Den;
		const double w1 = ((cy - ay) * (NX - cx) + (ax - cx) * (NY - cy)) / Den;
		const double w2 = 1.0 - w0 - w1;
		const double Err = std::max(0.0, -std::min(w0, std::min(w1, w2)));
		if (Err < BestErr) { BestErr = Err; Best = t; BW[0] = w0; BW[1] = w1; BW[2] = w2; }
		if (Err == 0.0) break;
	}
	if (Best < 0) return;
	double Sum = 0;
	for (double& w : BW) { w = std::max(0.0, w); Sum += w; }
	for (int k = 0; k < 3; ++k) Add(A.Triangles[Best][k], BW[k] / std::max(Sum, 1e-12));
}

void BlendSpacePlayer::Update(double X, double Y, float DeltaTime) {
	const BlendSpaceAsset& A = BlendSpaceLibrary::Get().Asset(AssetIdx);
	std::vector<BlendSampleData> New;
	GetTargetWeights(X, Y, New);
	if (New.empty()) return;
	const int NPB = (int)A.PerBoneSpeed.size();
	if (!(A.Speed > 0.f || NPB > 0)) { Cache = New; return; }
	std::vector<BlendSampleData> Final;
	float TotalFinal = 0, TotalFinalPB = 0;
	for (BlendSampleData Old : Cache) {
		if ((int)Old.PerBone.size() != NPB) { Old.PerBone.assign(NPB, Old.TotalWeight); Old.PerBoneRate.assign(NPB, Old.WeightRate); }
		bool bFound = false;
		for (const auto& N : New) {
			if (N.Index != Old.Index) continue;
			BlendSampleData I = N;
			SmoothWeight(I.TotalWeight, I.WeightRate, Old.TotalWeight, Old.WeightRate, N.TotalWeight, DeltaTime, A.Speed);
			I.PerBone = Old.PerBone; I.PerBoneRate = Old.PerBoneRate;
			float TPB = 0;
			for (int k = 0; k < NPB; ++k) { SmoothWeight(I.PerBone[k], I.PerBoneRate[k], Old.PerBone[k], Old.PerBoneRate[k], N.TotalWeight, DeltaTime, A.PerBoneSpeed[k]); TPB += I.PerBone[k]; }
			if (I.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TPB > ZERO_ANIMWEIGHT_THRESH) {
				Final.push_back(I); TotalFinal += Clamp(I.TotalWeight, 0.f, 1.f); TotalFinalPB += TPB; bFound = true;
				break;
			}
		}
		if (!bFound) {
			BlendSampleData I = Old;
			SmoothWeight(I.TotalWeight, I.WeightRate, Old.TotalWeight, Old.WeightRate, 0.f, DeltaTime, A.Speed);
			float TPB = 0;
			for (int k = 0; k < NPB; ++k) { SmoothWeight(I.PerBone[k], I.PerBoneRate[k], Old.PerBone[k], Old.PerBoneRate[k], 0.f, DeltaTime, A.PerBoneSpeed[k]); TPB += I.PerBone[k]; }
			if (I.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TPB > ZERO_ANIMWEIGHT_THRESH) { Final.push_back(I); TotalFinal += Clamp(I.TotalWeight, 0.f, 1.f); TotalFinalPB += TPB; }
		}
	}
	for (BlendSampleData N : New) {
		bool bExists = false;
		for (const auto& F : Final) if (F.Index == N.Index) { bExists = true; break; }
		if (bExists) continue;
		if ((int)N.PerBone.size() != NPB) { N.PerBone.assign(NPB, N.TotalWeight); N.PerBoneRate.assign(NPB, N.WeightRate); }
		BlendSampleData I = N;
		SmoothWeight(I.TotalWeight, I.WeightRate, 0.f, 0.f, N.TotalWeight, DeltaTime, A.Speed);
		float TPB = 0;
		for (int k = 0; k < NPB; ++k) { SmoothWeight(I.PerBone[k], I.PerBoneRate[k], 0.f, 0.f, N.PerBone[k], DeltaTime, A.PerBoneSpeed[k]); TPB += I.PerBone[k]; }
		if (I.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TPB > ZERO_ANIMWEIGHT_THRESH) { Final.push_back(I); TotalFinal += Clamp(I.TotalWeight, 0.f, 1.f); TotalFinalPB += TPB; }
	}
	if (!(TotalFinal > ZERO_ANIMWEIGHT_THRESH || TotalFinalPB > ZERO_ANIMWEIGHT_THRESH)) { Cache = New; return; }
	// FBlendSampleData::NormalizeDataWeight
	float Sum = 0; std::vector<float> PBSum(NPB, 0.f);
	for (auto& F : Final) { Sum += F.TotalWeight; for (int k = 0; k < NPB; ++k) PBSum[k] += F.PerBone[k]; }
	if (Sum > ZERO_ANIMWEIGHT_THRESH) { if (std::abs(Sum - 1.f) > ZERO_ANIMWEIGHT_THRESH) for (auto& F : Final) F.TotalWeight /= Sum; }
	else for (auto& F : Final) F.TotalWeight = 1.f / Final.size();
	for (int k = 0; k < NPB; ++k) {
		if (PBSum[k] > ZERO_ANIMWEIGHT_THRESH) { if (std::abs(PBSum[k] - 1.f) > ZERO_ANIMWEIGHT_THRESH) for (auto& F : Final) F.PerBone[k] /= PBSum[k]; }
		else for (auto& F : Final) F.PerBone[k] = 1.f / Final.size();
	}
	Cache = Final;
}

void BlendSpacePlayer::EvaluateAdditive(std::vector<Xform>& Out) const {
	const BlendSpaceAsset& A = BlendSpaceLibrary::Get().Asset(AssetIdx);
	const int NB = AnimLibrary::Get().NumBones();
	if (Cache.empty()) { MakeIdentityAdditive(Out); return; }
	Out.assign(NB, Xform(Quat(0, 0, 0, 0), Vec3(), Vec3(0, 0, 0)));
	bool bFirst = true;
	for (const auto& D : Cache) {
		const auto& Delta = BlendSpaceLibrary::Get().AdditiveDelta(A.Samples[D.Index].Anim);
		for (int b = 0; b < NB; ++b) {
			const int PBI = A.PerBoneIndex.empty() ? -1 : A.PerBoneIndex[b];
			const float W = (PBI >= 0 && PBI < (int)D.PerBone.size()) ? D.PerBone[PBI] : D.TotalWeight;
			if (bFirst) Out[b] = Delta[b].Scaled(W);
			else Out[b].AccumulateWithShortestRotation(Delta[b], W);
		}
		bFirst = false;
	}
	for (auto& X : Out) X.NormalizeRotation();
}

} // namespace gasp
