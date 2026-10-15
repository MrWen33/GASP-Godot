#include "post_process.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <algorithm>
#include <cmath>

using namespace gasp;
using ue::Clamp;

namespace {

std::string S(const godot::Variant& v) { return std::string(godot::String(v).utf8().get_data()); }
double F(const godot::Dictionary& d, const char* k, double def) { godot::Variant v = d.get(k, def); return (double)v; }
bool B(const godot::Dictionary& d, const char* k, bool def) { return (bool)d.get(k, def); }
std::string Str(const godot::Dictionary& d, const char* k, const char* def) { return S(d.get(k, def)); }
Vec3 V3(const godot::Variant& v, const Vec3& def = Vec3()) {
	if (v.get_type() != godot::Variant::ARRAY) return def;
	godot::Array a = v;
	return a.size() >= 3 ? Vec3((double)a[0], (double)a[1], (double)a[2]) : def;
}
// [pitch, yaw, roll] degrees -> quat
Quat RotPYR(const godot::Variant& v) {
	godot::Array a = v;
	return a.size() >= 3 ? Quat::MakeFromEulerPYR((double)a[0], (double)a[1], (double)a[2]) : Quat();
}
// [qx qy qz qw tx ty tz sx sy sz]
Xform XF(const godot::Variant& v) {
	godot::Array a = v;
	if (a.size() < 10) return Xform();
	return Xform(Quat((double)a[0], (double)a[1], (double)a[2], (double)a[3]), Vec3((double)a[4], (double)a[5], (double)a[6]), Vec3((double)a[7], (double)a[8], (double)a[9]));
}

// small 3x3 matrix for inertia tensors
struct M3 {
	double m[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
	static M3 Diag(double a, double b, double c) { M3 r; r.m[0][0] = a; r.m[1][1] = b; r.m[2][2] = c; return r; }
	static M3 FromQuat(const Quat& q) {
		M3 r;
		const Vec3 X = q.GetAxisX(), Y = q.GetAxisY(), Z = q.GetAxisZ();
		r.m[0][0] = X.X; r.m[1][0] = X.Y; r.m[2][0] = X.Z;
		r.m[0][1] = Y.X; r.m[1][1] = Y.Y; r.m[2][1] = Y.Z;
		r.m[0][2] = Z.X; r.m[1][2] = Z.Y; r.m[2][2] = Z.Z;
		return r;
	}
	Vec3 operator*(const Vec3& v) const {
		return Vec3(m[0][0] * v.X + m[0][1] * v.Y + m[0][2] * v.Z, m[1][0] * v.X + m[1][1] * v.Y + m[1][2] * v.Z, m[2][0] * v.X + m[2][1] * v.Y + m[2][2] * v.Z);
	}
	M3 operator*(const M3& o) const {
		M3 r;
		for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { double s = 0; for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j]; r.m[i][j] = s; }
		return r;
	}
	M3 operator+(const M3& o) const { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][j] + o.m[i][j]; return r; }
	M3 operator*(double s) const { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][j] * s; return r; }
	M3 T() const { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i]; return r; }
	M3 Inverse() const {
		const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1], i = m[2][2];
		const double A = e * i - f * h, Bc = -(d * i - f * g), C = d * h - e * g;
		const double det = a * A + b * Bc + c * C;
		M3 r;
		if (std::abs(det) < 1e-12) return r;
		const double id = 1.0 / det;
		r.m[0][0] = A * id; r.m[0][1] = -(b * i - c * h) * id; r.m[0][2] = (b * f - c * e) * id;
		r.m[1][0] = Bc * id; r.m[1][1] = (a * i - c * g) * id; r.m[1][2] = -(a * f - c * d) * id;
		r.m[2][0] = C * id; r.m[2][1] = -(a * h - b * g) * id; r.m[2][2] = (a * e - b * d) * id;
		return r;
	}
};

