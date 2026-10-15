#include "mm.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>

using namespace godot;

namespace gasp {

static constexpr float FiniteDelta = 1.f / 60.f;
static std::string S(const Variant& v) { return std::string(String(v).utf8().get_data()); }

// ---------------------------------------------------------------------------------------------
TrajectorySample Trajectory::GetSampleAtTime(float Time, bool bExtrapolate) const {
	const int Num = (int)Samples.size();
	if (Num > 1) {
		int Lower = 0;
		while (Lower < Num && Samples[Lower].AccumulatedSeconds < Time) ++Lower;
		const int NextIdx = Clamp(Lower, 1, Num - 1);
		const int PrevIdx = NextIdx - 1;
		const float Den = Samples[NextIdx].AccumulatedSeconds - Samples[PrevIdx].AccumulatedSeconds;
		if (!IsNearlyZero(Den)) {
			const float Num_ = Time - Samples[PrevIdx].AccumulatedSeconds;
			const float L = bExtrapolate ? Num_ / Den : Clamp(Num_ / Den, 0.f, 1.f);
			return Samples[PrevIdx].Lerp(Samples[NextIdx], L);
		}
		return Samples[PrevIdx];
	}
	if (Num > 0) return Samples[0];
	return TrajectorySample();
}

// ---------------------------------------------------------------------------------------------
void PoseHistory::Init(const std::vector<int>& InBones, const std::vector<int>& InCurves) {
	Bones.clear();
	Bones.push_back(0);
	for (int b : InBones) if (std::find(Bones.begin(), Bones.end(), b) == Bones.end()) Bones.push_back(b);
	CollectedCurves = InCurves;
	Entries.clear();
}

int PoseHistory::SlotOf(int Bone) const {
	for (int i = 0; i < (int)Bones.size(); ++i) if (Bones[i] == Bone) return i;
	return -1;
}

void PoseHistory::Evaluate(float DeltaTime, const std::vector<Xform>& CSPose, const Pose& CurvesPose) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	PoseHistoryEntry Future;
	bool bHadFuture = false;
	if (!Entries.empty() && Entries.back().AccumulatedSeconds > 0.f) {
		Future = Entries.back();
		Entries.pop_back();
		bHadFuture = true;
	}
	for (auto& E : Entries) E.AccumulatedSeconds -= DeltaTime;
	if ((int)Entries.size() != MaxNumPoses) Entries.emplace_back();
	else {
		PoseHistoryEntry Tmp = std::move(Entries.front());
		Entries.erase(Entries.begin());
		Entries.push_back(std::move(Tmp));
	}
	PoseHistoryEntry& Recent = Entries.back();
	Recent.AccumulatedSeconds = 0.f;
	Recent.CS.resize(Bones.size());
	for (int i = 0; i < (int)Bones.size(); ++i) Recent.CS[i] = CSPose[Bones[i]];
	Recent.Curves.resize(CollectedCurves.size());
	for (int i = 0; i < (int)CollectedCurves.size(); ++i) Recent.Curves[i] = CurvesPose.GetCurve(CollectedCurves[i]);

	if (RootBoneRecoveryTime > 0.f && !Traj.Samples.empty()) {
		const Xform& RefRoot = Lib.RefPose[0];
		const Quat R0 = Recent.CS[0].Rotation;
		const Quat RotAtRecovery = R0 + (RefRoot.Rotation - R0) * RootBoneRotationRecoveryRatio; // FMath::Lerp on FQuat (component-wise)
		Vec3 DeltaT;
		if (RootBoneTranslationRecoveryRatio > 0.f) {
			const Xform WorldRootNow = Traj.GetSampleAtTime(0.f).GetTransform();
			const Xform WorldRootBoneNow = Recent.CS[0] * WorldRootNow;
			const Vec3 D = (WorldRootBoneNow.Translation - WorldRootNow.Translation) * RootBoneTranslationRecoveryRatio;
			const Xform WorldRootAtRecovery = Traj.GetSampleAtTime(RootBoneRecoveryTime).GetTransform();
			DeltaT = WorldRootAtRecovery.InverseTransformVector(D);
		}
		Future.CS.assign(1, Xform(RotAtRecovery, DeltaT, RefRoot.Scale3D));
		Future.Curves.clear();
		Future.AccumulatedSeconds = RootBoneRecoveryTime;
		Entries.push_back(Future);
	}
	(void)bHadFuture;
}

static inline Xform EntryCS(const PoseHistoryEntry& E, int Slot) {
	if (Slot < 0 || Slot >= (int)E.CS.size()) return Xform();
	const Xform& X = E.CS[Slot];
	return X;
}

