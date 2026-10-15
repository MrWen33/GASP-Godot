#include "anim_lib.h"
#include <cctype>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <fstream>

using namespace godot;

namespace gasp {

static std::string LowerName(const std::string& S) { std::string R = S; for (char& c : R) c = (char)std::tolower((unsigned char)c); return R; }

static std::string S(const Variant& v) { return std::string(String(v).utf8().get_data()); }

static Variant LoadJson(const std::string& Path) {
	Ref<FileAccess> f = FileAccess::open(String(Path.c_str()), FileAccess::READ);
	if (f.is_null()) {
		UtilityFunctions::push_error(String("gasp: cannot open ") + Path.c_str());
		return Variant();
	}
	return JSON::parse_string(f->get_as_text());
}

AnimLibrary& AnimLibrary::Get() {
	static AnimLibrary Lib;
	return Lib;
}

int AnimLibrary::AddCurveName(const std::string& N) {
	const std::string Key = LowerName(N); // FName comparison is case-insensitive
	auto It = CurveIndex.find(Key);
	if (It != CurveIndex.end()) return It->second;
	int Id = (int)CurveNames.size();
	CurveNames.push_back(N);
	CurveIndex[Key] = Id;
	return Id;
}

static void ParseNotifies(const Array& Arr, std::vector<NotifyEvent>& Out) {
	for (int i = 0; i < Arr.size(); ++i) {
		Dictionary d = Arr[i];
		NotifyEvent e;
		e.Name = S(d.get("name", ""));
		e.Class = S(d.get("class", ""));
		e.Time = (float)(double)d.get("time", 0.0);
		e.Duration = (float)(double)d.get("duration", 0.0);
		e.bState = (bool)d.get("state", false);
		if (d.has("cost_addend")) { e.bHasCostAddend = true; e.CostAddend = (float)(double)d["cost_addend"]; }
		e.Side = S(d.get("side", ""));
		if (d.has("modifier")) {
			Dictionary m = d["modifier"];
			e.WarpTargetName = S(m.get("warp_target_name", ""));
			e.WarpPointBone = S(m.get("warp_point_anim_bone_name", ""));
			e.bWarpTranslation = (bool)m.get("warp_translation", true);
			e.bIgnoreZAxis = (bool)m.get("ignore_z_axis", true);
			e.bWarpRotation = (bool)m.get("warp_rotation", true);
			e.RotationType = S(m.get("rotation_type", "DEFAULT"));
			e.WarpRotationTimeMultiplier = (float)(double)m.get("warp_rotation_time_multiplier", 1.0);
			e.WarpMaxRotationRate = (float)(double)m.get("warp_max_rotation_rate", 0.0);
		}
		Out.push_back(e);
	}
}

bool AnimLibrary::Load(const std::string& DataDir) {
	if (bLoaded) return true;
	Dictionary J = LoadJson(DataDir + "/anims/anims.json");
	if (J.is_empty()) return false;
	Array Bones = J["bones"], Par = J["parents"], RP = J["ref_pos"], RR = J["ref_rot"];
	const int NB = Bones.size();
	BoneNames.resize(NB); Parents.resize(NB); RefPose.resize(NB);
	for (int i = 0; i < NB; ++i) {
		BoneNames[i] = S(Bones[i]);
		BoneIndex[BoneNames[i]] = i;
		Parents[i] = (int)Par[i];
		Array p = RP[i], r = RR[i];
		RefPose[i] = Xform(Quat((double)r[0], (double)r[1], (double)r[2], (double)r[3]).GetNormalized(), Vec3((double)p[0], (double)p[1], (double)p[2]));
	}
	{
		std::ifstream f(DataDir + "/anims/anims.bin", std::ios::binary | std::ios::ate);
		if (!f) { UtilityFunctions::push_error("gasp: cannot open anims.bin"); return false; }
		const size_t Size = (size_t)f.tellg();
		f.seekg(0);
		Blob.resize(Size);
		f.read(Blob.data(), Size);
	}
	Array Anims = J["anims"];
	Clips.resize(Anims.size());
	for (int i = 0; i < Anims.size(); ++i) {
		Dictionary a = Anims[i];
		AnimClip& C = Clips[i];
		C.Name = S(a["name"]); C.Path = S(a["path"]);
		C.Length = (float)(double)a["length"]; C.Fps = (float)(double)a["fps"];
		C.NumKeys = (int)a["num_keys"];
		C.bLoop = (bool)a["loop"]; C.bRootMotion = (bool)a["root_motion"]; C.bLockRoot = (bool)a["lock_root"];
		C.Keys = reinterpret_cast<const float*>(Blob.data() + (int64_t)a["offset"]);
		C.CurveData = reinterpret_cast<const float*>(Blob.data() + (int64_t)a["curve_offset"]);
		Array Cv = a["curves"];
		for (int k = 0; k < Cv.size(); ++k) C.CurveIds.push_back(AddCurveName(S(Cv[k])));
		ClipIndex[C.Path] = i;
		ClipIndex[C.Name] = i;
	}
	// extra curve names used by graph logic even if absent
	for (const char* N : {"Enable_OrientationWarping", "Enable_PlayRateWarping", "MoveData_Speed", "MaxDynamicPlayRate", "MinDynamicPlayRate",
		"Disable_AO", "MovingTraversal", "footspeed_l", "footspeed_r", "Phase", "Disable_Legs", "Disable_FootPlacement"})
		AddCurveName(N);
	for (AnimClip& C : Clips) {
		C.GlobalToLocal.assign(CurveNames.size(), -1);
		for (int k = 0; k < (int)C.CurveIds.size(); ++k) C.GlobalToLocal[C.CurveIds[k]] = k;
	}
	// notifies
	Dictionary N = LoadJson(DataDir + "/notifies.json");
	Array NK = N.keys();
	for (int i = 0; i < NK.size(); ++i) {
		std::string Path = S(NK[i]);
		auto It = ClipIndex.find(Path);
		if (It != ClipIndex.end()) ParseNotifies(N[NK[i]], Clips[It->second].Notifies);
	}
	// per-notify TriggerWeightThreshold, one value per notifies.json event (tools/build/build_notify_thresholds.py)
	Dictionary NotifyThresholds = LoadJson(DataDir + "/notify_thresholds.json");
	auto ApplyThresholds = [&NotifyThresholds](const std::string& Path, std::vector<NotifyEvent>& Notifies) {
		String Key(Path.c_str());
		if (!NotifyThresholds.has(Key)) return;
		Array T = NotifyThresholds[Key];
		if (T.size() != (int)Notifies.size()) return;
		for (int k = 0; k < T.size(); ++k) Notifies[k].TriggerWeightThreshold = (float)(double)T[k];
	};
	for (AnimClip& C : Clips) ApplyThresholds(C.Path, C.Notifies);
	// montages (from mm.json)
	Dictionary MM = LoadJson(DataDir + "/mm/mm.json");
	Dictionary Mo = MM.get("montages", Dictionary());
	Array MK = Mo.keys();
	for (int i = 0; i < MK.size(); ++i) {
		Dictionary m = Mo[MK[i]];
		MontageInfo Info;
		Info.Path = S(MK[i]);
		Info.Anim = FindAnim(S(m["sequence"]));
		Info.Length = (float)(double)m["length"];
		if (N.has(MK[i])) ParseNotifies(N[MK[i]], Info.Notifies);
		ApplyThresholds(Info.Path, Info.Notifies);
		MontageIndex[Info.Path] = (int)Montages.size();
		Montages.push_back(Info);
	}
	// montage blend / warping settings (tools/build/build_montages.py)
	Dictionary MS = LoadJson(DataDir + "/montages.json");
	auto Opt = [](const std::string& N) {
		if (N == "HermiteCubic") return BlendOption::HermiteCubic;
		if (N == "Cubic") return BlendOption::Cubic;
		if (N == "Sinusoidal") return BlendOption::Sinusoidal;
		return BlendOption::Linear;
	};
	for (MontageInfo& Info : Montages) {
		String Key(Info.Path.c_str());
		if (!MS.has(Key)) continue;
		Dictionary m = MS[Key];
		Dictionary bi = m["blend_in"], bo = m["blend_out"];
		Info.BlendInTime = (float)(double)bi["time"]; Info.BlendInOption = Opt(S(bi["option"]));
		Info.BlendOutTime = (float)(double)bo["time"]; Info.BlendOutOption = Opt(S(bo["option"]));
		Info.BlendOutTriggerTime = (float)(double)m["blend_out_trigger"];
		Info.bEnableAutoBlendOut = (bool)m["auto_blend_out"];
		Array W = m["warps"];
		for (int k = 0; k < W.size(); ++k) {
			Dictionary w = W[k];
			WarpWindow ww;
			ww.Start = (float)(double)w["start"]; ww.End = (float)(double)w["end"];
			ww.Target = S(w["target"]); ww.Bone = S(w["bone"]);
			ww.bBoneProvider = S(w["provider"]) == "Bone";
			ww.bWarpTranslation = (bool)w["warp_translation"]; ww.bIgnoreZAxis = (bool)w["ignore_z"]; ww.bWarpRotation = (bool)w["warp_rotation"];
			ww.RotationTimeMultiplier = (float)(double)w["rot_mult"];
			Info.Warps.push_back(ww);
		}
		Array CV = m.get("curves", Array());
		for (int k = 0; k < CV.size(); ++k) {
			Dictionary c = CV[k];
			MontageInfo::Curve mc;
			mc.Index = AddCurveName(S(c["name"]));
			Array K = c["keys"];
			for (int j = 0; j < K.size(); ++j) {
				Array kv = K[j];
				mc.Keys.push_back({ (float)(double)kv[0], (float)(double)kv[1] });
			}
			Info.Curves.push_back(mc);
		}
		Array B = m["blendouts"];
		for (int k = 0; k < B.size(); ++k) {
			Dictionary b = B[k];
			BlendOutWindow bw;
			bw.Start = (float)(double)b["start"]; bw.End = (float)(double)b["end"];
			bw.BlendOutTime = (float)(double)b["time"]; bw.Profile = S(b["profile"]); bw.Condition = (int)b["condition"];
			Info.BlendOuts.push_back(bw);
		}
	}
	// additive info
	Dictionary Add = LoadJson(DataDir + "/additive.json");
	Array AK = Add.keys();
	for (int i = 0; i < AK.size(); ++i) {
		int Ai = FindAnim(S(AK[i]));
		if (Ai < 0) continue;
		Dictionary d = Add[AK[i]];
		std::string T = S(d["additive_anim_type"]);
		if (T.find("MESH_SPACE") != std::string::npos) Clips[Ai].AdditiveType = 2;
		else if (T.find("LOCAL") != std::string::npos) Clips[Ai].AdditiveType = 1;
		else continue;
		std::string RT = S(d["ref_pose_type"]);
		Variant Seq = d["ref_pose_seq"];
		Clips[Ai].AdditiveBaseAnim = (Seq.get_type() == Variant::STRING) ? FindAnim(S(Seq)) : Ai;
		if (RT.find("LOCAL_ANIM_FRAME") != std::string::npos || Clips[Ai].AdditiveBaseAnim < 0) Clips[Ai].AdditiveBaseAnim = Ai;
		Clips[Ai].AdditiveBaseFrame = (int)d["ref_frame_index"];
	}
	// blend profiles
	Dictionary BP = LoadJson(DataDir + "/blend_profiles.json");
	Array BK = BP.keys();
	for (int i = 0; i < BK.size(); ++i) {
		Dictionary d = BP[BK[i]];
		BlendProfile P;
		P.bWeightFactor = S(d["mode"]) == "WeightFactor";
		P.Scales.assign(NB, 1.f); P.Has.assign(NB, 0);
		Dictionary E = d["entries"];
		Array EK = E.keys();
		for (int k = 0; k < EK.size(); ++k) {
			int b = FindBone(S(EK[k]));
			if (b >= 0) { P.Scales[b] = (float)(double)E[EK[k]]; P.Has[b] = 1; }
		}
		BlendProfiles[S(BK[i])] = P;
	}
	bLoaded = true;
	UtilityFunctions::print(String("gasp: loaded ") + String::num_int64(Clips.size()) + " clips, " + String::num_int64(Montages.size()) + " montages, " + String::num_int64(CurveNames.size()) + " curves");
	return true;
}

const AnimLibrary::BlendProfile* AnimLibrary::FindBlendProfile(const std::string& Name) const {
	auto It = BlendProfiles.find(Name);
	return It == BlendProfiles.end() ? nullptr : &It->second;
}

int AnimLibrary::FindBone(const std::string& Name) const { auto It = BoneIndex.find(Name); return It == BoneIndex.end() ? -1 : It->second; }
int AnimLibrary::FindCurve(const std::string& Name) const { auto It = CurveIndex.find(LowerName(Name)); return It == CurveIndex.end() ? -1 : It->second; }
int AnimLibrary::FindAnim(const std::string& P) const {
	auto It = ClipIndex.find(P);
	if (It != ClipIndex.end()) return It->second;
	// tolerate "/Path/Name.Name"
	size_t Dot = P.find('.');
	if (Dot != std::string::npos) { It = ClipIndex.find(P.substr(0, Dot)); if (It != ClipIndex.end()) return It->second; }
	return -1;
}
int AnimLibrary::FindMontage(const std::string& P) const {
	auto It = MontageIndex.find(P);
	if (It != MontageIndex.end()) return It->second;
	size_t Dot = P.find('.');
	if (Dot != std::string::npos) { It = MontageIndex.find(P.substr(0, Dot)); if (It != MontageIndex.end()) return It->second; }
	return -1;
}

static inline void KeyFrame(const AnimClip& C, float Time, int& K0, int& K1, float& Alpha) {
	const float T = Clamp(Time, 0.f, C.Length);
	const float F = T * C.Fps;
	K0 = Clamp((int)std::floor(F), 0, C.NumKeys - 1);
	K1 = std::min(K0 + 1, C.NumKeys - 1);
	Alpha = F - (float)K0;
	if (K0 == K1) Alpha = 0.f;
	Alpha = Clamp(Alpha, 0.f, 1.f);
}

static inline Xform KeyXform(const float* K) {
	return Xform(Quat(K[3], K[4], K[5], K[6]), Vec3(K[0], K[1], K[2]));
}

Xform AnimLibrary::SampleBone(int Anim, int Bone, float Time) const {
	const AnimClip& C = Clips[Anim];
	const int NB = NumBones();
	int K0, K1; float A;
	KeyFrame(C, Time, K0, K1, A);
	const float* P0 = C.Keys + ((size_t)K0 * NB + Bone) * 7;
	const float* P1 = C.Keys + ((size_t)K1 * NB + Bone) * 7;
	Xform X0 = KeyXform(P0);
	if (A <= 0.f) return X0;
	Xform X1 = KeyXform(P1);
	Xform R;
	R.Translation = Vec3::Lerp(X0.Translation, X1.Translation, A);
	R.Rotation = Quat::FastLerp(X0.Rotation, X1.Rotation, A).GetNormalized();
	return R;
}

Xform AnimLibrary::SampleRootRaw(int Anim, float Time) const { return SampleBone(Anim, 0, Time); }

void AnimLibrary::SampleLocal(int Anim, float Time, Pose& Out) const {
	const AnimClip& C = Clips[Anim];
	const int NB = NumBones();
	if ((int)Out.Bones.size() != NB) Out.Init(NB, NumCurves());
	int K0, K1; float A;
	KeyFrame(C, Time, K0, K1, A);
	const float* B0 = C.Keys + (size_t)K0 * NB * 7;
	const float* B1 = C.Keys + (size_t)K1 * NB * 7;
	for (int b = 0; b < NB; ++b) {
		Xform X0 = KeyXform(B0 + b * 7);
		if (A > 0.f) {
			Xform X1 = KeyXform(B1 + b * 7);
			X0.Translation = Vec3::Lerp(X0.Translation, X1.Translation, A);
			X0.Rotation = Quat::FastLerp(X0.Rotation, X1.Rotation, A).GetNormalized();
		}
		Out.Bones[b] = X0;
	}
	if (C.bLockRoot) Out.Bones[0] = RefPose[0];
	Out.ClearCurves();
	const int NC = (int)C.CurveIds.size();
	for (int k = 0; k < NC; ++k) {
		const float V0 = C.CurveData[(size_t)K0 * NC + k];
		const float V1 = C.CurveData[(size_t)K1 * NC + k];
		Out.SetCurve(C.CurveIds[k], V0 + (V1 - V0) * A);
	}
}

float AnimLibrary::SampleCurve(int Anim, int GlobalCurve, float Time, bool* bFound) const {
	const AnimClip& C = Clips[Anim];
	if (GlobalCurve < 0 || GlobalCurve >= (int)C.GlobalToLocal.size() || C.GlobalToLocal[GlobalCurve] < 0) {
		if (bFound) *bFound = false;
		return 0.f;
	}
	if (bFound) *bFound = true;
	const int k = C.GlobalToLocal[GlobalCurve];
	const int NC = (int)C.CurveIds.size();
	int K0, K1; float A;
	KeyFrame(C, Time, K0, K1, A);
	const float V0 = C.CurveData[(size_t)K0 * NC + k];
	const float V1 = C.CurveData[(size_t)K1 * NC + k];
	return V0 + (V1 - V0) * A;
}

Xform AnimLibrary::ExtractRootMotionFromRange(int Anim, float A, float B) const {
	const AnimClip& C = Clips[Anim];
	if (!C.bRootMotion) return Xform();
	const Xform RA = SampleRootRaw(Anim, A);
	const Xform RB = SampleRootRaw(Anim, B);
	return RB.GetRelativeTransform(RA);
}

Xform AnimLibrary::ExtractRootMotion(int Anim, float StartTime, float DeltaTime, bool bLoop) const {
	const AnimClip& C = Clips[Anim];
	if (!C.bRootMotion || DeltaTime == 0.f) return Xform();
	// FRootMotionMovementParams accumulation over wrapped ranges (UAnimSequence::ExtractRootMotion)
	Xform Acc;
	float Prev = StartTime;
	float Remaining = DeltaTime;
	const float L = C.Length;
	int Guard = 0;
	while (Remaining != 0.f && Guard++ < 8) {
		float Next = Prev + Remaining;
		if (bLoop) {
			if (Remaining > 0.f && Next > L) {
				Acc = ExtractRootMotionFromRange(Anim, Prev, L) * Acc;
				Remaining = Next - L; Prev = 0.f; continue;
			}
			if (Remaining < 0.f && Next < 0.f) {
				Acc = ExtractRootMotionFromRange(Anim, Prev, 0.f) * Acc;
				Remaining = Next; Prev = L; continue;
			}
		}
		Next = Clamp(Next, 0.f, L);
		Acc = ExtractRootMotionFromRange(Anim, Prev, Next) * Acc;
		break;
	}
	return Acc;
}

void AnimLibrary::LocalToComponent(const std::vector<Xform>& Local, std::vector<Xform>& OutCS) const {
	const int NB = (int)Local.size();
	OutCS.resize(NB);
	for (int b = 0; b < NB; ++b) {
		const int P = Parents[b];
		OutCS[b] = P < 0 ? Local[b] : Local[b] * OutCS[P];
	}
}

} // namespace gasp