Quat FromAxes(const Vec3& Pri, const Vec3& Sec) {
	// FConstraintFrame: X = primary, Y = secondary, Z = X ^ Y
	const Vec3 X = Pri.GetSafeNormal();
	Vec3 Z = (X ^ Sec).GetSafeNormal();
	const Vec3 Y = Z ^ X;
	// rotation matrix columns X Y Z -> quat
	const double m00 = X.X, m01 = Y.X, m02 = Z.X, m10 = X.Y, m11 = Y.Y, m12 = Z.Y, m20 = X.Z, m21 = Y.Z, m22 = Z.Z;
	const double tr = m00 + m11 + m22;
	Quat q;
	if (tr > 0) {
		const double s = std::sqrt(tr + 1.0) * 2;
		q = Quat((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s);
	} else if (m00 > m11 && m00 > m22) {
		const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
		q = Quat(0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
	} else if (m11 > m22) {
		const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
		q = Quat((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s);
	} else {
		const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
		q = Quat((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s);
	}
	return q.GetNormalized();
}

Vec3 RotationVector(Quat q) {
	if (q.W < 0) q = q * -1.0;
	return q.ToRotationVector();
}

} // namespace

// ================================================================================================================ PPPose
void PPPose::SetCSBatch(const std::vector<std::pair<int, Xform>>& Sorted) {
	if (Sorted.empty()) return;
	UpdateCS();
	size_t k = 0;
	const int First = Sorted[0].first;
	for (int b = First; b < (int)Local.size(); ++b) {
		const int P = Parents[b];
		if (k < Sorted.size() && Sorted[k].first == b) {
			CS[b] = Sorted[k].second;
			Local[b] = P < 0 ? CS[b] : CS[b].GetRelativeTransform(CS[P]);
			++k;
		} else if (P >= 0) {
			CS[b] = Local[b] * CS[P];
		}
	}
	bCSDirty = false;
}

namespace gasp {

// ============================================================================================================ ModifyCurve
struct PPModifyCurve : PPNode {
	std::vector<std::pair<int, float>> Values;
	void Evaluate(PPPose&, PPCurves& C, const PPFrame&) override {
		for (auto& v : Values) C.Values[v.first] = v.second;
	}
};

// ============================================================================================================ PoseDriver
enum class RBFDist { Euclidean, Quaternion, SwingAngle, TwistAngle, Default };
enum class RBFFunc { Linear, Gaussian, Exponential, Cubic, Quintic, Default };
enum class RBFNorm { OnlyAboveOne, Always, WithinMedian };

static RBFDist ParseDist(const std::string& s) {
	if (s == "Euclidean") return RBFDist::Euclidean;
	if (s == "Quaternion") return RBFDist::Quaternion;
	if (s == "SwingAngle") return RBFDist::SwingAngle;
	if (s == "TwistAngle") return RBFDist::TwistAngle;
	return RBFDist::Default;
}
static RBFFunc ParseFunc(const std::string& s) {
	if (s == "Linear") return RBFFunc::Linear;
	if (s == "Gaussian") return RBFFunc::Gaussian;
	if (s == "Exponential") return RBFFunc::Exponential;
	if (s == "Cubic") return RBFFunc::Cubic;
	if (s == "Quintic") return RBFFunc::Quintic;
	return RBFFunc::Default;
}

// RBFDistanceMetric / RBFKernel / FRBFSolver (Engine/Source/Runtime/AnimGraphRuntime/Private/RBF)
struct RBFEntry {
	std::vector<double> V;  // per source: roll, pitch, yaw (or x, y, z)
	Quat AsQuat(int i) const { return Quat::MakeFromEulerPYR(V[i * 3 + 1], V[i * 3 + 2], V[i * 3 + 0]); }
	void AddRotator(const Quat& q) { double p, y, r; q.ToRotator(p, y, r); V.push_back(r); V.push_back(p); V.push_back(y); }
	void AddVector(const Vec3& v) { V.push_back(v.X); V.push_back(v.Y); V.push_back(v.Z); }
};

static double EntryDistance(const RBFEntry& A, const RBFEntry& B, RBFDist M, const Vec3& TwistAxis) {
	const int N = (int)A.V.size() / 3;
	double Total = 0;
	for (int i = 0; i < N; ++i) {
		double D = 0;
		switch (M) {
		case RBFDist::Euclidean: {
			const Vec3 a(ue::DegToRad(A.V[i * 3]), ue::DegToRad(A.V[i * 3 + 1]), ue::DegToRad(A.V[i * 3 + 2]));
			const Vec3 b(ue::DegToRad(B.V[i * 3]), ue::DegToRad(B.V[i * 3 + 1]), ue::DegToRad(B.V[i * 3 + 2]));
			D = (a - b).Size();
			break;
		}
		case RBFDist::Quaternion: D = Quat::AngularDistance(A.AsQuat(i).GetNormalized(), B.AsQuat(i).GetNormalized()); break;
		case RBFDist::TwistAngle: D = std::abs(A.AsQuat(i).GetTwistAngle(TwistAxis) - B.AsQuat(i).GetTwistAngle(TwistAxis)); break;
		case RBFDist::SwingAngle:
		case RBFDist::Default:
		default: {
			Quat AS, BS, T;
			A.AsQuat(i).ToSwingTwist(TwistAxis, AS, T);
			B.AsQuat(i).ToSwingTwist(TwistAxis, BS, T);
			D = Quat::AngularDistance(AS, BS);
			break;
		}
		}
		Total += D * D;
	}
	return std::sqrt(Total);
}

static double Kernel(double Value, double Width, RBFFunc Fn, bool bBackCompFix) {
	if (Value < 0) return 0;
	switch (Fn) {
	case RBFFunc::Gaussian: return bBackCompFix ? std::exp(-Value * Value) : std::exp(-Value * (1.0 / Width) * (1.0 / Width));
	case RBFFunc::Exponential: return bBackCompFix ? 1.0 / std::exp(Value) : std::exp(-2.0 * Value / Width);
	case RBFFunc::Cubic: { const double v = Value / Width; return std::max(1.0 - v * v * v, 0.0); }
	case RBFFunc::Quintic: { const double v = Value / Width; return std::max(1.0 - std::pow(v, 5.0), 0.0); }
	case RBFFunc::Linear:
	case RBFFunc::Default:
	default: return bBackCompFix ? std::max(1.0 - Value, 0.0) : (Width - Clamp(Value, 0.0, Width)) / Width;
	}
}

static bool InvertMatrix(std::vector<double>& M, int N) {
	// Gauss-Jordan with partial pivoting; returns false when singular (FRBFInterpolatorBase::SetUpperKernel determinant test)
	std::vector<double> Inv(N * N, 0.0);
	for (int i = 0; i < N; ++i) Inv[i * N + i] = 1.0;
	double Det = 1.0;
	for (int c = 0; c < N; ++c) {
		int P = c;
		for (int r = c + 1; r < N; ++r) if (std::abs(M[r * N + c]) > std::abs(M[P * N + c])) P = r;
		if (std::abs(M[P * N + c]) < 1e-300) return false;
		if (P != c) { for (int k = 0; k < N; ++k) { std::swap(M[P * N + k], M[c * N + k]); std::swap(Inv[P * N + k], Inv[c * N + k]); } Det = -Det; }
		const double D = M[c * N + c];
		Det *= D;
		for (int k = 0; k < N; ++k) { M[c * N + k] /= D; Inv[c * N + k] /= D; }
		for (int r = 0; r < N; ++r) {
			if (r == c) continue;
			const double Fct = M[r * N + c];
			if (Fct == 0) continue;
			for (int k = 0; k < N; ++k) { M[r * N + k] -= Fct * M[c * N + k]; Inv[r * N + k] -= Fct * Inv[c * N + k]; }
		}
	}
	if (std::abs((float)Det) <= 1e-8) return false;
	M = Inv;
	return true;
}

struct PoseAssetData {
	bool bAdditive = false;
	int BasePoseIndex = -1;
	std::vector<std::string> PoseNames;
	std::vector<int> TrackBone;                               // skeleton bone per track (-1 missing)
	std::vector<std::vector<Xform>> Poses;                     // LocalSpacePose per pose
	std::vector<std::vector<std::pair<int, int>>> Influences;  // per track: (pose, bone transform index)
};

struct PPPoseDriver : PPNode {
	std::vector<int> SourceBones;
	int EvalSpaceBone = -1;
	bool bEvalFromRefPose = false;
	bool bTranslation = false;
	bool bDriveCurves = false;
	bool bInterpolative = true;
	double Radius = 1.0;
	RBFFunc Function = RBFFunc::Gaussian;
	RBFDist Distance = RBFDist::SwingAngle;
	Vec3 TwistAxis = Vec3(1, 0, 0);
	double WeightThreshold = ue::KINDA_SMALL_NUMBER;
	RBFNorm Normalize = RBFNorm::OnlyAboveOne;
	Vec3 MedianReference;
	double MedianMin = 45, MedianMax = 60;
	struct Target { RBFEntry E; int Curve = -1; int Pose = -1; double Scale = 1; RBFDist Dist = RBFDist::Default; RBFFunc Fn = RBFFunc::Default; };
	std::vector<Target> Targets;
	// interpolative solver data
	std::vector<double> Coeffs;
	bool bSolverValid = false;
	double KernelWidth = 1;
	// pose asset
	bool bHasPoseAsset = false;
	PoseAssetData PA;
	std::vector<char> Filtered;   // per skeleton bone: zeroed in the additive pose (source bones / bones not in OnlyDriveBones)
	std::vector<char> IsTrack;    // per skeleton bone: driven by the pose asset (BoneBlendWeights)
	std::vector<float> Weights;

	double WeightFn(const RBFEntry& A, const RBFEntry& B) const { return Kernel(EntryDistance(A, B, Distance, TwistAxis), KernelWidth, Function, false); }

	void Init() {
		const int N = (int)Targets.size();
		if (bInterpolative) {
			if (RadiusAuto) {
				double Sum = 0; int Count = 0;
				for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) if (i != j) { Sum += EntryDistance(Targets[i].E, Targets[j].E, Distance, TwistAxis); ++Count; }
				KernelWidth = Count ? Sum / Count : 1.0;
			} else {
				KernelWidth = ue::DegToRad(Radius);
			}
			if (N >= 2) {
				std::vector<double> K(N * N, 0.0);
				for (int i = 0; i < N; ++i) for (int j = i; j < N; ++j) K[i * N + j] = K[j * N + i] = WeightFn(Targets[i].E, Targets[j].E);
				bSolverValid = InvertMatrix(K, N);
				Coeffs = K;
			} else {
				bSolverValid = true;
			}
		}
		Weights.assign(N, 0.f);
	}
	bool RadiusAuto = false;

	void Solve(const RBFEntry& Input, std::vector<std::pair<int, double>>& Out) const {
		const int N = (int)Targets.size();
		std::vector<double> W(N, 0.0);
		if (bInterpolative) {
			if (!bSolverValid) { for (auto& w : W) w = 0; }
			else if (N > 1) {
				std::vector<double> VW(N);
				for (int j = 0; j < N; ++j) VW[j] = WeightFn(Input, Targets[j].E);
				for (int i = 0; i < N; ++i) {
					double s = 0;
					for (int j = 0; j < N; ++j) s += Coeffs[j * N + i] * VW[j];
					W[i] = Clamp(s, 0.0, 1.0);
				}
			} else if (N == 1) {
				W[0] = 1;
			}
			for (int i = 0; i < N; ++i) W[i] *= Targets[i].Scale;
		} else {
			for (int i = 0; i < N; ++i) {
				const RBFFunc Fn = Targets[i].Fn == RBFFunc::Default ? Function : Targets[i].Fn;
				const RBFDist Dm = Targets[i].Dist == RBFDist::Default ? Distance : Targets[i].Dist;
				const double D = ue::RadToDeg(EntryDistance(Targets[i].E, Input, Dm, TwistAxis));
				const double Sc = std::max(Radius * Targets[i].Scale, ue::KINDA_SMALL_NUMBER);
				W[i] = Kernel(D / Sc, 1.0, Fn, true);
			}
		}
		double Total = 0;
		for (double w : W) Total += w;
		if (Total > ue::KINDA_SMALL_NUMBER) {
			double Scale = 1;
			if (Total > 1) Scale = 1.0 / Total;
			else if (Normalize == RBFNorm::Always) Scale = 1.0 / Total;
			else if (Normalize == RBFNorm::WithinMedian && MedianMax >= MedianMin) {
				RBFEntry Med;
				while (Med.V.size() < Input.V.size()) Med.AddVector(MedianReference);
				const double MD = ue::RadToDeg(EntryDistance(Input, Med, Distance, TwistAxis));
				if (MD <= MedianMin) Scale = 1.0 / Total;
				else if (MD <= MedianMax) Scale = ue::Lerp(1.0 / Total, 1.0, Clamp((MD - MedianMin) / (MedianMax - MedianMin), 0.0, 1.0));
			}
			for (int i = 0; i < N; ++i) {
				const double NW = W[i] * Scale;
				if (NW > WeightThreshold) Out.push_back({ i, NW });
			}
		}
	}

	void Evaluate(PPPose& Pose, PPCurves& C, const PPFrame&) override {
		RBFEntry Input;
		bool bFound = false;
		for (int sb : SourceBones) {
			Xform TM;
			if (sb >= 0) {
				if (EvalSpaceBone >= 0) TM = Pose.GetCS(sb).GetRelativeTransform(Pose.GetCS(EvalSpaceBone));
				else if (bEvalFromRefPose) TM = Pose.Local[sb].GetRelativeTransform(Pose.RefLocal[sb]);
				else TM = Pose.Local[sb];
				bFound = true;
			}
			if (bTranslation) Input.AddVector(TM.Translation);
			else Input.AddRotator(TM.Rotation);
		}
		if (!bFound || Targets.empty()) return;
		std::vector<std::pair<int, double>> Out;
		Solve(Input, Out);
		if (Out.empty()) return;
		if (bDriveCurves) {
			for (auto& w : Out) if (Targets[w.first].Curve >= 0) C.Values[Targets[w.first].Curve] = (float)w.second;
			return;
		}
		if (!bHasPoseAsset) return;
		// UPoseAsset::GetAnimationPose (weights per pose) then FAnimNode_PoseDriver additive accumulate / per bone blend
		const int NP = (int)PA.Poses.size();
		std::vector<double> PW(NP, 0.0);
		std::vector<char> Weighted(NP, 0);
		double TotalWeight = 0;
		int NumCurves = 0;
		for (auto& w : Out) {
			const int P = Targets[w.first].Pose;
			if (P < 0) continue;
			++NumCurves;
			if ((!PA.bAdditive || P != PA.BasePoseIndex) && std::abs(w.second) > ue::ZERO_ANIMWEIGHT_THRESH) {
				PW[P] = w.second; Weighted[P] = 1; TotalWeight += w.second;
			}
		}
		if (NumCurves == 1 && !PA.bAdditive) for (auto& p : PW) p = Clamp(p, 0.0, 1.0);
		if (!PA.bAdditive && NumCurves > 1 && TotalWeight > 1) for (auto& p : PW) p /= TotalWeight;
		const Xform AddIdentity(Quat(), Vec3(), Vec3(0, 0, 0));
		Pose.bCSDirty = true;
		for (size_t t = 0; t < PA.TrackBone.size(); ++t) {
			const int b = PA.TrackBone[t];
			if (b < 0) continue;
			const auto& Inf = PA.Influences[t];
			if (PA.bAdditive) {
				if (Filtered[b]) continue;
				Xform Acc = AddIdentity;
				for (auto& I : Inf) {
					if (!Weighted[I.first]) continue;
					const double W = PW[I.first];
					Xform Delta = PA.Poses[I.first][I.second];
					if (W < 1.0 - ue::ZERO_ANIMWEIGHT_THRESH) Delta.Blend(AddIdentity, Delta, W);
					Acc.Rotation = (Delta.Rotation * Acc.Rotation).GetNormalized();
					Acc.Translation += Delta.Translation;
					Acc.Scale3D += Delta.Scale3D;
				}
				// FAnimationRuntime::AccumulateAdditivePose (local space, weight 1)
				Xform& Out = Pose.Local[b];
				Out.Rotation = (Acc.Rotation * Out.Rotation).GetNormalized();
				Out.Translation += Acc.Translation;
				Out.Scale3D = Out.Scale3D * (Vec3(1, 1, 1) + Acc.Scale3D);
			} else {
				if (!IsTrack[b] || Filtered[b]) continue;
				Xform R = Pose.RefLocal[b];
				double TotalLocal = 0;
				bool bSet = false;
				for (auto& I : Inf) {
					if (!Weighted[I.first]) continue;
					const double W = PW[I.first];
					TotalLocal += W;
					const Xform& BP = PA.Poses[I.first][I.second];
					if (!bSet) { R = BP.Scaled(W); bSet = true; }
					else R.AccumulateWithShortestRotation(BP, W);
				}
				if (std::abs(TotalLocal) <= ue::ZERO_ANIMWEIGHT_THRESH) R = Pose.RefLocal[b];
				else if (TotalLocal < 1.0 - ue::ZERO_ANIMWEIGHT_THRESH) R.AccumulateWithShortestRotation(Pose.RefLocal[b], 1.0 - TotalLocal);
				R.NormalizeRotation();
				Pose.Local[b] = R;
			}
		}
	}
};

// ============================================================================================================ Constraint
struct PPConstraint : PPNode {
	int Bone = -1;
	struct T { int Bone; double Weight; Xform Offset; bool bOffset; };
	std::vector<T> Targets;
	void Evaluate(PPPose& Pose, PPCurves&, const PPFrame&) override {
		if (Bone < 0) return;
		std::vector<std::pair<Xform, double>> Parents;
		double Total = 0;
		for (auto& t : Targets) {
			if (t.Bone < 0 || t.Weight <= ue::ZERO_ANIMWEIGHT_THRESH) continue;
			const Xform Target = Pose.GetCS(t.Bone);
			// FConstraintData::ApplyInverseOffset (parent, base = identity) then AccumulateConstraintTransform
			const Xform OffsetTarget = t.bOffset ? t.Offset * Target : Target;
			Parents.push_back({ OffsetTarget, t.Weight });
			Total += t.Weight;
		}
		if (Parents.empty() || Total <= ue::ZERO_ANIMWEIGHT_THRESH) return;
		const double Mult = Total > 1 ? 1.0 / Total : 1.0;
		Xform R = Parents[0].first.Scaled(Parents[0].second * Mult);
		for (size_t i = 1; i < Parents.size(); ++i) R.AccumulateWithShortestRotation(Parents[i].first, Parents[i].second * Mult);
		R.NormalizeRotation();
		Pose.SetCS(Bone, R);
	}
};

// ========================================================================================================== Control Rig
struct PPControlRig : PPNode {
	struct Op {
		int Type = 0;  // 0 twist, 1 project
		int Driver = -1;
		Vec3 Axis = Vec3(1, 0, 0);
		bool bInvert = false;
		std::vector<std::pair<int, double>> TwistTargets;
		int BoneIdx = -1, NewParent = -1, OldParent = -1, NewParentB = -1, OldParentB = -1;
		double SlerpT = 0.5, Weight = 1;
		bool bPropagate = true, bSlerp = false;
	};
	std::vector<Op> Ops;

	static void SetLocalRotationKeepChildren(PPPose& Pose, int b, const Quat& R, bool bPropagate) {
		if (bPropagate) { Pose.Local[b].Rotation = R; Pose.bCSDirty = true; return; }
		Pose.UpdateCS();
		std::vector<std::pair<int, Xform>> Kids;
		for (int c = b + 1; c < (int)Pose.Local.size(); ++c) if (Pose.Parents[c] == b) Kids.push_back({ c, Pose.CS[c] });
		Pose.Local[b].Rotation = R;
		Pose.bCSDirty = true;
		if (!Kids.empty()) Pose.SetCSBatch(Kids);
	}

	void Evaluate(PPPose& Pose, PPCurves&, const PPFrame&) override {
		for (const Op& o : Ops) {
			if (o.Type == 0) {
				if (o.Driver < 0) continue;
				// MathTransformMakeRelative(Global = current local, Parent = initial local) -> SwingTwist -> (inverse) twist
				const Quat Rel = Pose.RefLocal[o.Driver].Rotation.Inverse() * Pose.Local[o.Driver].Rotation;
				Quat Swing, Twist;
				Rel.ToSwingTwist(o.Axis, Swing, Twist);
				const Quat V = o.bInvert ? Twist.Inverse() : Twist;
				for (auto& t : o.TwistTargets) {
					if (t.first < 0) continue;
					const Quat Cur = Pose.Local[t.first].Rotation;
					const Quat New = t.second >= 1.0 - ue::SMALL_NUMBER ? V : Quat::Slerp(Cur, V, Clamp(t.second, 0.0, 1.0));
					SetLocalRotationKeepChildren(Pose, t.first, New, false);
				}
			} else {
				if (o.BoneIdx < 0 || o.NewParent < 0 || o.OldParent < 0) continue;
				// FRigUnit_ProjectTransformToNewParent: Child(initial).GetRelativeTransform(OldParent(initial)) * NewParent(current)
				auto Project = [&](int NewP, int OldP) {
					return (Pose.RefCS[o.BoneIdx].GetRelativeTransform(Pose.RefCS[OldP]) * Pose.GetCS(NewP)).Rotation;
				};
				Quat Target = Project(o.NewParent, o.OldParent);
				if (o.bSlerp && o.NewParentB >= 0 && o.OldParentB >= 0) Target = Quat::Slerp(Target, Project(o.NewParentB, o.OldParentB), o.SlerpT);
				Xform G = Pose.GetCS(o.BoneIdx);
				G.Rotation = o.Weight >= 1.0 - ue::SMALL_NUMBER ? Target : Quat::Slerp(G.Rotation, Target, Clamp(o.Weight, 0.0, 1.0));
				if (o.bPropagate) {
					Pose.SetCS(o.BoneIdx, G);
				} else {
					std::vector<std::pair<int, Xform>> Batch{ { o.BoneIdx, G } };
					for (int c = o.BoneIdx + 1; c < (int)Pose.Local.size(); ++c) if (Pose.Parents[c] == o.BoneIdx) Batch.push_back({ c, Pose.CS[c] });
					Pose.SetCSBatch(Batch);
				}
			}
		}
	}
};

} // namespace gasp

// ======================================================================================================= Rig program
// Control Rig compiled by tools/build/rigvm_compile.py: flat ops over a register file of doubles (see that file for the
// slot layout). Hierarchy "global" = component space, "initial" = reference pose (bSetRefPoseFromSkeleton).
namespace {

// FMath::SpringDamper (per component; the vector version is component-wise)
void SpringDamper1(double& Value, double& Rate, double Target, double TargetRate, double Dt, double Freq, double Damping) {
	if (Dt <= 0) return;
	const double W = Freq * 2.0 * ue::PI;
	if (W < ue::SMALL_NUMBER) { Value += Rate * Dt; return; }
	if (Damping < ue::SMALL_NUMBER) {
		const double Err = Value - Target, B = Rate / W, S = std::sin(W * Dt), C = std::cos(W * Dt);
		Value = Target + Err * C + B * S;
		Rate = Rate * C - Err * (W * S);
		return;
	}
	const double Smoothing = 2.0 / W;
	const double Adj = Target + TargetRate * (Damping * Smoothing);
	const double Err = Value - Adj;
	if (Damping > 1.0) {
		const double WD = W * std::sqrt(Damping * Damping - 1.0);
		const double C2 = -(Rate + (W * Damping - WD) * Err) / (2.0 * WD);
		const double C1 = Err - C2;
		const double A1 = WD - Damping * W, A2 = -(WD + Damping * W);
		const double E1 = ue::InvExpApprox(-A1 * Dt), E2 = ue::InvExpApprox(-A2 * Dt);
		Value = Adj + E1 * C1 + E2 * C2;
		Rate = E1 * C1 * A1 + E2 * C2 * A2;
	} else if (Damping < 1.0) {
		const double WD = W * std::sqrt(1.0 - Damping * Damping);
		const double A = Err, B = (Rate + Err * (Damping * W)) / WD;
		const double S = std::sin(WD * Dt), C = std::cos(WD * Dt);
		const double E = ue::InvExpApprox(Damping * W * Dt);
		Value = E * (A * C + B * S);
		Rate = -Value * Damping * W + E * (B * (WD * C) - A * (WD * S));
		Value += Adj;
	} else {
		const double C1 = Err, C2 = Rate + Err * W;
		const double E = ue::InvExpApprox(W * Dt);
		Value = Adj + (C1 + C2 * Dt) * E;
		Rate = (C2 - C1 * W - C2 * (W * Dt)) * E;
	}
}

Xform LerpXf(const Xform& A, const Xform& B, double T) {
	return Xform(Quat::Slerp(A.Rotation, B.Rotation, T), Vec3::Lerp(A.Translation, B.Translation, T), Vec3::Lerp(A.Scale3D, B.Scale3D, T));
}

} // namespace

namespace gasp {

struct PPRigProgram : PPNode {
	enum Code {
		C_copy, C_select, C_jne, C_jmp, C_ignored,
		C_GetTransform, C_SetTransform, C_SetRotation, C_SetTranslation, C_SetRelativeTransformForItem, C_ProjectTransformToNewParent,
		C_HierarchyGetParent, C_GetCurveValue, C_SetCurveValue,
		C_Mul, C_Add, C_Sub, C_Abs, C_FloatToInt, C_Remap, C_VectorAdd, C_VectorSub, C_VectorMul,
		C_QuatSwingTwist, C_QuatToRotator, C_QuatFromRotator, C_QuatInverse, C_QuatMul, C_QuatSlerp,
		C_TransformMakeRelative, C_TransformMakeAbsolute, C_TransformMul, C_TransformLerp, C_AlphaInterp, C_SpringInterpVector,
	};
	struct Op {
		Code C;
		std::vector<int> A;
		double V = 0;
		int To = -1;
		std::string Unit;
		// state
		bool bInit = false;
		double Interp = 0;
		Vec3 SimResult, Velocity, PrevTarget;
		bool bPrevTargetValid = false;
	};
	std::vector<Op> Ops;
	std::vector<double> R, R0;
	std::vector<int> CurveMap;  // program curve index -> PPCurves index
	bool bWarned = false;

	// element keys are bone indices (or -1 none / -2 ignored element types such as controls)
	int Bone(int Slot) const { return (int)R[Slot]; }
	Quat Q(int s) const { return Quat(R[s], R[s + 1], R[s + 2], R[s + 3]); }
	Vec3 V(int s) const { return Vec3(R[s], R[s + 1], R[s + 2]); }
	Xform X(int s) const { return Xform(Q(s), V(s + 4), V(s + 7)); }
	void PutQ(int s, const Quat& q) { R[s] = q.X; R[s + 1] = q.Y; R[s + 2] = q.Z; R[s + 3] = q.W; }
	void PutV(int s, const Vec3& v) { R[s] = v.X; R[s + 1] = v.Y; R[s + 2] = v.Z; }
	void PutX(int s, const Xform& x) { PutQ(s, x.Rotation); PutV(s + 4, x.Translation); PutV(s + 7, x.Scale3D); }

	static Xform GetXf(PPPose& Pose, int b, bool bGlobal, bool bInitial) {
		if (b < 0) return Xform();
		if (bGlobal) return bInitial ? Pose.RefCS[b] : Pose.GetCS(b);
		return bInitial ? Pose.RefLocal[b] : Pose.Local[b];
	}

	static void SetXf(PPPose& Pose, int b, bool bGlobal, const Xform& Value, double Weight, bool bPropagate) {
		if (b < 0 || Weight < ue::SMALL_NUMBER) return;
		Xform W = Value;
		if (Weight < 1.0 - ue::SMALL_NUMBER) W = LerpXf(GetXf(Pose, b, bGlobal, false), Value, Weight);
		std::vector<std::pair<int, Xform>> Batch;
		if (!bPropagate) {
			Pose.UpdateCS();
			for (int c = b + 1; c < (int)Pose.Local.size(); ++c) if (Pose.Parents[c] == b) Batch.push_back({ c, Pose.CS[c] });
		}
		if (bGlobal) {
			Batch.insert(Batch.begin(), { b, W });
			Pose.SetCSBatch(Batch);
		} else {
			Pose.Local[b] = W;
			Pose.bCSDirty = true;
			if (!Batch.empty()) Pose.SetCSBatch(Batch);
		}
	}

	void Reset() override {
		R = R0;
		for (Op& o : Ops) { o.bInit = false; o.Interp = 0; o.SimResult = o.Velocity = o.PrevTarget = Vec3(); o.bPrevTargetValid = false; }
	}

	void Evaluate(PPPose& Pose, PPCurves& Curves, const PPFrame& Frame) override {
		const double Dt = Frame.DeltaTime;
		const int N = (int)Ops.size();
		for (int i = 0; i < N; ++i) {
			Op& o = Ops[i];
			const std::vector<int>& a = o.A;
			switch (o.C) {
			case C_copy: for (int k = 0; k < a[2]; ++k) R[a[0] + k] = R[a[1] + k]; break;
			case C_select: for (int k = 0; k < a[4]; ++k) R[a[3] + k] = R[a[0]] != 0 ? R[a[1] + k] : R[a[2] + k]; break;
			case C_jne: if (std::floor(R[a[0]]) != o.V) i = o.To - 1; break;
			case C_jmp: i = o.To - 1; break;
			case C_ignored:
				if (!bWarned) { bWarned = true; godot::UtilityFunctions::push_warning(godot::String("GaspPostProcess: rig unit not supported: ") + godot::String(o.Unit.c_str())); }
				break;
			case C_GetTransform: PutX(a[3], GetXf(Pose, Bone(a[0]), R[a[1]] == 1, R[a[2]] != 0)); break;
			case C_SetTransform:
			case C_SetRotation:
			case C_SetTranslation: {
				if (R[a[2]] != 0) break;  // initial transforms are not changed during the forward solve
				const int b = Bone(a[0]);
				if (b < 0) break;
				const bool G = R[a[1]] == 1;
				Xform T;
				if (o.C == C_SetTransform) T = X(a[3]);
				else {
					T = GetXf(Pose, b, G, false);
					if (o.C == C_SetRotation) T.Rotation = Q(a[3]);
					else T.Translation = V(a[3]);
				}
				SetXf(Pose, b, G, T, R[a[4]], R[a[5]] != 0);
				break;
			}
			case C_SetRelativeTransformForItem: {
				if (R[a[4]] < ue::SMALL_NUMBER) break;
				const Xform P = GetXf(Pose, Bone(a[1]), true, R[a[2]] != 0);
				Xform G = X(a[3]) * P;
				G.NormalizeRotation();
				SetXf(Pose, Bone(a[0]), true, G, R[a[4]], R[a[5]] != 0);
				break;
			}
			case C_ProjectTransformToNewParent: {
				const Xform C = GetXf(Pose, Bone(a[0]), true, R[a[1]] != 0);
				const Xform Old = GetXf(Pose, Bone(a[2]), true, R[a[3]] != 0);
				const Xform New = GetXf(Pose, Bone(a[4]), true, R[a[5]] != 0);
				Xform Rel = C.GetRelativeTransform(Old);
				Rel.NormalizeRotation();
				Xform Out = Rel * New;
				Out.NormalizeRotation();
				PutX(a[6], Out);
				break;
			}
			case C_HierarchyGetParent: { const int b = Bone(a[0]); R[a[2]] = b >= 0 ? Pose.Parents[b] : -1; break; }
			case C_GetCurveValue: {
				const int c = CurveMap[(int)R[a[0]]];
				R[a[1]] = 1.0;
				R[a[2]] = Curves.Values[c];
				break;
			}
			case C_SetCurveValue: Curves.Values[CurveMap[(int)R[a[0]]]] = (float)R[a[1]]; break;
			case C_Mul: R[a[2]] = R[a[0]] * R[a[1]]; break;
			case C_Add: R[a[2]] = R[a[0]] + R[a[1]]; break;
			case C_Sub: R[a[2]] = R[a[0]] - R[a[1]]; break;
			case C_Abs: R[a[1]] = std::abs(R[a[0]]); break;
			case C_FloatToInt: R[a[1]] = std::floor(R[a[0]]); break;
			case C_Remap: {
				const double Value = R[a[0]], SMin = R[a[1]], SMax = R[a[2]], TMin = R[a[3]], TMax = R[a[4]];
				double Ratio = 0;
				if (!ue::IsNearlyEqual(SMin, SMax)) Ratio = (Value - SMin) / (SMax - SMin);
				if (R[a[5]] != 0) Ratio = Clamp(Ratio, 0.0, 1.0);
				R[a[6]] = ue::Lerp(TMin, TMax, Ratio);
				break;
			}
			case C_VectorAdd: PutV(a[2], V(a[0]) + V(a[1])); break;
			case C_VectorSub: PutV(a[2], V(a[0]) - V(a[1])); break;
			case C_VectorMul: PutV(a[2], V(a[0]) * V(a[1])); break;
			case C_QuatSwingTwist: {
				const Vec3 Axis = V(a[1]);
				Quat Sw, Tw;
				if (Axis.IsNearlyZero()) Sw = Tw = Quat();
				else Q(a[0]).ToSwingTwist(Axis.GetSafeNormal(), Sw, Tw);
				PutQ(a[2], Sw);
				PutQ(a[3], Tw);
				break;
			}
			case C_QuatToRotator: { double p, y, r; Q(a[0]).ToRotator(p, y, r); R[a[1]] = p; R[a[1] + 1] = y; R[a[1] + 2] = r; break; }
			case C_QuatFromRotator: PutQ(a[1], Quat::MakeFromEulerPYR(R[a[0]], R[a[0] + 1], R[a[0] + 2])); break;
			case C_QuatInverse: PutQ(a[1], Q(a[0]).Inverse()); break;
			case C_QuatMul: PutQ(a[2], Q(a[0]) * Q(a[1])); break;
			case C_QuatSlerp: PutQ(a[3], Quat::Slerp(Q(a[0]), Q(a[1]), R[a[2]])); break;
			case C_TransformMakeRelative: { Xform L = X(a[0]).GetRelativeTransform(X(a[1])); L.NormalizeRotation(); PutX(a[2], L); break; }
			case C_TransformMakeAbsolute: { Xform G = X(a[0]) * X(a[1]); G.NormalizeRotation(); PutX(a[2], G); break; }
			case C_TransformMul: PutX(a[2], X(a[0]) * X(a[1])); break;
			case C_TransformLerp: PutX(a[3], LerpXf(X(a[0]), X(a[1]), R[a[2]])); break;
			case C_AlphaInterp: {
				// FInputScaleBiasClamp::ApplyTo
				double Res = R[a[0]];
				if (R[a[3]] != 0) {
					const double InMin = R[a[4]], InMax = R[a[4] + 1], OutMin = R[a[5]], OutMax = R[a[5] + 1];
					const double Div = InMax - InMin;
					const double Pct = ue::IsNearlyZero(Div) ? (Res >= InMax ? 1.0 : 0.0) : (Res - InMin) / Div;
					Res = ue::Lerp(OutMin, OutMax, Pct);
				}
				Res = Res * R[a[1]] + R[a[2]];
				if (R[a[6]] != 0) Res = Clamp(Res, R[a[7]], R[a[8]]);
				if (R[a[9]] != 0) {
					if (o.bInit) Res = ue::FInterpTo(o.Interp, Res, Dt, Res >= o.Interp ? R[a[10]] : R[a[11]]);
					o.Interp = Res;
				}
				o.bInit = true;
				R[a[12]] = Res;
				break;
			}
			case C_SpringInterpVector: {
				// FRigUnit_SpringInterpVectorV2 + UKismetMathLibrary::VectorSpringInterp (mass 1)
				const bool bUseCurrent = R[a[0]] != 0;
				const double Stiffness = ue::Square(R[a[3]] * 2.0 * ue::PI);
				const double Damping = R[a[4]];
				const Vec3 Force = V(a[5]);
				Vec3 Target = V(a[2]);
				if (!ue::IsNearlyZero(Stiffness)) Target += Force / Stiffness;
				else o.Velocity += Force * Dt;
				Vec3 Cur = bUseCurrent ? V(a[1]) : o.SimResult;
				const bool bInitFromTarget = !bUseCurrent || R[a[7]] != 0;
				if (bInitFromTarget && !o.bPrevTargetValid) Cur = Target;
				if (Dt > ue::SMALL_NUMBER) {
					const Vec3 TargetVel = o.bPrevTargetValid ? (Target - o.PrevTarget) * (R[a[6]] / Dt) : Vec3();
					const double Freq = std::sqrt(Stiffness) / (2.0 * ue::PI);
					SpringDamper1(Cur.X, o.Velocity.X, Target.X, TargetVel.X, Dt, Freq, Damping);
					SpringDamper1(Cur.Y, o.Velocity.Y, Target.Y, TargetVel.Y, Dt, Freq, Damping);
					SpringDamper1(Cur.Z, o.Velocity.Z, Target.Z, TargetVel.Z, Dt, Freq, Damping);
					o.PrevTarget = Target;
					o.bPrevTargetValid = true;
				}
				o.SimResult = Cur;
				PutV(a[8], Cur);
				PutV(a[9], o.Velocity);
				break;
			}
			}
		}
	}
};

} // namespace gasp

namespace gasp {

// =========================================================================================================== RigidBody
// FAnimNode_RigidBody on the physics asset bodies, solved with small-step XPBD: locked / limited linear joints, cone + twist
// limits (soft limits as acceleration springs), SLERP / twist-swing angular drives, capsule contacts against the other bodies.
struct PPRigidBody : PPNode {
	struct Shape { Vec3 C; Vec3 Axis; double R = 1, HL = 0; };
	struct Body {
		int Bone = -1;
		bool bSim = false, bGravity = true, bCollide = true;
		double InvMass = 0;
		M3 InvInertiaLocal;
		Vec3 Com;
		double LinDamp = 0.01, AngDamp = 0;
		std::vector<Shape> Shapes;
		Vec3 X, XPrev, V, W, KX0, KX1;
		Quat Q, QPrev, KQ0, KQ1;
		M3 InvI;  // world
	};
	enum Motion { Free, Limited, Locked };
	struct Joint {
		int B1 = -1, B2 = -1;  // child, parent body
		Vec3 P1, P2;
		Quat R1, R2;
		Motion Lin[3] = { Locked, Locked, Locked };
		double LinLimit = 0;
		Motion Swing1 = Free, Swing2 = Free, Twist = Free;
		double Swing1Limit = 0, Swing2Limit = 0, TwistLimit = 0;
		bool bConeSoft = true, bTwistSoft = true;
		double ConeK = 50, ConeD = 5, TwistK = 50, TwistD = 5;
		bool bDrive = false;
		double DriveK = 0, DriveD = 0;
		bool bDrivePos = false;
		Quat DriveTarget;
	};
	std::vector<Body> Bodies;
	std::vector<Joint> Joints;
	std::vector<std::pair<int, int>> Pairs;
	Vec3 Gravity = Vec3(0, 0, -980);
	Vec3 AccScale, VelScale, AccClamp = Vec3(10000, 10000, 10000);
	int BaseBone = -1;
	bool bComponentSpace = false;
	bool bInitialized = false;
	int DebugFlags = 0;  // 1 no contacts, 2 no drives, 4 no soft limits, 8 no hard limits, 16 no external acceleration
	Vec3 PrevCompVel;
	bool bHasPrevCompVel = false;

	void Reset() override { bInitialized = false; bHasPrevCompVel = false; }

	static double Volume(const Shape& s) { return ue::PI * s.R * s.R * (2 * s.HL) + 4.0 / 3.0 * ue::PI * s.R * s.R * s.R; }

	void UpdateWorldInertia(Body& b) {
		if (!b.bSim) { b.InvI = M3(); return; }
		const M3 Rm = M3::FromQuat(b.Q);
		b.InvI = Rm * b.InvInertiaLocal * Rm.T();
	}
	Vec3 Anchor(const Body& b, const Vec3& LocalP) const { return b.X + b.Q.RotateVector(LocalP - b.Com); }

	Xform SimSpace(PPPose& Pose) {
		if (bComponentSpace || BaseBone < 0) return Xform();
		return Pose.GetCS(BaseBone);
	}

	void ApplyPositional(Body& A, Body* Bp, const Vec3& PA, const Vec3& PB, const Vec3& Corr, double Compliance, double H) {
		const double C = Corr.Size();
		if (C < 1e-9) return;
		const Vec3 N = Corr / C;
		const Vec3 RA = PA - A.X;
		const Vec3 RAN = RA ^ N;
		double WA = A.InvMass + (RAN | (A.InvI * RAN));
		double WB = 0;
		Vec3 RB, RBN;
		if (Bp && Bp->bSim) { RB = PB - Bp->X; RBN = RB ^ N; WB = Bp->InvMass + (RBN | (Bp->InvI * RBN)); }
		const double Alpha = Compliance / (H * H);
		const double WSum = WA + WB + Alpha;
		if (WSum < 1e-12) return;
		const Vec3 P = N * (C / WSum);
		if (A.bSim) {
			A.X += P * A.InvMass;
			const Vec3 dW = A.InvI * (RA ^ P);
			A.Q = (A.Q + Quat(dW.X, dW.Y, dW.Z, 0) * A.Q * 0.5).GetNormalized();
		}
		if (Bp && Bp->bSim) {
			Bp->X -= P * Bp->InvMass;
			const Vec3 dW = Bp->InvI * (RB ^ P);
			Bp->Q = (Bp->Q - Quat(dW.X, dW.Y, dW.Z, 0) * Bp->Q * 0.5).GetNormalized();
		}
	}
	void ApplyAngular(Body& A, Body* Bp, const Vec3& DTheta) {
		const double Th = DTheta.Size();
		if (Th < 1e-9) return;
		const Vec3 N = DTheta / Th;
		const double WA = A.bSim ? (N | (A.InvI * N)) : 0.0;
		const double WB = (Bp && Bp->bSim) ? (N | (Bp->InvI * N)) : 0.0;
		if (WA + WB < 1e-12) return;
		const double L = Th / (WA + WB);
		if (A.bSim) { const Vec3 d = A.InvI * N * L; A.Q = (A.Q + Quat(d.X, d.Y, d.Z, 0) * A.Q * 0.5).GetNormalized(); }
		if (Bp && Bp->bSim) { const Vec3 d = Bp->InvI * N * L; Bp->Q = (Bp->Q - Quat(d.X, d.Y, d.Z, 0) * Bp->Q * 0.5).GetNormalized(); }
	}
	// relative angular acceleration (acceleration-mode drive / soft limit) split by the bodies' inverse inertia
	void ApplyAngularAccel(Body& A, Body* Bp, const Vec3& Acc, double H) {
		const double Len = Acc.Size();
		if (Len < 1e-12) return;
		const Vec3 N = Acc / Len;
		const double WA = A.bSim ? (N | (A.InvI * N)) : 0.0;
		const double WB = (Bp && Bp->bSim) ? (N | (Bp->InvI * N)) : 0.0;
		if (WA + WB < 1e-12) return;
		if (A.bSim) A.W += Acc * (H * WA / (WA + WB));
		if (Bp && Bp->bSim) Bp->W -= Acc * (H * WB / (WA + WB));
	}

	static void ClosestSegSeg(const Vec3& P1, const Vec3& Q1, const Vec3& P2, const Vec3& Q2, Vec3& C1, Vec3& C2) {
		const Vec3 D1 = Q1 - P1, D2 = Q2 - P2, R = P1 - P2;
		const double A = D1 | D1, E = D2 | D2, Fv = D2 | R;
		double S = 0, T = 0;
		if (A <= 1e-9 && E <= 1e-9) { C1 = P1; C2 = P2; return; }
		if (A <= 1e-9) { S = 0; T = Clamp(Fv / E, 0.0, 1.0); }
		else {
			const double Cc = D1 | R;
			if (E <= 1e-9) { T = 0; S = Clamp(-Cc / A, 0.0, 1.0); }
			else {
				const double Bb = D1 | D2, Den = A * E - Bb * Bb;
				S = Den != 0 ? Clamp((Bb * Fv - Cc * E) / Den, 0.0, 1.0) : 0.0;
				T = (Bb * S + Fv) / E;
				if (T < 0) { T = 0; S = Clamp(-Cc / A, 0.0, 1.0); }
				else if (T > 1) { T = 1; S = Clamp((Bb - Cc) / A, 0.0, 1.0); }
			}
		}
		C1 = P1 + D1 * S; C2 = P2 + D2 * T;
	}
	void SegOf(const Body& b, const Shape& s, Vec3& P, Vec3& Q) const {
		const Vec3 C = Anchor(b, s.C);
		const Vec3 Ax = b.Q.RotateVector(s.Axis) * s.HL;
		P = C - Ax; Q = C + Ax;
	}
	bool Overlap(const Body& a, const Body& b, double Margin) const {
		for (auto& sa : a.Shapes) for (auto& sb : b.Shapes) {
			Vec3 P1, Q1, P2, Q2, C1, C2;
			SegOf(a, sa, P1, Q1); SegOf(b, sb, P2, Q2);
			ClosestSegSeg(P1, Q1, P2, Q2, C1, C2);
			if ((C1 - C2).Size() < sa.R + sb.R + Margin) return true;
		}
		return false;
	}

	void SetFromPose(Body& b, const Xform& Sim) { b.Q = Sim.Rotation; b.X = Sim.TransformPositionNoScale(b.Com); }

	void Evaluate(PPPose& Pose, PPCurves&, const PPFrame& Frame) override {
		const Xform SpaceTM = SimSpace(Pose);
		auto ToSim = [&](int Bone) { return bComponentSpace ? Pose.GetCS(Bone) : Pose.GetCS(Bone).GetRelativeTransform(SpaceTM); };
		if (!bInitialized || Frame.bTeleport) {
			for (auto& b : Bodies) {
				SetFromPose(b, ToSim(b.Bone));
				b.V = b.W = Vec3();
				b.KX0 = b.KX1 = b.X; b.KQ0 = b.KQ1 = b.Q;
			}
			// pairs: simulated vs any other colliding body, minus jointed pairs and pairs overlapping in the reference pose
			if (!bInitialized) {
				std::vector<Body> Ref = Bodies;
				for (auto& b : Ref) { SetFromPose(b, Pose.RefCS[b.Bone]); }
				Pairs.clear();
				for (int i = 0; i < (int)Bodies.size(); ++i) for (int j = i + 1; j < (int)Bodies.size(); ++j) {
					if (!Bodies[i].bSim && !Bodies[j].bSim) continue;
					if (!Bodies[i].bCollide || !Bodies[j].bCollide) continue;
					bool bJoint = false;
					for (auto& jt : Joints) if ((jt.B1 == i && jt.B2 == j) || (jt.B1 == j && jt.B2 == i)) bJoint = true;
					if (bJoint || Overlap(Ref[i], Ref[j], 0.5)) continue;
					Pairs.push_back(Bodies[i].bSim ? std::make_pair(i, j) : std::make_pair(j, i));
				}
			}
			bInitialized = true;
			bHasPrevCompVel = false;
			Output(Pose, SpaceTM);
			return;
		}
		const double Dt = Frame.DeltaTime;
		if (Dt <= 1e-5) { Output(Pose, SpaceTM); return; }
		// kinematic targets
		for (auto& b : Bodies) {
			b.KX0 = b.bSim ? b.X : b.KX1; b.KQ0 = b.bSim ? b.Q : b.KQ1;
			Body Tmp = b; SetFromPose(Tmp, ToSim(b.Bone));
			b.KX1 = Tmp.X; b.KQ1 = Tmp.Q;
		}
		// component linear velocity / acceleration as forces (FAnimNode_RigidBody, non world space simulation)
		Vec3 ExtAcc;
		{
			const Vec3 CompVel = (Frame.ComponentWorld.Translation - FramePrevLocation) / Dt;
			const Vec3 Acc = bHasPrevCompVel ? (CompVel - PrevCompVel) / Dt : Vec3();
			PrevCompVel = CompVel;
			bHasPrevCompVel = true;
			auto ToSimVec = [&](const Vec3& V) {
				const Vec3 C = Frame.ComponentWorld.InverseTransformVectorNoScale(V);
				return bComponentSpace ? C : SpaceTM.InverseTransformVectorNoScale(C);
			};
			ExtAcc = (DebugFlags & 16) ? Vec3() : ToSimVec(-CompVel) * VelScale + ToSimVec(-Acc) * AccScale;
			if (!(AccClamp.X == 0 && AccClamp.Y == 0 && AccClamp.Z == 0))
				ExtAcc = Vec3(Clamp(ExtAcc.X, -AccClamp.X, AccClamp.X), Clamp(ExtAcc.Y, -AccClamp.Y, AccClamp.Y), Clamp(ExtAcc.Z, -AccClamp.Z, AccClamp.Z));
		}
		const Vec3 G = bComponentSpace ? Frame.ComponentWorld.InverseTransformVectorNoScale(Gravity)
		                               : SpaceTM.InverseTransformVectorNoScale(Frame.ComponentWorld.InverseTransformVectorNoScale(Gravity));
		const int Sub = Clamp((int)std::ceil(Dt * 240.0), 2, 16);
		const double H = Dt / Sub;
		for (int s = 0; s < Sub; ++s) {
			const double A = double(s + 1) / Sub;
			for (auto& b : Bodies) {
				if (!b.bSim) {
					b.XPrev = b.X; b.QPrev = b.Q;
					b.X = Vec3::Lerp(b.KX0, b.KX1, A);
					b.Q = Quat::Slerp(b.KQ0, b.KQ1, A);
				}
				UpdateWorldInertia(b);
			}
			// velocities: gravity, external acceleration, damping, drives, soft limits
			for (auto& b : Bodies) {
				if (!b.bSim) continue;
				b.V += ((b.bGravity ? G : Vec3()) + ExtAcc) * H;
				b.V *= 1.0 / (1.0 + H * b.LinDamp);
				b.W *= 1.0 / (1.0 + H * b.AngDamp);
			}
			for (auto& j : Joints) {
				Body& C = Bodies[j.B1];
				Body* P = j.B2 >= 0 ? &Bodies[j.B2] : nullptr;
				if (!C.bSim) continue;
				const Quat F1 = C.Q * j.R1;
				const Quat F2 = (P ? P->Q : Quat()) * j.R2;
				const Vec3 RelW = C.W - (P && P->bSim ? P->W : Vec3());
				if (j.bDrive && !(DebugFlags & 2)) {
					Vec3 Acc = RelW * -j.DriveD;
					if (j.bDrivePos) Acc += RotationVector((F2 * j.DriveTarget) * F1.Inverse()) * j.DriveK;
					ApplyAngularAccel(C, P, Acc, H);
				}
				// soft swing / twist limits
				const Quat Rel = F2.Inverse() * F1;
				Quat Sw, Tw;
				Rel.ToSwingTwist(Vec3(1, 0, 0), Sw, Tw);
				if (j.Twist == Limited && j.bTwistSoft && !(DebugFlags & 4)) {
					const double T = Tw.GetTwistAngle(Vec3(1, 0, 0));
					const double Ex = std::abs(T) - j.TwistLimit;
					if (Ex > 0) {
						const Vec3 Ax = F2.RotateVector(Vec3(1, 0, 0)) * (T > 0 ? -1.0 : 1.0);
						ApplyAngularAccel(C, P, Ax * (j.TwistK * Ex) - Ax * ((RelW | Ax) * j.TwistD), H);
					}
				}
				if ((j.Swing1 == Limited || j.Swing2 == Limited) && j.bConeSoft && !(DebugFlags & 4)) {
					const Vec3 SV = RotationVector(Sw);  // in parent frame, x ~ 0
					const double L1 = j.Swing1 == Limited ? j.Swing1Limit : 1e9, L2 = j.Swing2 == Limited ? j.Swing2Limit : 1e9;
					const double E = std::sqrt(ue::Square(SV.Z / std::max(L1, 1e-4)) + ue::Square(SV.Y / std::max(L2, 1e-4)));
					if (E > 1.0) {
						const Vec3 Clamped = Vec3(0, SV.Y / E, SV.Z / E);
						const Vec3 Ex = F2.RotateVector(Clamped - SV);
						const double ExL = Ex.Size();
						if (ExL > 1e-9) {
							const Vec3 N = Ex / ExL;
							ApplyAngularAccel(C, P, N * (j.ConeK * ExL) - N * ((RelW | N) * j.ConeD), H);
						}
					}
				}
			}
			for (auto& b : Bodies) {
				if (!b.bSim) continue;
				const double VL = b.V.Size(); if (VL > 10000) b.V *= 10000 / VL;
				const double WL = b.W.Size(); if (WL > 60) b.W *= 60 / WL;
				b.XPrev = b.X; b.QPrev = b.Q;
				b.X += b.V * H;
				b.Q = (b.Q + Quat(b.W.X, b.W.Y, b.W.Z, 0) * b.Q * (0.5 * H)).GetNormalized();
				UpdateWorldInertia(b);
			}
			for (int it = 0; it < 2; ++it) {
				for (auto& j : Joints) {
					Body& C = Bodies[j.B1];
					Body* P = j.B2 >= 0 ? &Bodies[j.B2] : nullptr;
					if (!C.bSim && !(P && P->bSim)) continue;
					// linear
					const Vec3 A1 = Anchor(C, j.P1);
					const Vec3 A2 = P ? Anchor(*P, j.P2) : j.P2;
					const Quat F2 = (P ? P->Q : Quat()) * j.R2;
					Vec3 D = F2.UnrotateVector(A1 - A2);
					Vec3 Tgt = D;
					double* Comp[3] = { &Tgt.X, &Tgt.Y, &Tgt.Z };
					double LimSq = 0;
					for (int k = 0; k < 3; ++k) {
						if (j.Lin[k] == Locked) *Comp[k] = 0;
						else if (j.Lin[k] == Limited) LimSq += *Comp[k] * *Comp[k];
					}
					if (LimSq > j.LinLimit * j.LinLimit) {
						const double Sc = j.LinLimit / std::sqrt(LimSq);
						for (int k = 0; k < 3; ++k) if (j.Lin[k] == Limited) *Comp[k] *= Sc;
					}
					const Vec3 Corr = F2.RotateVector(Tgt - D);
					if (C.bSim) ApplyPositional(C, P, A1, A2, Corr, 0, H);
					else if (P) ApplyPositional(*P, &C, A2, A1, -Corr, 0, H);
					// hard angular limits
					const bool bHardTwist = j.Twist == Locked || (j.Twist == Limited && !j.bTwistSoft);
					const bool bHardSwing = j.Swing1 == Locked || j.Swing2 == Locked || ((j.Swing1 == Limited || j.Swing2 == Limited) && !j.bConeSoft);
					if ((bHardTwist || bHardSwing) && !(DebugFlags & 8)) {
						const Quat F1 = C.Q * j.R1;
						const Quat F2b = (P ? P->Q : Quat()) * j.R2;
						const Quat Rel = F2b.Inverse() * F1;
						Quat Sw, Tw;
						Rel.ToSwingTwist(Vec3(1, 0, 0), Sw, Tw);
						double T = Tw.GetTwistAngle(Vec3(1, 0, 0));
						if (bHardTwist) T = j.Twist == Locked ? 0.0 : Clamp(T, -j.TwistLimit, j.TwistLimit);
						Vec3 SV = RotationVector(Sw);
						if (bHardSwing) {
							if (j.Swing1 == Locked) SV.Z = 0;
							if (j.Swing2 == Locked) SV.Y = 0;
							if (!j.bConeSoft) {
								const double L1 = j.Swing1 == Limited ? j.Swing1Limit : 1e9, L2 = j.Swing2 == Limited ? j.Swing2Limit : 1e9;
								const double E = std::sqrt(ue::Square(SV.Z / std::max(L1, 1e-4)) + ue::Square(SV.Y / std::max(L2, 1e-4)));
								if (E > 1) { SV.Y /= E; SV.Z /= E; }
							}
							SV.X = 0;
						}
						const Quat NewRel = Quat::MakeFromRotationVector(SV) * Quat(Vec3(1, 0, 0), T);
						const Vec3 DTheta = RotationVector((F2b * NewRel) * F1.Inverse());
						if (C.bSim) ApplyAngular(C, P, DTheta);
						else if (P) ApplyAngular(*P, &C, -DTheta);
					}
				}
				// contacts
				if (!(DebugFlags & 1)) for (auto& pr : Pairs) {
					Body& A = Bodies[pr.first];
					Body& Bb = Bodies[pr.second];
					for (auto& sa : A.Shapes) for (auto& sb : Bb.Shapes) {
						Vec3 P1, Q1, P2, Q2, C1, C2;
						SegOf(A, sa, P1, Q1); SegOf(Bb, sb, P2, Q2);
						ClosestSegSeg(P1, Q1, P2, Q2, C1, C2);
						Vec3 D = C1 - C2;
						const double Dist = D.Size();
						const double Pen = sa.R + sb.R - Dist;
						if (Pen <= 0) continue;
						const Vec3 N = Dist > 1e-6 ? D / Dist : Vec3(0, 0, 1);
						ApplyPositional(A, &Bb, C1 - N * sa.R, C2 + N * sb.R, N * Pen, 0, H);
					}
				}
			}
			for (auto& b : Bodies) {
				if (!b.bSim) continue;
				b.V = (b.X - b.XPrev) / H;
				Quat dq = b.Q * b.QPrev.Inverse();
				if (dq.W < 0) dq = dq * -1.0;
				b.W = Vec3(dq.X, dq.Y, dq.Z) * (2.0 / H);
			}
		}
		Output(Pose, SpaceTM);
	}
	Vec3 FramePrevLocation;

	void Output(PPPose& Pose, const Xform& SpaceTM) {
		std::vector<std::pair<int, Xform>> Out;
		for (auto& b : Bodies) {
			if (!b.bSim) continue;
			const Xform Sim(b.Q, b.X - b.Q.RotateVector(b.Com));
			Out.push_back({ b.Bone, bComponentSpace ? Sim : Sim * SpaceTM });
		}
		std::sort(Out.begin(), Out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		Pose.SetCSBatch(Out);
	}
	void DebugJoints(godot::Array& Out) const {
		for (auto& j : Joints) {
			const Body& C = Bodies[j.B1];
			const Body* P = j.B2 >= 0 ? &Bodies[j.B2] : nullptr;
			const Quat F1 = C.Q * j.R1, F2 = (P ? P->Q : Quat()) * j.R2;
			const Quat Rel = F2.Inverse() * F1;
			Quat Sw, Tw; Rel.ToSwingTwist(Vec3(1, 0, 0), Sw, Tw);
			const Vec3 SV = RotationVector(Sw);
			const Vec3 DE = RotationVector((F2 * j.DriveTarget) * F1.Inverse());
			const Vec3 Gap = Anchor(C, j.P1) - (P ? Anchor(*P, j.P2) : j.P2);
			godot::Dictionary D;
			D["child"] = C.Bone; D["parent"] = P ? P->Bone : -1;
			D["swing_deg"] = godot::Vector3(ue::RadToDeg(SV.X), ue::RadToDeg(SV.Y), ue::RadToDeg(SV.Z));
			D["twist_deg"] = ue::RadToDeg(Tw.GetTwistAngle(Vec3(1, 0, 0)));
			D["drive_err_deg"] = ue::RadToDeg(DE.Size());
			D["gap"] = Gap.Size();
			D["w"] = C.W.Size();
			D["sim"] = C.bSim;
			Out.push_back(D);
		}
	}
	void DebugBodies(std::vector<std::pair<Xform, Vec3>>& Out) const override {
		for (auto& b : Bodies) for (auto& s : b.Shapes) {
			Xform X(b.Q * Quat::FindBetweenNormals(Vec3(0, 0, 1), s.Axis.GetSafeNormal()), b.X + b.Q.RotateVector(s.C - b.Com));
			Out.push_back({ X, Vec3(s.R, s.HL, b.bSim ? 1 : 0) });
		}
	}
};

} // namespace gasp

namespace godot {

void GaspPostProcess::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "program", "skeleton"), &GaspPostProcess::setup);
	ClassDB::bind_method(D_METHOD("update", "skeleton", "delta", "component_location", "component_yaw"), &GaspPostProcess::update);
	ClassDB::bind_method(D_METHOD("get_curve_names"), &GaspPostProcess::get_curve_names);
	ClassDB::bind_method(D_METHOD("get_curve_values"), &GaspPostProcess::get_curve_values);
	ClassDB::bind_method(D_METHOD("reset"), &GaspPostProcess::reset);
	ClassDB::bind_method(D_METHOD("get_debug_bodies"), &GaspPostProcess::get_debug_bodies);
	ClassDB::bind_method(D_METHOD("set_physics_debug_flags", "flags"), &GaspPostProcess::set_physics_debug_flags);
	ClassDB::bind_method(D_METHOD("get_debug_joints"), &GaspPostProcess::get_debug_joints);
}

static Xform FromGodot(const Vector3& p, const Quaternion& q, const Vector3& s) {
	return Xform(Quat(-q.x, q.y, -q.z, q.w).GetNormalized(), Vec3(p.x, -p.y, p.z) * 100.0, Vec3(s.x, s.y, s.z));
}

static gasp::PPRigidBody::Motion ParseMotion(const std::string& s) {
	if (s.find("Locked") != std::string::npos) return gasp::PPRigidBody::Locked;
	if (s.find("Limited") != std::string::npos) return gasp::PPRigidBody::Limited;
	return gasp::PPRigidBody::Free;
}

bool GaspPostProcess::setup(const Dictionary& program, Skeleton3D* skeleton) {
	bReady = false;
	Nodes.clear();
	Curves = PPCurves();
	if (!skeleton) return false;
	const int N = skeleton->get_bone_count();
	Pose = PPPose();
	for (int b = 0; b < N; ++b) {
		Pose.Names.push_back(S(skeleton->get_bone_name(b)));
		Pose.Parents.push_back(skeleton->get_bone_parent(b));
		const Transform3D R = skeleton->get_bone_rest(b);
		Pose.RefLocal.push_back(FromGodot(R.origin, R.basis.get_rotation_quaternion(), R.basis.get_scale()));
		Pose.Index[Pose.Names.back()] = b;
	}
	for (int b = 0; b < N; ++b) if (Pose.Parents[b] >= b) { UtilityFunctions::push_error("GaspPostProcess: skeleton is not parent-before-child ordered"); return false; }
	Pose.RefCS.resize(N);
	for (int b = 0; b < N; ++b) Pose.RefCS[b] = Pose.Parents[b] < 0 ? Pose.RefLocal[b] : Pose.RefLocal[b] * Pose.RefCS[Pose.Parents[b]];
	Pose.Local = Pose.RefLocal;
	Pose.CS = Pose.RefCS;
	Pose.bCSDirty = false;

	Array nodes = program.get("nodes", Array());
	for (int i = 0; i < nodes.size(); ++i) {
		Dictionary d = nodes[i];
		const std::string type = Str(d, "type", "");
		if (type == "modify_curve") {
			auto n = std::make_unique<PPModifyCurve>();
			Array names = d.get("names", Array()), vals = d.get("values", Array());
			for (int k = 0; k < names.size(); ++k) n->Values.push_back({ Curves.Add(S(names[k])), k < vals.size() ? (float)(double)vals[k] : 0.f });
			Nodes.push_back(std::move(n));
		} else if (type == "pose_driver") {
			auto n = std::make_unique<PPPoseDriver>();
			Array sb = d.get("source_bones", Array());
			for (int k = 0; k < sb.size(); ++k) n->SourceBones.push_back(Pose.Find(S(sb[k])));
			const Variant esb = d.get("eval_space_bone", Variant());
			if (esb.get_type() == Variant::STRING) n->EvalSpaceBone = Pose.Find(S(esb));
			n->bEvalFromRefPose = B(d, "eval_from_ref_pose", false);
			n->bTranslation = Str(d, "drive_source", "Rotation") == "Translation";
			n->bDriveCurves = Str(d, "drive_output", "DrivePoses") == "DriveCurves";
			n->bInterpolative = Str(d, "solver", "Additive") == "Interpolative";
			n->Radius = F(d, "radius", 1.0);
			n->RadiusAuto = B(d, "automatic_radius", false);
			n->Function = ParseFunc(Str(d, "function", "Gaussian"));
			n->Distance = ParseDist(Str(d, "distance", "SwingAngle"));
			if (n->Function == RBFFunc::Default) n->Function = RBFFunc::Linear;
			if (n->Distance == RBFDist::Default) n->Distance = RBFDist::SwingAngle;
			const std::string ta = Str(d, "twist_axis", "BA_X");
			n->TwistAxis = ta == "BA_Y" ? Vec3(0, 1, 0) : (ta == "BA_Z" ? Vec3(0, 0, 1) : Vec3(1, 0, 0));
			n->WeightThreshold = F(d, "weight_threshold", 1e-4);
			const std::string nm = Str(d, "normalize", "OnlyNormalizeAboveOne");
			n->Normalize = nm == "AlwaysNormalize" ? RBFNorm::Always : (nm == "NormalizeWithinMedian" ? RBFNorm::WithinMedian : RBFNorm::OnlyAboveOne);
			n->MedianReference = V3(d.get("median_reference", Variant()));
			n->MedianMin = F(d, "median_min", 45.0);
			n->MedianMax = F(d, "median_max", 60.0);
			if (d.has("pose_asset")) {
				Dictionary pa = d["pose_asset"];
				n->bHasPoseAsset = true;
				n->PA.bAdditive = B(pa, "additive", false);
				n->PA.BasePoseIndex = (int)pa.get("base_pose_index", -1);
				Array pn = pa.get("pose_names", Array());
				for (int k = 0; k < pn.size(); ++k) n->PA.PoseNames.push_back(S(pn[k]));
				Array tr = pa.get("tracks", Array());
				for (int k = 0; k < tr.size(); ++k) n->PA.TrackBone.push_back(Pose.Find(S(tr[k])));
				Array ps = pa.get("poses", Array());
				for (int k = 0; k < ps.size(); ++k) {
					Array pl = ps[k];
					std::vector<Xform> L;
					for (int m = 0; m < pl.size(); ++m) L.push_back(XF(pl[m]));
					n->PA.Poses.push_back(L);
				}
				Array inf = pa.get("influences", Array());
				for (int k = 0; k < inf.size(); ++k) {
					Array il = inf[k];
					std::vector<std::pair<int, int>> L;
					for (int m = 0; m < il.size(); ++m) {
						Array e = il[m];
						const int P = (int)e[0], BI = (int)e[1];
						if (P >= 0 && P < (int)n->PA.Poses.size() && BI >= 0 && BI < (int)n->PA.Poses[P].size()) L.push_back({ P, BI });
					}
					n->PA.Influences.push_back(L);
				}
				n->PA.Influences.resize(n->PA.TrackBone.size());
				n->Filtered.assign(N, 0);
				n->IsTrack.assign(N, 0);
				for (int b : n->PA.TrackBone) if (b >= 0) n->IsTrack[b] = 1;
				for (int s : n->SourceBones) if (s >= 0) n->Filtered[s] = 1;
				Array odb = d.get("only_drive_bones", Array());
				if (odb.size() > 0) {
					std::vector<char> Keep(N, 0);
					for (int k = 0; k < odb.size(); ++k) { const int b = Pose.Find(S(odb[k])); if (b >= 0) Keep[b] = 1; }
					for (int b : n->PA.TrackBone) if (b >= 0 && !Keep[b]) n->Filtered[b] = 1;
				}
			}
			Array tg = d.get("targets", Array());
			for (int k = 0; k < tg.size(); ++k) {
				Dictionary t = tg[k];
				PPPoseDriver::Target T;
				Array rots = t.get("rot", Array()), trans = t.get("trans", Array());
				for (size_t s = 0; s < n->SourceBones.size(); ++s) {
					const int sbone = n->SourceBones[s];
					const Xform Ref = (n->bEvalFromRefPose && sbone >= 0) ? Pose.RefLocal[sbone] : Xform();
					if ((int)s < rots.size()) {
						if (n->bTranslation) T.E.AddVector(n->bEvalFromRefPose ? Ref.Inverse().TransformPosition(V3(trans[s])) : V3(trans[s]));
						else {
							const Quat TR = RotPYR(rots[s]);
							T.E.AddRotator(n->bEvalFromRefPose ? Ref.Inverse().TransformRotation(TR) : TR);
						}
					} else {
						T.E.AddVector(Vec3());
					}
				}
				const std::string name = S(t.get("name", "None"));
				if (n->bDriveCurves) { if (name != "None") T.Curve = Curves.Add(name); }
				else if (n->bHasPoseAsset) {
					for (size_t p = 0; p < n->PA.PoseNames.size(); ++p) if (n->PA.PoseNames[p] == name) T.Pose = (int)p;
				}
				T.Scale = F(t, "scale", 1.0);
				T.Dist = ParseDist(Str(t, "distance", "DefaultMethod"));
				T.Fn = ParseFunc(Str(t, "function", "DefaultFunction"));
				n->Targets.push_back(T);
			}
			n->Init();
			Nodes.push_back(std::move(n));
		} else if (type == "constraint") {
			auto n = std::make_unique<PPConstraint>();
			n->Bone = Pose.Find(Str(d, "bone", ""));
			Array tg = d.get("targets", Array()), w = d.get("weights", Array());
			for (int k = 0; k < tg.size(); ++k) {
				Dictionary t = tg[k];
				PPConstraint::T T;
				T.Bone = Pose.Find(Str(t, "bone", ""));
				T.Weight = k < w.size() ? (double)w[k] : 1.0;
				T.bOffset = Str(t, "offset", "Offset_RefPose") != "None";
				if (Str(t, "transform_type", "Parent") != "Parent") UtilityFunctions::push_warning("GaspPostProcess: only Parent constraints are supported");
				if (T.Bone >= 0 && n->Bone >= 0) T.Offset = Pose.RefCS[n->Bone].GetRelativeTransform(Pose.RefCS[T.Bone]);
				n->Targets.push_back(T);
			}
			Nodes.push_back(std::move(n));
		} else if (type == "control_rig" && d.get("program", Variant()).get_type() == Variant::DICTIONARY) {
			Dictionary pr = d["program"];
			auto n = std::make_unique<PPRigProgram>();
			n->R0.assign((int)pr.get("slots", 0), 0.0);
			Array init = pr.get("init", Array());
			for (int k = 0; k < init.size(); ++k) { Array e = init[k]; n->R0[(int)e[0]] = (double)e[1]; }
			Array bones = pr.get("bones", Array());
			for (int k = 0; k < bones.size(); ++k) { Array e = bones[k]; n->R0[(int)e[0]] = Pose.Find(S(e[1])); }
			Array cv = pr.get("curves", Array());
			for (int k = 0; k < cv.size(); ++k) n->CurveMap.push_back(Curves.Add(S(cv[k])));
			Dictionary vars = pr.get("vars", Dictionary()), vv = d.get("var_values", Dictionary());
			Array vk = vv.keys();
			for (int k = 0; k < vk.size(); ++k) {
				if (!vars.has(vk[k])) continue;
				Array slot = vars[vk[k]];
				Array val = vv[vk[k]];
				for (int m = 0; m < val.size() && m < (int)slot[1]; ++m) n->R0[(int)slot[0] + m] = (double)val[m];
			}
			static const std::unordered_map<std::string, PPRigProgram::Code> Codes = {
				{ "copy", PPRigProgram::C_copy }, { "select", PPRigProgram::C_select }, { "jne", PPRigProgram::C_jne }, { "jmp", PPRigProgram::C_jmp },
				{ "ignored", PPRigProgram::C_ignored }, { "GetTransform", PPRigProgram::C_GetTransform }, { "SetTransform", PPRigProgram::C_SetTransform },
				{ "SetRotation", PPRigProgram::C_SetRotation }, { "SetTranslation", PPRigProgram::C_SetTranslation },
				{ "SetRelativeTransformForItem", PPRigProgram::C_SetRelativeTransformForItem },
				{ "ProjectTransformToNewParent", PPRigProgram::C_ProjectTransformToNewParent }, { "HierarchyGetParent", PPRigProgram::C_HierarchyGetParent },
				{ "GetCurveValue", PPRigProgram::C_GetCurveValue }, { "SetCurveValue", PPRigProgram::C_SetCurveValue },
				{ "MathFloatMul", PPRigProgram::C_Mul }, { "MathDoubleMul", PPRigProgram::C_Mul }, { "MathFloatAdd", PPRigProgram::C_Add },
				{ "MathDoubleAdd", PPRigProgram::C_Add }, { "MathFloatSub", PPRigProgram::C_Sub }, { "MathDoubleSub", PPRigProgram::C_Sub },
				{ "MathFloatAbs", PPRigProgram::C_Abs }, { "MathDoubleAbs", PPRigProgram::C_Abs }, { "MathFloatToInt", PPRigProgram::C_FloatToInt },
				{ "MathFloatRemap", PPRigProgram::C_Remap }, { "MathDoubleRemap", PPRigProgram::C_Remap },
				{ "MathVectorAdd", PPRigProgram::C_VectorAdd }, { "MathVectorSub", PPRigProgram::C_VectorSub }, { "MathVectorMul", PPRigProgram::C_VectorMul },
				{ "MathQuaternionSwingTwist", PPRigProgram::C_QuatSwingTwist }, { "MathQuaternionToRotator", PPRigProgram::C_QuatToRotator },
				{ "MathQuaternionFromRotator", PPRigProgram::C_QuatFromRotator }, { "MathQuaternionFromRotatorV2", PPRigProgram::C_QuatFromRotator },
				{ "MathQuaternionInverse", PPRigProgram::C_QuatInverse }, { "MathQuaternionMul", PPRigProgram::C_QuatMul },
				{ "MathQuaternionSlerp", PPRigProgram::C_QuatSlerp }, { "MathTransformMakeRelative", PPRigProgram::C_TransformMakeRelative },
				{ "MathTransformMakeAbsolute", PPRigProgram::C_TransformMakeAbsolute }, { "MathTransformMul", PPRigProgram::C_TransformMul },
				{ "MathTransformLerp", PPRigProgram::C_TransformLerp }, { "AlphaInterp", PPRigProgram::C_AlphaInterp },
				{ "SpringInterpVectorV2", PPRigProgram::C_SpringInterpVector },
			};
			Array ops = pr.get("ops", Array());
			bool bOk = true;
			for (int k = 0; k < ops.size(); ++k) {
				Dictionary o = ops[k];
				const std::string t = Str(o, "t", "");
				auto it = Codes.find(t);
				if (it == Codes.end()) { UtilityFunctions::push_error(String("GaspPostProcess: unknown rig op ") + String(t.c_str())); bOk = false; break; }
				PPRigProgram::Op Op;
				Op.C = it->second;
				Array a = o.get("a", Array());
				for (int m = 0; m < a.size(); ++m) Op.A.push_back((int)a[m]);
				Op.V = F(o, "v", 0.0);
				Op.To = (int)o.get("to", -1);
				Op.Unit = Str(o, "unit", "");
				n->Ops.push_back(Op);
			}
			if (bOk) { n->Reset(); Nodes.push_back(std::move(n)); }
		} else if (type == "control_rig") {
			auto n = std::make_unique<PPControlRig>();
			Array ops = d.get("ops", Array());
			if (d.get("ops", Variant()).get_type() != Variant::ARRAY) UtilityFunctions::push_warning(String("GaspPostProcess: control rig not ported: ") + String(d.get("rig", "")));
			for (int k = 0; k < ops.size(); ++k) {
				Dictionary o = ops[k];
				PPControlRig::Op Op;
				const std::string ot = Str(o, "op", "");
				if (ot == "twist") {
					Op.Type = 0;
					Op.Driver = Pose.Find(Str(o, "driver", ""));
					Op.Axis = V3(o.get("axis", Variant()), Vec3(1, 0, 0)).GetSafeNormal();
					Op.bInvert = B(o, "invert", false);
					Array tt = o.get("targets", Array());
					for (int m = 0; m < tt.size(); ++m) { Array e = tt[m]; Op.TwistTargets.push_back({ Pose.Find(S(e[0])), (double)e[1] }); }
				} else if (ot == "project") {
					Op.Type = 1;
					Op.BoneIdx = Pose.Find(Str(o, "bone", ""));
					Op.NewParent = Pose.Find(Str(o, "new_parent", ""));
					Op.OldParent = Pose.Find(Str(o, "old_parent", ""));
					Op.Weight = F(o, "weight", 1.0);
					Op.bPropagate = B(o, "propagate", true);
					if (o.has("slerp_with")) {
						Dictionary sw = o["slerp_with"];
						Op.bSlerp = true;
						Op.NewParentB = Pose.Find(Str(sw, "new_parent", ""));
						Op.OldParentB = Pose.Find(Str(sw, "old_parent", ""));
						Op.SlerpT = F(sw, "t", 0.5);
					}
				} else continue;
				n->Ops.push_back(Op);
			}
			Nodes.push_back(std::move(n));
		} else if (type == "rigid_body") {
			auto n = std::make_unique<PPRigidBody>();
			Dictionary pa = d["physics_asset"];
			n->Gravity = V3(d.get("gravity", Variant()), Vec3(0, 0, -980));
			n->AccScale = V3(d.get("acc_scale", Variant()));
			n->VelScale = V3(d.get("vel_scale", Variant()));
			n->AccClamp = V3(d.get("acc_clamp", Variant()), Vec3(10000, 10000, 10000));
			n->BaseBone = Pose.Find(Str(d, "base_bone", "root"));
			n->bComponentSpace = Str(d, "space", "ComponentSpace") == "ComponentSpace";
			Array bodies = pa.get("bodies", Array());
			std::unordered_map<std::string, int> BodyOf;
			for (int k = 0; k < bodies.size(); ++k) {
				Dictionary bd = bodies[k];
				PPRigidBody::Body Bd;
				Bd.Bone = Pose.Find(Str(bd, "bone", ""));
				if (Bd.Bone < 0) continue;
				Bd.bSim = B(bd, "simulated", true);
				Bd.bCollide = B(bd, "collision", true);
				Bd.bGravity = B(bd, "enable_gravity", true);
				Bd.LinDamp = F(bd, "linear_damping", 0.01);
				Bd.AngDamp = F(bd, "angular_damping", 0.0);
				double Vol = 0;
				Vec3 ComAcc;
				Array shapes = bd.get("shapes", Array());
				for (int m = 0; m < shapes.size(); ++m) {
					Dictionary sd = shapes[m];
					PPRigidBody::Shape Sh;
					Sh.C = V3(sd.get("center", Variant()));
					const Quat R = RotPYR(sd.get("rot", Variant()));
					const std::string st = Str(sd, "type", "capsule");
					if (st == "box") {
						double ex[3] = { F(sd, "x", 1) * 0.5, F(sd, "y", 1) * 0.5, F(sd, "z", 1) * 0.5 };
						const Vec3 Axes[3] = { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) };
						int L = 0; for (int a = 1; a < 3; ++a) if (ex[a] > ex[L]) L = a;
						double r = 0; for (int a = 0; a < 3; ++a) if (a != L) r = std::max(r, ex[a]);
						r = std::min(r, ex[L]);
						Sh.R = r; Sh.HL = std::max(ex[L] - r, 0.0); Sh.Axis = R.RotateVector(Axes[L]);
						Vol += 8 * ex[0] * ex[1] * ex[2];
						ComAcc += Sh.C * (8 * ex[0] * ex[1] * ex[2]);
					} else {
						Sh.R = F(sd, "radius", 1.0);
						Sh.HL = st == "sphere" ? 0.0 : F(sd, "length", 0.0) * 0.5;
						Sh.Axis = R.RotateVector(Vec3(0, 0, 1));
						const double V = PPRigidBody::Volume(Sh);
						Vol += V; ComAcc += Sh.C * V;
					}
					Bd.Shapes.push_back(Sh);
				}
				Bd.Com = (Vol > 0 ? ComAcc / Vol : Vec3()) + V3(bd.get("com_nudge", Variant()));
				double Mass = B(bd, "override_mass", false) ? F(bd, "mass", 100.0) : std::pow(std::max(Vol * 0.001, 0.0), 0.75) * F(bd, "mass_scale", 1.0);
				Mass = std::max(Mass, 0.001);
				if (Bd.bSim) {
					Bd.InvMass = 1.0 / Mass;
					// inertia: shapes as solid cylinders (capsule) / boxes about the body COM, scaled by InertiaTensorScale
					M3 I;
					for (auto& Sh : Bd.Shapes) {
						const double m = Mass * (Vol > 0 ? PPRigidBody::Volume(Sh) / Vol : 1.0 / Bd.Shapes.size());
						const double Ltot = 2 * Sh.HL + 2 * Sh.R;
						const double Ia = 0.5 * m * Sh.R * Sh.R, Ip = m * (3 * Sh.R * Sh.R + Ltot * Ltot) / 12.0;
						const Vec3 a = Sh.Axis.GetSafeNormal();
						M3 Is = M3::Diag(Ip, Ip, Ip);
						for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
							const double ar = r == 0 ? a.X : (r == 1 ? a.Y : a.Z), ac = c == 0 ? a.X : (c == 1 ? a.Y : a.Z);
							Is.m[r][c] += (Ia - Ip) * ar * ac;
						}
						const Vec3 dd = Sh.C - Bd.Com;
						const double d2 = dd | dd;
						for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
							const double dr = r == 0 ? dd.X : (r == 1 ? dd.Y : dd.Z), dc = c == 0 ? dd.X : (c == 1 ? dd.Y : dd.Z);
							Is.m[r][c] += m * ((r == c ? d2 : 0) - dr * dc);
						}
						I = I + Is;
					}
					const Vec3 sc = V3(bd.get("inertia_scale", Variant()), Vec3(1, 1, 1));
					for (int r = 0; r < 3; ++r) { const double s = r == 0 ? sc.X : (r == 1 ? sc.Y : sc.Z); for (int c = 0; c < 3; ++c) I.m[r][c] *= s; }
					for (int r = 0; r < 3; ++r) I.m[r][r] = std::max(I.m[r][r], 1e-3);
					Bd.InvInertiaLocal = I.Inverse();
				}
				BodyOf[Str(bd, "bone", "")] = (int)n->Bodies.size();
				n->Bodies.push_back(Bd);
			}
			Array joints = pa.get("joints", Array());
			for (int k = 0; k < joints.size(); ++k) {
				Dictionary jd = joints[k];
				auto i1 = BodyOf.find(Str(jd, "bone1", "")), i2 = BodyOf.find(Str(jd, "bone2", ""));
				if (i1 == BodyOf.end() || i2 == BodyOf.end()) continue;
				PPRigidBody::Joint J;
				J.B1 = i1->second; J.B2 = i2->second;
				J.P1 = V3(jd.get("pos1", Variant())); J.P2 = V3(jd.get("pos2", Variant()));
				J.R1 = FromAxes(V3(jd.get("pri1", Variant()), Vec3(1, 0, 0)), V3(jd.get("sec1", Variant()), Vec3(0, 1, 0)));
				J.R2 = FromAxes(V3(jd.get("pri2", Variant()), Vec3(1, 0, 0)), V3(jd.get("sec2", Variant()), Vec3(0, 1, 0)));
				Dictionary lin = jd.get("linear", Dictionary());
				J.Lin[0] = ParseMotion(Str(lin, "x", "LCM_Locked")); J.Lin[1] = ParseMotion(Str(lin, "y", "LCM_Locked")); J.Lin[2] = ParseMotion(Str(lin, "z", "LCM_Locked"));
				J.LinLimit = F(lin, "limit", 0.0);
				J.Swing1 = ParseMotion(Str(jd, "swing1", "ACM_Free")); J.Swing2 = ParseMotion(Str(jd, "swing2", "ACM_Free")); J.Twist = ParseMotion(Str(jd, "twist", "ACM_Free"));
				J.Swing1Limit = ue::DegToRad(F(jd, "swing1_limit", 45)); J.Swing2Limit = ue::DegToRad(F(jd, "swing2_limit", 45)); J.TwistLimit = ue::DegToRad(F(jd, "twist_limit", 45));
				J.bConeSoft = B(jd, "cone_soft", true); J.bTwistSoft = B(jd, "twist_soft", true);
				J.ConeK = F(jd, "cone_stiffness", 50); J.ConeD = F(jd, "cone_damping", 5);
				J.TwistK = F(jd, "twist_stiffness", 50); J.TwistD = F(jd, "twist_damping", 5);
				const std::string mode = Str(jd, "drive_mode", "SLERP");
				Dictionary sl = jd.get("slerp", Dictionary()), sw = jd.get("swing_drive", Dictionary()), tw = jd.get("twist_drive", Dictionary());
				if (mode == "SLERP") {
					J.bDrivePos = B(sl, "position", false);
					J.bDrive = J.bDrivePos || B(sl, "velocity", false);
					J.DriveK = F(sl, "stiffness", 50); J.DriveD = F(sl, "damping", 1);
				} else {
					const bool sp = B(sw, "position", false), tp = B(tw, "position", false);
					J.bDrivePos = sp || tp;
					J.bDrive = J.bDrivePos || B(sw, "velocity", false) || B(tw, "velocity", false);
					J.DriveK = std::max(sp ? F(sw, "stiffness", 50) : 0.0, tp ? F(tw, "stiffness", 50) : 0.0);
					J.DriveD = std::max(F(sw, "damping", 1), F(tw, "damping", 1));
				}
				J.DriveTarget = RotPYR(jd.get("orientation_target", Variant()));
				n->Joints.push_back(J);
			}
			Nodes.push_back(std::move(n));
		}
	}
	bReady = true;
	bHasPrev = false;
	return true;
}