bool PoseHistory::GetTransformAtTime(float Time, Xform& Out, int Bone, bool bWorldSpace, bool bRelativeToRoot) const {
	Xform ComponentToWorld;
	if (bWorldSpace) ComponentToWorld = Traj.GetSampleAtTime(Time).GetTransform();
	const int Num = (int)Entries.size();
	if (Num == 0) { Out = ComponentToWorld; return false; }
	int NextIdx = 0, PrevIdx = 0;
	if (Num > 1) {
		int Lower = 0;
		while (Lower < Num && Entries[Lower].AccumulatedSeconds < Time) ++Lower;
		NextIdx = Clamp(Lower, 1, Num - 1);
		PrevIdx = NextIdx - 1;
	}
	const PoseHistoryEntry& P = Entries[PrevIdx];
	const PoseHistoryEntry& N = Entries[NextIdx];
	const float Den = N.AccumulatedSeconds - P.AccumulatedSeconds;
	float L = 0.f;
	if (!IsNearlyZero(Den)) L = Clamp((Time - P.AccumulatedSeconds) / Den, 0.f, 1.f);
	const int Slot = SlotOf(Bone);
	if (Slot < 0) { Out = Xform(); return false; }
	auto Get = [&](const PoseHistoryEntry& E) {
		Xform X = EntryCS(E, Slot);
		if (bRelativeToRoot && !bWorldSpace) X = X * EntryCS(E, 0).Inverse();
		return X;
	};
	if (IsNearlyZero(L, ZERO_ANIMWEIGHT_THRESH)) Out = Get(P);
	else if (IsNearlyZero(L - 1.f, ZERO_ANIMWEIGHT_THRESH)) Out = Get(N);
	else Out.Blend(Get(P), Get(N), L);
	if (bWorldSpace) Out = Out * ComponentToWorld;
	return true;
}

bool PoseHistory::GetCurveValueAtTime(float Time, int Curve, float& Out) const {
	Out = 0.f;
	const int Num = (int)Entries.size();
	int CI = -1;
	for (int i = 0; i < (int)CollectedCurves.size(); ++i) if (CollectedCurves[i] == Curve) CI = i;
	if (Num == 0 || CI < 0) return false;
	int NextIdx = 0, PrevIdx = 0;
	if (Num > 1) {
		int Lower = 0;
		while (Lower < Num && Entries[Lower].AccumulatedSeconds < Time) ++Lower;
		NextIdx = Clamp(Lower, 1, Num - 1);
		PrevIdx = NextIdx - 1;
	}
	const auto& P = Entries[PrevIdx];
	const auto& N = Entries[NextIdx];
	auto CV = [&](const PoseHistoryEntry& E) { return CI < (int)E.Curves.size() ? E.Curves[CI] : 0.f; };
	const float Den = N.AccumulatedSeconds - P.AccumulatedSeconds;
	float L = 0.f;
	if (!IsNearlyZero(Den)) L = Clamp((Time - P.AccumulatedSeconds) / Den, 0.f, 1.f);
	Out = CV(P) + (CV(N) - CV(P)) * L;
	return true;
}

// ---------------------------------------------------------------------------------------------
MotionMatchingLibrary& MotionMatchingLibrary::Get() {
	static MotionMatchingLibrary L;
	return L;
}

int MotionMatchingLibrary::FindDatabase(const std::string& Name) const {
	for (const auto& D : Databases) if (D.Name == Name) return D.Index;
	// accept full paths
	size_t Slash = Name.find_last_of('/');
	std::string Short = Slash == std::string::npos ? Name : Name.substr(Slash + 1);
	size_t Dot = Short.find('.');
	if (Dot != std::string::npos) Short = Short.substr(0, Dot);
	for (const auto& D : Databases) if (D.Name == Short) return D.Index;
	return -1;
}

static Variant LoadJsonMM(const std::string& Path) {
	Ref<FileAccess> f = FileAccess::open(String(Path.c_str()), FileAccess::READ);
	if (f.is_null()) return Variant();
	return JSON::parse_string(f->get_as_text());
}

bool MotionMatchingLibrary::Load(const std::string& DataDir) {
	if (bLoaded) return true;
	const AnimLibrary& Lib = AnimLibrary::Get();
	Dictionary J = LoadJsonMM(DataDir + "/mm/mm.json");
	if (J.is_empty()) { UtilityFunctions::push_error("gasp: mm.json missing"); return false; }
	Dictionary Sch = J["schemas"];
	Array SK = Sch.keys();
	std::unordered_map<std::string, int> SchemaIdx;
	for (int i = 0; i < SK.size(); ++i) {
		Dictionary s = Sch[SK[i]];
		Schema Out;
		Out.Name = S(SK[i]);
		Out.Cardinality = (int)s["cardinality"];
		Out.SampleRate = (int)s.get("rate", 30);
		Array Ch = s["channels"];
		for (int c = 0; c < Ch.size(); ++c) {
			Dictionary d = Ch[c];
			Channel C;
			std::string T = S(d["type"]);
			C.Type = T == "Position" ? ChannelType::Position : (T == "Velocity" ? ChannelType::Velocity : (T == "Curve" ? ChannelType::Curve : ChannelType::Heading));
			if (C.Type == ChannelType::Curve) C.CurveIdx = Lib.FindCurve(S(d.get("curve", "")));
			C.BoneName = S(d.get("bone", ""));
			C.OriginName = S(d.get("origin_bone", ""));
			C.Bone = C.BoneName.empty() ? 0 : Lib.FindBone(C.BoneName);
			C.OriginBone = C.OriginName.empty() ? 0 : Lib.FindBone(C.OriginName);
			if (C.Bone < 0) C.Bone = 0;
			if (C.OriginBone < 0) C.OriginBone = 0;
			C.SampleTimeOffset = (float)(double)d.get("sample_time_offset", 0.0);
			C.OriginTimeOffset = (float)(double)d.get("origin_time_offset", 0.0);
			C.Input = S(d.get("input_query_pose", "USE_CHARACTER_POSE")) == "USE_CHARACTER_POSE" ? InputQueryPose::Character : InputQueryPose::Continuing;
			std::string St = S(d.get("component_stripping", "NONE"));
			C.Stripping = St == "STRIP_Z" ? Strip::Z : (St == "STRIP_XY" ? Strip::XY : Strip::None);
			C.Cardinality = (int)d["cardinality"];
			C.Offset = (int)d["offset"];
			std::string HA = S(d.get("heading_axis", "X"));
			C.HeadingAxis = HA == "X" ? 0 : (HA == "Y" ? 1 : 2);
			C.bCharSpaceVel = (bool)d.get("use_char_space_vel", false);
			C.bNormalize = (bool)d.get("normalize", false);
			C.bBPOverride = (bool)d.get("bp_override", false);
			C.bTrajectory = S(d.get("outer", "")) == "Traj";
			char Buf[256];
			snprintf(Buf, sizeof(Buf), "%d|%d|%d|%.6f|%.6f|%d|%d|%d|%d|%d", (int)C.Type, C.Bone, C.OriginBone, C.SampleTimeOffset, C.OriginTimeOffset,
				(int)C.Input, (int)C.Stripping, C.Type == ChannelType::Heading ? C.HeadingAxis : (C.Type == ChannelType::Curve ? 100 + C.CurveIdx : 0), C.bCharSpaceVel ? 1 : 0, C.bNormalize ? 1 : 0);
			C.Key = Buf;
			Out.Channels.push_back(C);
		}
		Out.Index = (int)Schemas.size();
		SchemaIdx[Out.Name] = Out.Index;
		Schemas.push_back(Out);
	}
	Dictionary Dbs = J["databases"];
	Array DK = Dbs.keys();
	for (int i = 0; i < DK.size(); ++i) {
		Dictionary d = Dbs[DK[i]];
		Database Db;
		Db.Name = S(DK[i]);
		Db.Index = (int)Databases.size();
		Db.SchemaIdx = SchemaIdx[S(d["schema"])];
		Db.N = (int)d["num_poses"]; Db.D = (int)d["dims"]; Db.K = (int)d["pcs"];
		Db.bBruteForce = S(d["mode"]) == "BRUTE_FORCE";
		Db.KDNeighbors = (int)d.get("kd_neighbors", 200);
		Db.ContinuingPoseCostBias = (float)(double)d["continuing_pose_cost_bias"];
		Db.MinCostAddend = (float)(double)d.get("min_cost_addend", 0.0);
		Array Tags = d.get("tags", Array());
		for (int t = 0; t < Tags.size(); ++t) Db.Tags.push_back(S(Tags[t]));
		Array As = d["assets"];
		for (int a = 0; a < As.size(); ++a) {
			Dictionary ad = As[a];
			DbAsset A;
			A.SourceAssetIdx = (int)ad.get("source_asset_idx", a);
			std::string Mont = S(ad.get("montage", ""));
			A.Anim = Lib.FindAnim(S(ad["anim"]));
			A.Montage = Mont.empty() ? -1 : Lib.FindMontage(Mont);
			A.PlayLength = (float)(double)ad["play_length"];
			A.bLooping = (bool)ad["looping"];
			A.bDisableReselection = (bool)ad["disable_reselection"];
			A.FirstPose = (int)ad["first_pose"]; A.FirstSample = (int)ad["first_sample"]; A.LastSample = (int)ad["last_sample"];
			std::string NA = S(ad.get("notify_asset", ""));
			A.NotifyMontage = Lib.FindMontage(NA);
			A.NotifyAnim = A.NotifyMontage >= 0 ? -1 : Lib.FindAnim(NA);
			if (A.Anim < 0) UtilityFunctions::push_warning(String("gasp: db asset anim missing ") + String(ad["anim"]));
			Db.Assets.push_back(A);
		}
		// binary
		std::ifstream f(DataDir + "/mm/" + Db.Name + ".bin", std::ios::binary);
		if (!f) { UtilityFunctions::push_error(String("gasp: missing db bin ") + Db.Name.c_str()); return false; }
		auto ReadF = [&](std::vector<float>& V, size_t Count) { V.resize(Count); f.read(reinterpret_cast<char*>(V.data()), Count * 4); };
		ReadF(Db.Values, (size_t)Db.N * Db.D);
		ReadF(Db.WeightsSqrt, Db.D);
		ReadF(Db.Mean, Db.D);
		ReadF(Db.Proj, (size_t)Db.D * Db.K);
		ReadF(Db.PCAValues, (size_t)Db.N * Db.K);
		ReadF(Db.CostAddend, Db.N);
		Db.Flags.resize(Db.N);
		f.read(reinterpret_cast<char*>(Db.Flags.data()), (size_t)Db.N * 4);
		Db.PoseToAsset.assign(Db.N, -1);
		for (int a = 0; a < (int)Db.Assets.size(); ++a) {
			const DbAsset& A = Db.Assets[a];
			for (int p = 0; p < A.NumPoses(); ++p) if (A.FirstPose + p < Db.N) Db.PoseToAsset[A.FirstPose + p] = a;
		}
		for (int p = 0; p < Db.N; ++p) if (!Db.IsBlockTransition(p)) Db.SelectablePCA.push_back(p);
		Databases.push_back(std::move(Db));
	}
	bLoaded = true;
	UtilityFunctions::print(String("gasp: loaded ") + String::num_int64(Databases.size()) + " MM databases");
	return true;
}