void GaspPostProcess::update(Skeleton3D* skeleton, double delta, const Vector3& component_location, double component_yaw) {
	if (!bReady || !skeleton || skeleton->get_bone_count() != (int)Pose.Names.size()) return;
	const int N = (int)Pose.Names.size();
	for (int b = 0; b < N; ++b) Pose.Local[b] = FromGodot(skeleton->get_bone_pose_position(b), skeleton->get_bone_pose_rotation(b), skeleton->get_bone_pose_scale(b));
	Pose.bCSDirty = true;
	for (auto& v : Curves.Values) v = 0.f;
	PPFrame Frame;
	Frame.DeltaTime = delta;
	Frame.ComponentWorld = Xform(Quat::MakeFromYaw(component_yaw), Vec3(component_location.x, component_location.y, component_location.z));
	Frame.bTeleport = bHasPrev && (Frame.ComponentWorld.Translation - PrevComponent.Translation).Size() > 300.0;
	for (auto& n : Nodes) {
		if (auto* rb = dynamic_cast<gasp::PPRigidBody*>(n.get())) rb->FramePrevLocation = bHasPrev ? PrevComponent.Translation : Frame.ComponentWorld.Translation;
		n->Evaluate(Pose, Curves, Frame);
	}
	PrevComponent = Frame.ComponentWorld;
	bHasPrev = true;
	for (int b = 0; b < N; ++b) {
		const Xform& X = Pose.Local[b];
		skeleton->set_bone_pose_position(b, Vector3(X.Translation.X, -X.Translation.Y, X.Translation.Z) * 0.01);
		skeleton->set_bone_pose_rotation(b, Quaternion(-X.Rotation.X, X.Rotation.Y, -X.Rotation.Z, X.Rotation.W));
		skeleton->set_bone_pose_scale(b, Vector3(X.Scale3D.X, X.Scale3D.Y, X.Scale3D.Z));
	}
}

PackedStringArray GaspPostProcess::get_curve_names() const {
	PackedStringArray A;
	for (auto& n : Curves.Names) A.push_back(String(n.c_str()));
	return A;
}

PackedFloat32Array GaspPostProcess::get_curve_values() const {
	PackedFloat32Array A;
	for (float v : Curves.Values) A.push_back(v);
	return A;
}

void GaspPostProcess::set_physics_debug_flags(int flags) {
	for (auto& n : Nodes) if (auto* rb = dynamic_cast<gasp::PPRigidBody*>(n.get())) rb->DebugFlags = flags;
}

Array GaspPostProcess::get_debug_joints() const {
	Array A;
	for (auto& n : Nodes) if (auto* rb = dynamic_cast<gasp::PPRigidBody*>(n.get())) rb->DebugJoints(A);
	return A;
}

void GaspPostProcess::reset() {
	for (auto& n : Nodes) n->Reset();
	bHasPrev = false;
}

Array GaspPostProcess::get_debug_bodies() const {
	Array A;
	std::vector<std::pair<Xform, Vec3>> L;
	for (auto& n : Nodes) n->DebugBodies(L);
	for (auto& e : L) {
		// bodies are in simulation space; callers draw them relative to the skeleton (approximate for base bone space)
		const Xform& X = e.first;
		const Quaternion q(-X.Rotation.X, X.Rotation.Y, -X.Rotation.Z, X.Rotation.W);
		Dictionary D;
		D["xf"] = Transform3D(Basis(q), Vector3(X.Translation.X, -X.Translation.Y, X.Translation.Z) * 0.01);
		D["shape"] = Vector3(e.second.X * 0.01, e.second.Y * 0.01, e.second.Z);
		A.push_back(D);
	}
	return A;
}

} // namespace godot