// ---------------------------------------------------------------------------------------------
const DbAsset* GetSearchIndexAsset(const SearchResult& R) {
	if (!R.IsValid()) return nullptr;
	const Database& Db = MotionMatchingLibrary::Get().Db(R.Database);
	const int A = Db.PoseToAsset[R.PoseIdx];
	return A >= 0 ? &Db.Assets[A] : nullptr;
}

bool CanAdvance(const SearchResult& R, float DeltaTime) {
	const DbAsset* A = GetSearchIndexAsset(R);
	if (!A) return false;
	float NewTime = R.AssetTime + DeltaTime;
	if (NewTime < 0.f || NewTime > A->PlayLength) return A->bLooping;
	return true;
}

void UpdateSearchResultTime(SearchResult& R, float NewAssetTime) {
	if (R.IsValid()) {
		const Database& Db = MotionMatchingLibrary::Get().Db(R.Database);
		const DbAsset& A = Db.Assets[Db.PoseToAsset[R.PoseIdx]];
		R.PoseIdx = A.PoseFromTime(NewAssetTime, MotionMatchingLibrary::Get().Sch(Db.SchemaIdx).SampleRate);
		R.AssetTime = NewAssetTime;
		if (R.PoseIdx < 0) R.Reset();
	} else R.Reset();
}

// ---------------------------------------------------------------------------------------------
SearchContext::SearchContext(const PoseHistory* InHistory, const SearchResult& InCurrent, const std::map<std::pair<int, int>, float>* InPIH, const QueryOverrides* InOverrides)
	: Current(InCurrent), History(InHistory), PoseIndicesHistory(InPIH), Overrides(InOverrides) {
	if (Current.IsValid()) {
		const Database& Db = MotionMatchingLibrary::Get().Db(Current.Database);
		CurrentResultPoseVector.assign(Db.PoseValues(Current.PoseIdx), Db.PoseValues(Current.PoseIdx) + Db.D);
		bHasCurrentPoseVector = true;
	}
}

bool SearchContext::CanUseCurrentResult() const {
	return Current.IsValid() && MotionMatchingLibrary::Get().Db(Current.Database).SchemaIdx == BuildingSchema;
}

const std::vector<float>* SearchContext::GetCachedQuery(int SchemaIdx) const {
	for (const auto& C : Cached) if (C.Schema == SchemaIdx) return &C.Values;
	return nullptr;
}

const std::vector<float>& SearchContext::GetOrBuildQuery(int SchemaIdx) {
	for (const auto& C : Cached) if (C.Schema == SchemaIdx) return C.Values;
	Cached.push_back({ SchemaIdx, std::vector<float>() });
	const Schema& Sc = MotionMatchingLibrary::Get().Sch(SchemaIdx);
	Cached.back().Values.assign(Sc.Cardinality, 0.f);
	BuildingSchema = SchemaIdx;
	BuildQuery(Sc, Cached.back().Values);
	return Cached.back().Values;
}

Xform SearchContext::GetWorldRootBoneTransformAtTime(float Time) {
	Xform Out;
	if (History && History->GetTransformAtTime(Time, Out, 0, true, false)) return Out;
	return Out;
}

Xform SearchContext::GetWorldBoneTransformAtTime(float Time, int Bone) {
	uint32_t TB; std::memcpy(&TB, &Time, 4);
	const uint64_t Key = ((uint64_t)TB << 32) | (uint32_t)Bone;
	auto It = CachedTransforms.find(Key);
	if (It != CachedTransforms.end()) return It->second;
	Xform W;
	if (Bone == 0) W = GetWorldRootBoneTransformAtTime(Time);
	else {
		const uint64_t RootKey = ((uint64_t)TB << 32);
		auto RIt = CachedTransforms.find(RootKey);
		W = RIt != CachedTransforms.end() ? RIt->second : GetWorldRootBoneTransformAtTime(Time);
		Xform Local;
		if (History) History->GetTransformAtTime(Time, Local, Bone, false, true);
		W = Local * W;
	}
	CachedTransforms[Key] = W;
	return W;
}

Vec3 SearchContext::GetSamplePositionInternal(float SampleTime, float OriginTime, int Bone, int Origin, const Vec3* WorldOverride) {
	const Xform Root = GetWorldBoneTransformAtTime(OriginTime, 0);
	if (WorldOverride) {
		if (Origin == 0) return Root.InverseTransformPosition(*WorldOverride);
		const Xform O = GetWorldBoneTransformAtTime(OriginTime, Origin);
		return Root.InverseTransformVector(*WorldOverride - O.Translation);
	}
	const Xform SampleB = GetWorldBoneTransformAtTime(SampleTime, Bone);
	if (Origin == 0) return Root.InverseTransformPosition(SampleB.Translation);
	const Xform O = GetWorldBoneTransformAtTime(OriginTime, Origin);
	return Root.InverseTransformVector(SampleB.Translation - O.Translation);
}

Quat SearchContext::GetSampleRotationInternal(float SampleTime, float OriginTime, int Bone, int Origin, const Quat* WorldOverride) {
	const Xform Root = GetWorldBoneTransformAtTime(OriginTime, 0);
	if (WorldOverride) {
		if (Origin == 0) return Root.InverseTransformRotation(*WorldOverride);
		const Xform O = GetWorldBoneTransformAtTime(OriginTime, Origin);
		return Root.InverseTransformRotation(O.InverseTransformRotation(*WorldOverride));
	}
	const Xform SampleB = GetWorldBoneTransformAtTime(SampleTime, Bone);
	return Root.InverseTransformRotation(SampleB.Rotation);
}

static void EncodeVector(std::vector<float>& V, int Off, Vec3 Vec, Strip St, bool bNormalize) {
	if (bNormalize) {
		switch (St) {
			case Strip::None: Vec.Normalize(); V[Off] = Vec.X; V[Off + 1] = Vec.Y; V[Off + 2] = Vec.Z; break;
			case Strip::XY: Vec.X = 0; Vec.Y = 0; Vec.Normalize(); V[Off] = Vec.Z; break;
			case Strip::Z: Vec.Z = 0; Vec.Normalize(); V[Off] = Vec.X; V[Off + 1] = Vec.Y; break;
		}
	} else {
		switch (St) {
			case Strip::None: V[Off] = Vec.X; V[Off + 1] = Vec.Y; V[Off + 2] = Vec.Z; break;
			case Strip::XY: V[Off] = Vec.Z; break;
			case Strip::Z: V[Off] = Vec.X; V[Off + 1] = Vec.Y; break;
		}
	}
}

static Vec3 DecodeVector(const float* V, int Off, Strip St) {
	switch (St) {
		case Strip::None: return Vec3(V[Off], V[Off + 1], V[Off + 2]);
		case Strip::XY: return Vec3(0, 0, V[Off]);
		case Strip::Z: return Vec3(V[Off], V[Off + 1], 0);
	}
	return Vec3();
}

static Vec3 HeadingAxis(const Quat& Q, int Axis) { return Axis == 0 ? Q.GetAxisX() : (Axis == 1 ? Q.GetAxisY() : Q.GetAxisZ()); }

void SearchContext::BuildQuery(const Schema& Sc, std::vector<float>& Out) {
	const int QueryIdx = (int)Cached.size() - 1;
	for (const Channel& C : Sc.Channels) {
		// blueprint overrides (traversal interaction point)
		if (C.bBPOverride) {
			const bool bHas = Overrides && Overrides->bHasInteraction;
			const Xform IW = bHas ? Overrides->InteractionWorld : Xform();
			if (C.Type == ChannelType::Position) {
				const Vec3 P = IW.Translation;
				EncodeVector(Out, C.Offset, GetSamplePositionInternal(C.SampleTimeOffset, C.OriginTimeOffset, C.Bone, C.OriginBone, &P), C.Stripping, false);
			} else if (C.Type == ChannelType::Heading) {
				const Quat R = IW.Rotation;
				EncodeVector(Out, C.Offset, HeadingAxis(GetSampleRotationInternal(C.SampleTimeOffset, C.OriginTimeOffset, C.Bone, C.OriginBone, &R), C.HeadingAxis), C.Stripping, true);
			}
			continue;
		}
		if (bUseCachedChannelData) {
			auto It = CachedChannels.find(C.Key);
			if (It != CachedChannels.end()) {
				const std::vector<float>& Src = Cached[It->second.first].Values;
				for (int k = 0; k < C.Cardinality; ++k) Out[C.Offset + k] = Src[It->second.second + k];
				continue;
			}
			CachedChannels[C.Key] = { QueryIdx, C.Offset };
		}
		const bool bCanUseCurrent = CanUseCurrentResult();
		const bool bSkip = C.Input != InputQueryPose::Character && bCanUseCurrent;
		if (bSkip || (!History && (C.Bone != 0 || C.Type == ChannelType::Curve))) {
			if (bCanUseCurrent) {
				for (int k = 0; k < C.Cardinality; ++k) Out[C.Offset + k] = CurrentResultPoseVector[C.Offset + k];
			}
			continue;
		}
		switch (C.Type) {
			case ChannelType::Position:
				EncodeVector(Out, C.Offset, GetSamplePositionInternal(C.SampleTimeOffset, C.OriginTimeOffset, C.Bone, C.OriginBone, nullptr), C.Stripping, false);
				break;
			case ChannelType::Velocity: {
				const Vec3 Prev = GetSamplePositionInternal(C.SampleTimeOffset - FiniteDelta, C.bCharSpaceVel ? C.OriginTimeOffset - FiniteDelta : C.OriginTimeOffset, C.Bone, C.OriginBone, nullptr);
				const Vec3 Cur = GetSamplePositionInternal(C.SampleTimeOffset, C.OriginTimeOffset, C.Bone, C.OriginBone, nullptr);
				Vec3 Vel = (Cur - Prev) / FiniteDelta;
				if (C.bNormalize) Vel = Vel.GetClampedToMaxSize(1.0);
				EncodeVector(Out, C.Offset, Vel, C.Stripping, false);
				break;
			}
			case ChannelType::Heading:
				EncodeVector(Out, C.Offset, HeadingAxis(GetSampleRotationInternal(C.SampleTimeOffset, C.OriginTimeOffset, C.Bone, C.OriginBone, nullptr), C.HeadingAxis), C.Stripping, true);
				break;
			case ChannelType::Curve: {
				float V = 0.f;
				if (History) History->GetCurveValueAtTime(C.SampleTimeOffset, C.CurveIdx, V);
				Out[C.Offset] = V;
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
static inline float ComparePoses(const Database& Db, int PoseIdx, float Bias, const float* Q) {
	const float* V = Db.PoseValues(PoseIdx);
	const float* W = Db.WeightsSqrt.data();
	float Sum = 0.f;
	for (int d = 0; d < Db.D; ++d) { const float x = (V[d] - Q[d]) * W[d]; Sum += x * x; }
	return Sum + Db.CostAddend[PoseIdx] + Bias;
}

static void PopulateNonSelectable(const Database& Db, SearchContext& Ctx, std::vector<int>& Out) {
	Out.clear();
	if (Ctx.Current.IsValid() && Ctx.Current.Database == Db.Index) {
		const DbAsset* A = GetSearchIndexAsset(Ctx.Current);
		if (A && A->bDisableReselection) {
			for (const DbAsset& O : Db.Assets) {
				if (O.SourceAssetIdx != A->SourceAssetIdx) continue;
				for (int p = 0; p < O.NumPoses(); ++p) Out.push_back(O.FirstPose + p);
			}
		}
	}
	if (Ctx.PoseIndicesHistory) {
		for (const auto& KV : *Ctx.PoseIndicesHistory) if (KV.first.first == Db.Index) Out.push_back(KV.first.second);
	}
	std::sort(Out.begin(), Out.end());
	Out.erase(std::unique(Out.begin(), Out.end()), Out.end());
}

static SearchResult SearchDatabase(const Database& Db, SearchContext& Ctx, const std::vector<int>* SelectableAssets) {
	SearchResult R;
	if (!(Ctx.CurrentBestTotalCost > Db.MinCostAddend)) return R;
	const std::vector<float>& Q = Ctx.GetOrBuildQuery(Db.SchemaIdx);
	std::vector<int> NonSel;
	PopulateNonSelectable(Db, Ctx, NonSel);
	auto IsNonSel = [&](int p) { return std::binary_search(NonSel.begin(), NonSel.end(), p); };
	auto IsSelectableAsset = [&](int p) {
		if (!SelectableAssets || SelectableAssets->empty()) return true;
		return std::find(SelectableAssets->begin(), SelectableAssets->end(), Db.PoseToAsset[p]) != SelectableAssets->end();
	};
	if (Db.bBruteForce || Db.K == 0 || (SelectableAssets && !SelectableAssets->empty())) {
		for (int p = 0; p < Db.N; ++p) {
			if (Db.IsBlockTransition(p) || IsNonSel(p) || !IsSelectableAsset(p)) continue;
			const float C = ComparePoses(Db, p, 0.f, Q.data());
			if (C < R.PoseCost) { R.PoseCost = C; R.PoseIdx = p; }
		}
	} else {
		// PCA projection of the weighted query
		float PQ[16] = { 0 };
		const int K = std::min(Db.K, 16);
		for (int k = 0; k < K; ++k) {
			float s = 0.f;
			for (int d = 0; d < Db.D; ++d) s += (Q[d] * Db.WeightsSqrt[d] - Db.Mean[d]) * Db.Proj[(size_t)d * Db.K + k];
			PQ[k] = s;
		}
		// k nearest neighbours in PCA space among non block-transition, selectable poses
		const int NumN = Clamp(Db.KDNeighbors, 1, Db.N);
		std::vector<std::pair<float, int>> Heap; // max-heap on distance
		Heap.reserve(NumN + 1);
		for (int p : Db.SelectablePCA) {
			if (IsNonSel(p)) continue;
			const float* PV = Db.PCAValues.data() + (size_t)p * Db.K;
			float d2 = 0.f;
			for (int k = 0; k < K; ++k) { const float x = PV[k] - PQ[k]; d2 += x * x; }
			if ((int)Heap.size() < NumN) { Heap.emplace_back(d2, p); std::push_heap(Heap.begin(), Heap.end()); }
			else if (d2 < Heap.front().first) { std::pop_heap(Heap.begin(), Heap.end()); Heap.back() = { d2, p }; std::push_heap(Heap.begin(), Heap.end()); }
		}
		std::sort_heap(Heap.begin(), Heap.end());
		for (const auto& H : Heap) {
			const float C = ComparePoses(Db, H.second, 0.f, Q.data());
			if (C < R.PoseCost) { R.PoseCost = C; R.PoseIdx = H.second; }
		}
	}
	if (R.PoseIdx >= 0) {
		const DbAsset& A = Db.Assets[Db.PoseToAsset[R.PoseIdx]];
		R.AssetTime = A.TimeFromPose(R.PoseIdx, MotionMatchingLibrary::Get().Sch(Db.SchemaIdx).SampleRate);
		R.Database = Db.Index;
	}
	return R;
}

static SearchResult SearchContinuingPose(const Database& Db, SearchContext& Ctx) {
	SearchResult R;
	R.bIsContinuingPoseSearch = true;
	const int PoseIdx = Ctx.Current.PoseIdx;
	const DbAsset& A = Db.Assets[Db.PoseToAsset[PoseIdx]];
	float Bias = Db.ContinuingPoseCostBias;
	const float SampleTime = A.TimeFromPose(PoseIdx, MotionMatchingLibrary::Get().Sch(Db.SchemaIdx).SampleRate);
	const AnimLibrary& Lib = AnimLibrary::Get();
	const std::vector<NotifyEvent>* Notifies = nullptr;
	if (A.NotifyMontage >= 0) Notifies = &Lib.Montage(A.NotifyMontage).Notifies;
	else if (A.NotifyAnim >= 0) Notifies = &Lib.Clip(A.NotifyAnim).Notifies;
	else if (A.Anim >= 0) Notifies = &Lib.Clip(A.Anim).Notifies;
	if (Notifies) {
		const float H = 0.5f / 120.f;
		for (const NotifyEvent& N : *Notifies) {
			if (N.Class == "AnimNotifyState_PoseSearchOverrideContinuingPoseCostBias" && N.Time < SampleTime + H && N.EndTime() > SampleTime - H) {
				Bias = N.CostAddend;
				break;
			}
		}
	}
	if (Ctx.CurrentBestTotalCost > Db.MinCostAddend + Bias) {
		const std::vector<float>& Q = Ctx.GetOrBuildQuery(Db.SchemaIdx);
		R.PoseCost = ComparePoses(Db, PoseIdx, Bias, Q.data());
		R.AssetTime = Ctx.Current.AssetTime;
		R.PoseIdx = PoseIdx;
		R.Database = Db.Index;
	}
	return R;
}

// ---------------------------------------------------------------------------------------------
void MotionMatchingState::Reset() {
	CurrentSearchResult.Reset();
	ElapsedPoseSearchTime = 1e30f;
	WantedPlayRate = 1.f;
	bJumpedToPose = false;
	PoseIndicesHistory.clear();
}

void MotionMatchingState::AdjustAssetTime(float AssetTime) { UpdateSearchResultTime(CurrentSearchResult, AssetTime); }

static bool ContainsDb(const std::vector<int>& Dbs, int Db) { return std::find(Dbs.begin(), Dbs.end(), Db) != Dbs.end(); }

void MotionMatchingState::Update(float DeltaTime, const std::vector<int>& Databases, InterruptMode Mode, const PoseHistory* History,
	float PoseReselectHistory, float SearchThrottleTime, float PlayRateMin, float PlayRateMax, const QueryOverrides* Overrides) {
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	bJumpedToPose = false;
	if (Databases.empty()) return;
	const int CurDb = CurrentSearchResult.IsValid() ? CurrentSearchResult.Database : -1;
	bool bInvalidate = false;
	switch (Mode) {
		case InterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose: bInvalidate = !ContainsDb(Databases, CurDb); break;
		case InterruptMode::ForceInterruptAndInvalidateContinuingPose: bInvalidate = true; break;
		default: break;
	}
	if (bInvalidate) CurrentSearchResult.Reset();

	SearchContext Ctx(History, CurrentSearchResult, &PoseIndicesHistory, Overrides);
	const bool bCanAdvance = CanAdvance(CurrentSearchResult, DeltaTime);
	const bool bSearch = !bCanAdvance || (ElapsedPoseSearchTime >= SearchThrottleTime);
	if (bSearch) {
		ElapsedPoseSearchTime = 0.f;
		bool bForceInterrupt = false;
		switch (Mode) {
			case InterruptMode::InterruptOnDatabaseChange:
			case InterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose: bForceInterrupt = !ContainsDb(Databases, CurDb); break;
			case InterruptMode::ForceInterrupt:
			case InterruptMode::ForceInterruptAndInvalidateContinuingPose: bForceInterrupt = true; break;
			default: break;
		}
		const bool bSearchContinuingPose = !bForceInterrupt && bCanAdvance;
		{
			int OneSchema = bSearchContinuingPose && CurrentSearchResult.IsValid() ? MML.Db(CurrentSearchResult.Database).SchemaIdx : -1;
			bool bMulti = false;
			for (int d : Databases) {
				const int s = MML.Db(d).SchemaIdx;
				if (OneSchema != s) { if (OneSchema < 0) OneSchema = s; else bMulti = true; }
			}
			Ctx.bUseCachedChannelData = bMulti;
		}
		SearchResult Result;
		DebugCanAdvance = bCanAdvance;
		DebugContinuingCost = 3.402823e38f;
		if (bSearchContinuingPose && CurrentSearchResult.IsValid()) {
			Result = SearchContinuingPose(MML.Db(CurrentSearchResult.Database), Ctx);
			DebugContinuingCost = Result.PoseCost;
			if (Result.PoseCost < Ctx.CurrentBestTotalCost) Ctx.CurrentBestTotalCost = Result.PoseCost;
		}
		bool bJump = false;
		DebugDbCosts.clear();
		for (int d : Databases) {
			SearchResult NR = SearchDatabase(MML.Db(d), Ctx, nullptr);
			DebugDbCosts.push_back({ d, NR.PoseCost });
			if (NR.PoseCost < Result.PoseCost) {
				bJump = true;
				Result = NR;
				if (Result.PoseCost < Ctx.CurrentBestTotalCost) Ctx.CurrentBestTotalCost = Result.PoseCost;
			}
		}
		LastSearchCost = Result.PoseCost;
		if (Result.IsValid()) {
			const Database& RDb = MML.Db(Result.Database);
			if (const std::vector<float>* Q = Ctx.GetCachedQuery(RDb.SchemaIdx)) {
				DebugQuery = *Q;
				const float* P = RDb.PoseValues(Result.PoseIdx);
				DebugPose.assign(P, P + Q->size());
				DebugSchema = RDb.SchemaIdx;
			}
		}
		if (bJump) {
			CurrentSearchResult = Result;
			CurrentSearchResult.bIsContinuingPoseSearch = false;
			bJumpedToPose = true;
		} else {
			CurrentSearchResult.PoseCost = Result.PoseCost;
		}
	} else {
		ElapsedPoseSearchTime += DeltaTime;
	}

	// UpdateWantedPlayRate
	if (CurrentSearchResult.IsValid()) {
		if (!IsNearlyEqual(PlayRateMin, PlayRateMax, KINDA_SMALL_NUMBER)) {
			const Database& Db = MML.Db(CurrentSearchResult.Database);
			const std::vector<float>* Q = Ctx.GetCachedQuery(Db.SchemaIdx);
			if (Q) {
				const float* P = Db.PoseValues(CurrentSearchResult.PoseIdx);
				double QS = 0, PS = 0;
				for (const Channel& C : MML.Sch(Db.SchemaIdx).Channels) {
					if (C.bTrajectory && C.Type == ChannelType::Velocity && !C.bNormalize) {
						QS += DecodeVector(Q->data(), C.Offset, C.Stripping).Length();
						PS += DecodeVector(P, C.Offset, C.Stripping).Length();
					}
				}
				const double Ratio = PS > KINDA_SMALL_NUMBER ? QS / PS : 1.0;
				WantedPlayRate = (float)Clamp(Ratio, (double)PlayRateMin, (double)PlayRateMax);
			}
		} else WantedPlayRate = PlayRateMin;
	}

	// PoseIndicesHistory.Update
	if (PoseReselectHistory > 0.f) {
		for (auto It = PoseIndicesHistory.begin(); It != PoseIndicesHistory.end();) {
			It->second += DeltaTime;
			if (It->second > PoseReselectHistory) It = PoseIndicesHistory.erase(It);
			else ++It;
		}
		if (CurrentSearchResult.IsValid()) PoseIndicesHistory[{ CurrentSearchResult.Database, CurrentSearchResult.PoseIdx }] = 0.f;
	} else PoseIndicesHistory.clear();
}

SearchResult MotionMatchAssets(int DatabaseIdx, const std::vector<int>& AssetMontages, const PoseHistory* History, const QueryOverrides* Overrides) {
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	const Database& Db = MML.Db(DatabaseIdx);
	std::vector<int> Selectable;
	for (int a = 0; a < (int)Db.Assets.size(); ++a) {
		for (int m : AssetMontages) if (Db.Assets[a].Montage == m) Selectable.push_back(a);
	}
	SearchResult Empty;
	SearchContext Ctx(History, Empty, nullptr, Overrides);
	if (Selectable.empty()) return SearchResult();
	return SearchDatabase(Db, Ctx, &Selectable);
}

SearchResult MotionMatchAnimAssets(const std::vector<int>& Anims, const std::vector<int>& Databases, const PoseHistory* History) {
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	SearchResult Best;
	for (int DbIdx : Databases) {
		const Database& Db = MML.Db(DbIdx);
		std::vector<int> Selectable;
		for (int a = 0; a < (int)Db.Assets.size(); ++a)
			if (Db.Assets[a].Montage < 0 && std::find(Anims.begin(), Anims.end(), Db.Assets[a].Anim) != Anims.end()) Selectable.push_back(a);
		if (Selectable.empty()) continue;
		SearchResult Empty;
		SearchContext Ctx(History, Empty, nullptr, nullptr);
		const SearchResult R = SearchDatabase(Db, Ctx, &Selectable);
		if (R.IsValid() && R.PoseCost < Best.PoseCost) Best = R;
	}
	return Best;
}

} // namespace gasp
