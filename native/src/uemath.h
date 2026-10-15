// Minimal UE-compatible math (FVector / FQuat / FTransform semantics) so engine code can be ported verbatim.
// All runtime animation math runs in UE space (cm, Z-up, UE quaternion conventions).
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ue {

constexpr double PI = 3.14159265358979323846;
constexpr double HALF_PI = PI * 0.5;
constexpr double SMALL_NUMBER = 1.e-8;
constexpr double KINDA_SMALL_NUMBER = 1.e-4;
constexpr float ZERO_ANIMWEIGHT_THRESH = 0.00001f;

template <typename T> inline T Clamp(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }
template <typename T> inline T Square(T v) { return v * v; }
inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
inline bool IsNearlyZero(double v, double tol = SMALL_NUMBER) { return std::abs(v) <= tol; }
inline bool IsNearlyEqual(double a, double b, double tol = SMALL_NUMBER) { return std::abs(a - b) <= tol; }
inline double DegToRad(double d) { return d * (PI / 180.0); }
inline double RadToDeg(double r) { return r * (180.0 / PI); }
inline double Sign(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

// FRotator::NormalizeAxis
inline double NormalizeAxis(double Angle) {
	Angle = std::fmod(Angle, 360.0);
	if (Angle < 0.0) Angle += 360.0;
	if (Angle > 180.0) Angle -= 360.0;
	return Angle;
}
// FMath::UnwindRadians
inline double UnwindRadians(double A) {
	while (A > PI) A -= 2 * PI;
	while (A < -PI) A += 2 * PI;
	return A;
}
inline double UnwindDegrees(double A) {
	while (A > 180.0) A -= 360.0;
	while (A < -180.0) A += 360.0;
	return A;
}
inline double SmoothStep(double A, double B, double X) {
	if (X < A) return 0.0;
	if (X >= B) return 1.0;
	const double F = (X - A) / (B - A);
	return F * F * (3.0 - 2.0 * F);
}
inline double InterpEaseInOut(double A, double B, double Alpha, double Exp) {
	const double ModifiedAlpha = (Alpha < 0.5) ? 0.5 * std::pow(2.0 * Alpha, Exp) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - Alpha), Exp);
	return Lerp(A, B, ModifiedAlpha);
}
inline double CubicInterp(double P0, double T0, double P1, double T1, double A) {
	const double A2 = A * A, A3 = A2 * A;
	return (((2 * A3) - (3 * A2) + 1) * P0) + ((A3 - (2 * A2) + A) * T0) + ((A3 - A2) * T1) + (((-2 * A3) + (3 * A2)) * P1);
}

struct Vec3 {
	double X = 0, Y = 0, Z = 0;
	Vec3() = default;
	Vec3(double x, double y, double z) : X(x), Y(y), Z(z) {}
	static Vec3 Zero() { return Vec3(); }
	static Vec3 One() { return Vec3(1, 1, 1); }
	static Vec3 Up() { return Vec3(0, 0, 1); }
	static Vec3 Forward() { return Vec3(1, 0, 0); }
	Vec3 operator+(const Vec3& o) const { return Vec3(X + o.X, Y + o.Y, Z + o.Z); }
	Vec3 operator-(const Vec3& o) const { return Vec3(X - o.X, Y - o.Y, Z - o.Z); }
	Vec3 operator-() const { return Vec3(-X, -Y, -Z); }
	Vec3 operator*(double s) const { return Vec3(X * s, Y * s, Z * s); }
	Vec3 operator*(const Vec3& o) const { return Vec3(X * o.X, Y * o.Y, Z * o.Z); }
	Vec3 operator/(double s) const { return Vec3(X / s, Y / s, Z / s); }
	Vec3& operator+=(const Vec3& o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
	Vec3& operator-=(const Vec3& o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
	Vec3& operator*=(double s) { X *= s; Y *= s; Z *= s; return *this; }
	double operator|(const Vec3& o) const { return X * o.X + Y * o.Y + Z * o.Z; }
	Vec3 operator^(const Vec3& o) const { return Vec3(Y * o.Z - Z * o.Y, Z * o.X - X * o.Z, X * o.Y - Y * o.X); }
	double Dot(const Vec3& o) const { return *this | o; }
	Vec3 Cross(const Vec3& o) const { return *this ^ o; }
	double SizeSquared() const { return X * X + Y * Y + Z * Z; }
	double Size() const { return std::sqrt(SizeSquared()); }
	double Length() const { return Size(); }
	double Size2D() const { return std::sqrt(X * X + Y * Y); }
	double SizeSquared2D() const { return X * X + Y * Y; }
	bool IsZero() const { return X == 0 && Y == 0 && Z == 0; }
	bool IsNearlyZero(double Tol = KINDA_SMALL_NUMBER) const { return std::abs(X) <= Tol && std::abs(Y) <= Tol && std::abs(Z) <= Tol; }
	Vec3 GetSafeNormal(double Tol = SMALL_NUMBER) const {
		const double S = SizeSquared();
		if (S == 1.0) return *this;
		if (S < Tol) return Vec3();
		return *this * (1.0 / std::sqrt(S));
	}
	Vec3 GetSafeNormal2D(double Tol = SMALL_NUMBER) const {
		const double S = X * X + Y * Y;
		if (S == 1.0) return Z == 0 ? *this : Vec3(X, Y, 0);
		if (S < Tol) return Vec3();
		const double I = 1.0 / std::sqrt(S);
		return Vec3(X * I, Y * I, 0);
	}
	Vec3 GetUnsafeNormal() const { return *this * (1.0 / Size()); }
	bool Normalize(double Tol = SMALL_NUMBER) {
		const double S = SizeSquared();
		if (S > Tol) { *this *= 1.0 / std::sqrt(S); return true; }
		return false;
	}
	Vec3 GetClampedToMaxSize(double MaxSize) const {
		if (MaxSize < KINDA_SMALL_NUMBER) return Vec3();
		const double S = SizeSquared();
		if (S > MaxSize * MaxSize) return *this * (MaxSize / std::sqrt(S));
		return *this;
	}
	Vec3 GetClampedToSize(double Min, double Max) const {
		double L = Size();
		Vec3 Dir = L > SMALL_NUMBER ? *this / L : Vec3();
		L = Clamp(L, Min, Max);
		return Dir * L;
	}
	Vec3 ProjectOnTo(const Vec3& A) const { return A * ((*this | A) / (A | A)); }
	Vec3 ProjectOnToNormal(const Vec3& N) const { return N * (*this | N); }
	double HeadingAngle() const {
		Vec3 PlaneDir = *this; PlaneDir.Z = 0; PlaneDir = PlaneDir.GetSafeNormal();
		double Angle = std::acos(Clamp(PlaneDir.X, -1.0, 1.0));
		if (PlaneDir.Y < 0.0) Angle *= -1.0;
		return Angle;
	}
	static Vec3 Lerp(const Vec3& A, const Vec3& B, double T) { return A + (B - A) * T; }
	static double Dist(const Vec3& A, const Vec3& B) { return (A - B).Size(); }
	static double DistSquared(const Vec3& A, const Vec3& B) { return (A - B).SizeSquared(); }
	static double Dist2D(const Vec3& A, const Vec3& B) { return (A - B).Size2D(); }
	static Vec3 VectorPlaneProject(const Vec3& V, const Vec3& N) { return V - V.ProjectOnToNormal(N); }
	static Vec3 PointPlaneProject(const Vec3& P, const Vec3& Origin, const Vec3& N) { return P - N * ((P - Origin) | N); }
};
inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

struct Quat {
	double X = 0, Y = 0, Z = 0, W = 1;
	Quat() = default;
	Quat(double x, double y, double z, double w) : X(x), Y(y), Z(z), W(w) {}
	Quat(const Vec3& Axis, double AngleRad) {
		const double h = 0.5 * AngleRad, s = std::sin(h);
		X = s * Axis.X; Y = s * Axis.Y; Z = s * Axis.Z; W = std::cos(h);
	}
	static Quat Identity() { return Quat(); }
	// UE: (A * B) applies B first then A.
	Quat operator*(const Quat& Q) const {
		return Quat(
			W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
			W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
			W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
			W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z);
	}
	Quat operator*(double s) const { return Quat(X * s, Y * s, Z * s, W * s); }
	Quat operator+(const Quat& q) const { return Quat(X + q.X, Y + q.Y, Z + q.Z, W + q.W); }
	Quat operator-(const Quat& q) const { return Quat(X - q.X, Y - q.Y, Z - q.Z, W - q.W); }
	double operator|(const Quat& q) const { return X * q.X + Y * q.Y + Z * q.Z + W * q.W; }
	Quat Inverse() const { return Quat(-X, -Y, -Z, W); }
	double SizeSquared() const { return X * X + Y * Y + Z * Z + W * W; }
	double Size() const { return std::sqrt(SizeSquared()); }
	void Normalize(double Tol = SMALL_NUMBER) {
		const double S = SizeSquared();
		if (S >= Tol) { const double I = 1.0 / std::sqrt(S); X *= I; Y *= I; Z *= I; W *= I; }
		else *this = Quat();
	}
	Quat GetNormalized(double Tol = SMALL_NUMBER) const { Quat q = *this; q.Normalize(Tol); return q; }
	bool IsNormalized() const { return std::abs(1.0 - SizeSquared()) < 0.01; }
	bool IsIdentity(double Tol = SMALL_NUMBER) const { return Equals(Quat(), Tol); }
	bool Equals(const Quat& Q, double Tol = KINDA_SMALL_NUMBER) const {
		return (std::abs(X - Q.X) <= Tol && std::abs(Y - Q.Y) <= Tol && std::abs(Z - Q.Z) <= Tol && std::abs(W - Q.W) <= Tol) ||
			(std::abs(X + Q.X) <= Tol && std::abs(Y + Q.Y) <= Tol && std::abs(Z + Q.Z) <= Tol && std::abs(W + Q.W) <= Tol);
	}
	Vec3 RotateVector(const Vec3& V) const {
		const Vec3 Q(X, Y, Z);
		const Vec3 T = (Q ^ V) * 2.0;
		return V + (T * W) + (Q ^ T);
	}
	Vec3 UnrotateVector(const Vec3& V) const {
		const Vec3 Q(-X, -Y, -Z);
		const Vec3 T = (Q ^ V) * 2.0;
		return V + (T * W) + (Q ^ T);
	}
	Vec3 GetAxisX() const { return RotateVector(Vec3(1, 0, 0)); }
	Vec3 GetAxisY() const { return RotateVector(Vec3(0, 1, 0)); }
	Vec3 GetAxisZ() const { return RotateVector(Vec3(0, 0, 1)); }
	Vec3 GetForwardVector() const { return GetAxisX(); }
	Vec3 GetRightVector() const { return GetAxisY(); }
	Vec3 GetUpVector() const { return GetAxisZ(); }
	double GetAngle() const { return 2.0 * std::acos(Clamp(W, -1.0, 1.0)); }
	Vec3 GetRotationAxis() const {
		const double S = std::sqrt(std::max(1.0 - W * W, 0.0));
		if (S >= 0.0001) return Vec3(X / S, Y / S, Z / S);
		return Vec3(1, 0, 0);
	}
	void ToAxisAndAngle(Vec3& Axis, double& Angle) const { Angle = GetAngle(); Axis = GetRotationAxis(); }
	void EnforceShortestArcWith(const Quat& Other) {
		const double D = *this | Other;
		const double Bias = D >= 0 ? 1.0 : -1.0;
		X *= Bias; Y *= Bias; Z *= Bias; W *= Bias;
	}
	Vec3 ToRotationVector() const {
		Quat RotQ = Log();
		return Vec3(RotQ.X * 2.0, RotQ.Y * 2.0, RotQ.Z * 2.0);
	}
	static Quat MakeFromRotationVector(const Vec3& RV) {
		Quat RotQ(RV.X * 0.5, RV.Y * 0.5, RV.Z * 0.5, 0.0);
		return RotQ.Exp();
	}
	Quat Log() const {
		Quat R; R.W = 0;
		if (std::abs(W) < 1.0) {
			const double Angle = std::acos(W);
			const double SinAngle = std::sin(Angle);
			if (std::abs(SinAngle) >= SMALL_NUMBER) {
				const double Scale = Angle / SinAngle;
				R.X = Scale * X; R.Y = Scale * Y; R.Z = Scale * Z;
				return R;
			}
		}
		R.X = X; R.Y = Y; R.Z = Z;
		return R;
	}
	Quat Exp() const {
		const double Angle = std::sqrt(X * X + Y * Y + Z * Z);
		const double SinAngle = std::sin(Angle);
		Quat R; R.W = std::cos(Angle);
		if (std::abs(SinAngle) >= SMALL_NUMBER) {
			const double Scale = SinAngle / Angle;
			R.X = Scale * X; R.Y = Scale * Y; R.Z = Scale * Z;
		} else { R.X = X; R.Y = Y; R.Z = Z; }
		return R;
	}
	// swing/twist decomposition
	void ToSwingTwist(const Vec3& InTwistAxis, Quat& OutSwing, Quat& OutTwist) const {
		Vec3 Projection = InTwistAxis * (Vec3(X, Y, Z) | InTwistAxis);
		OutTwist = Quat(Projection.X, Projection.Y, Projection.Z, W);
		if (OutTwist.SizeSquared() == 0.0) OutTwist = Quat();
		else OutTwist.Normalize();
		OutSwing = *this * OutTwist.Inverse();
	}
	double GetTwistAngle(const Vec3& TwistAxis) const {
		double XYZ = Vec3(X, Y, Z) | TwistAxis;
		return UnwindRadians(2.0 * std::atan2(XYZ, W));
	}
	static Quat FindBetweenNormals(const Vec3& A, const Vec3& B) {
		const double NormAB = 1.0;
		double Wv = NormAB + (A | B);
		Quat R;
		if (Wv >= 1e-6 * NormAB) {
			R = Quat(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X, Wv);
		} else {
			Wv = 0.0;
			R = std::abs(A.X) > std::abs(A.Y) ? Quat(-A.Z, 0.0, A.X, Wv) : Quat(0.0, -A.Z, A.Y, Wv);
		}
		R.Normalize();
		return R;
	}
	static Quat FindBetweenVectors(const Vec3& A, const Vec3& B) {
		const double NormAB = std::sqrt(A.SizeSquared() * B.SizeSquared());
		double Wv = NormAB + (A | B);
		Quat R;
		if (Wv >= 1e-6 * NormAB) {
			R = Quat(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X, Wv);
		} else {
			Wv = 0.0;
			R = std::abs(A.X) > std::abs(A.Y) ? Quat(-A.Z, 0.0, A.X, Wv) : Quat(0.0, -A.Z, A.Y, Wv);
		}
		R.Normalize();
		return R;
	}
	static Quat FastLerp(const Quat& A, const Quat& B, double Alpha) {
		const double DotResult = A | B;
		const double Bias = DotResult >= 0 ? 1.0 : -1.0;
		return (B * Alpha) + (A * (Bias * (1.0 - Alpha)));
	}
	static Quat Slerp_NotNormalized(const Quat& Quat1, const Quat& Quat2, double Slerp) {
		const double RawCosom = Quat1 | Quat2;
		const double Cosom = RawCosom >= 0 ? RawCosom : -RawCosom;
		double Scale0, Scale1;
		if (Cosom < 0.9999) {
			const double Omega = std::acos(Cosom);
			const double InvSin = 1.0 / std::sin(Omega);
			Scale0 = std::sin((1.0 - Slerp) * Omega) * InvSin;
			Scale1 = std::sin(Slerp * Omega) * InvSin;
		} else {
			Scale0 = 1.0 - Slerp;
			Scale1 = Slerp;
		}
		Scale1 = RawCosom >= 0 ? Scale1 : -Scale1;
		return Quat(Scale0 * Quat1.X + Scale1 * Quat2.X, Scale0 * Quat1.Y + Scale1 * Quat2.Y, Scale0 * Quat1.Z + Scale1 * Quat2.Z, Scale0 * Quat1.W + Scale1 * Quat2.W);
	}
	static Quat Slerp(const Quat& A, const Quat& B, double S) { return Slerp_NotNormalized(A, B, S).GetNormalized(); }
	static double AngularDistance(const Quat& A, const Quat& B) {
		const double InnerProd = A | B;
		return std::acos(Clamp(2 * InnerProd * InnerProd - 1.0, -1.0, 1.0));
	}
	// FRotator(Pitch, Yaw, Roll) degrees -> quat (UE FRotator::Quaternion)
	static Quat MakeFromEulerPYR(double Pitch, double Yaw, double Roll) {
		const double DIVIDE_BY_2 = PI / 180.0 / 2.0;
		const double SP = std::sin(Pitch * DIVIDE_BY_2), CP = std::cos(Pitch * DIVIDE_BY_2);
		const double SY = std::sin(Yaw * DIVIDE_BY_2), CY = std::cos(Yaw * DIVIDE_BY_2);
		const double SR = std::sin(Roll * DIVIDE_BY_2), CR = std::cos(Roll * DIVIDE_BY_2);
		return Quat(CR * SP * SY - SR * CP * CY, -CR * SP * CY - SR * CP * SY, CR * CP * SY - SR * SP * CY, CR * CP * CY + SR * SP * SY);
	}
	static Quat MakeFromYaw(double YawDeg) { return MakeFromEulerPYR(0, YawDeg, 0); }
	// FQuat::Rotator()
	void ToRotator(double& Pitch, double& Yaw, double& Roll) const {
		const double SingularityTest = Z * X - W * Y;
		const double YawY = 2.0 * (W * Z + X * Y);
		const double YawX = (1.0 - 2.0 * (Y * Y + Z * Z));
		const double SINGULARITY_THRESHOLD = 0.4999995;
		const double RAD_TO_DEG = 180.0 / PI;
		if (SingularityTest < -SINGULARITY_THRESHOLD) {
			Pitch = -90.0; Yaw = std::atan2(YawY, YawX) * RAD_TO_DEG;
			Roll = NormalizeAxis(-Yaw - (2.0 * std::atan2(X, W) * RAD_TO_DEG));
		} else if (SingularityTest > SINGULARITY_THRESHOLD) {
			Pitch = 90.0; Yaw = std::atan2(YawY, YawX) * RAD_TO_DEG;
			Roll = NormalizeAxis(Yaw - (2.0 * std::atan2(X, W) * RAD_TO_DEG));
		} else {
			Pitch = std::asin(2.0 * SingularityTest) * RAD_TO_DEG;
			Yaw = std::atan2(YawY, YawX) * RAD_TO_DEG;
			Roll = std::atan2(-2.0 * (W * X + Y * Z), (1.0 - 2.0 * (X * X + Y * Y))) * RAD_TO_DEG;
		}
	}
	double Yaw() const { double p, y, r; ToRotator(p, y, r); return y; }
	// FVector::ToOrientationQuat
	static Quat FromDirection(const Vec3& V) {
		const double YawRad = std::atan2(V.Y, V.X);
		const double PitchRad = std::atan2(V.Z, std::sqrt(V.X * V.X + V.Y * V.Y));
		const double SP = std::sin(PitchRad * 0.5), SY = std::sin(YawRad * 0.5);
		const double CP = std::cos(PitchRad * 0.5), CY = std::cos(YawRad * 0.5);
		return Quat(SP * SY, -SP * CY, CP * SY, CP * CY);
	}
};

// FTransform with UE multiplication order: (A * B) = apply A then B.
struct Xform {
	Quat Rotation;
	Vec3 Translation;
	Vec3 Scale3D = Vec3(1, 1, 1);
	Xform() = default;
	Xform(const Quat& R, const Vec3& T, const Vec3& S = Vec3(1, 1, 1)) : Rotation(R), Translation(T), Scale3D(S) {}
	static Xform Identity() { return Xform(); }
	Xform operator*(const Xform& B) const {
		// A = this
		Xform Out;
		Out.Rotation = B.Rotation * Rotation;
		Out.Scale3D = Scale3D * B.Scale3D;
		Out.Translation = B.Rotation.RotateVector(B.Scale3D * Translation) + B.Translation;
		return Out;
	}
	Xform& operator*=(const Xform& B) { *this = *this * B; return *this; }
	Vec3 TransformPosition(const Vec3& V) const { return Rotation.RotateVector(Scale3D * V) + Translation; }
	Vec3 TransformPositionNoScale(const Vec3& V) const { return Rotation.RotateVector(V) + Translation; }
	Vec3 TransformVector(const Vec3& V) const { return Rotation.RotateVector(Scale3D * V); }
	Vec3 TransformVectorNoScale(const Vec3& V) const { return Rotation.RotateVector(V); }
	Quat TransformRotation(const Quat& Q) const { return Rotation * Q; }
	Vec3 InverseTransformPosition(const Vec3& V) const {
		const Vec3 P = Rotation.UnrotateVector(V - Translation);
		return Vec3(P.X / Scale3D.X, P.Y / Scale3D.Y, P.Z / Scale3D.Z);
	}
	Vec3 InverseTransformPositionNoScale(const Vec3& V) const { return Rotation.UnrotateVector(V - Translation); }
	Vec3 InverseTransformVector(const Vec3& V) const {
		const Vec3 P = Rotation.UnrotateVector(V);
		return Vec3(P.X / Scale3D.X, P.Y / Scale3D.Y, P.Z / Scale3D.Z);
	}
	Vec3 InverseTransformVectorNoScale(const Vec3& V) const { return Rotation.UnrotateVector(V); }
	Quat InverseTransformRotation(const Quat& Q) const { return Rotation.Inverse() * Q; }
	Xform Inverse() const {
		const Quat InvRot = Rotation.Inverse();
		const Vec3 InvScale(1.0 / Scale3D.X, 1.0 / Scale3D.Y, 1.0 / Scale3D.Z);
		const Vec3 InvTrans = InvRot.RotateVector(InvScale * -Translation);
		return Xform(InvRot, InvTrans, InvScale);
	}
	// this relative to Other: this * Other^-1
	Xform GetRelativeTransform(const Xform& Other) const {
		Xform Result;
		const Vec3 SafeRecip(1.0 / Other.Scale3D.X, 1.0 / Other.Scale3D.Y, 1.0 / Other.Scale3D.Z);
		const Quat Inverse = Other.Rotation.Inverse();
		Result.Rotation = Inverse * Rotation;
		Result.Translation = Inverse.RotateVector(Translation - Other.Translation) * SafeRecip;
		Result.Scale3D = Scale3D * SafeRecip;
		return Result;
	}
	Vec3 GetUnitAxis(int Axis) const { return Axis == 0 ? Rotation.GetAxisX() : (Axis == 1 ? Rotation.GetAxisY() : Rotation.GetAxisZ()); }
	// FTransform::Blend (lerp translation/scale, nlerp rotation shortest)
	void Blend(const Xform& A, const Xform& B, double Alpha) {
		if (Alpha <= ZERO_ANIMWEIGHT_THRESH) { *this = A; return; }
		if (Alpha >= 1.f - ZERO_ANIMWEIGHT_THRESH) { *this = B; return; }
		Rotation = Quat::FastLerp(A.Rotation, B.Rotation, Alpha).GetNormalized();
		Translation = Vec3::Lerp(A.Translation, B.Translation, Alpha);
		Scale3D = Vec3::Lerp(A.Scale3D, B.Scale3D, Alpha);
	}
	void BlendWith(const Xform& Other, double Alpha) {
		if (Alpha > ZERO_ANIMWEIGHT_THRESH) {
			if (Alpha >= 1.f - ZERO_ANIMWEIGHT_THRESH) { *this = Other; return; }
			Rotation = Quat::FastLerp(Rotation, Other.Rotation, Alpha).GetNormalized();
			Translation = Vec3::Lerp(Translation, Other.Translation, Alpha);
			Scale3D = Vec3::Lerp(Scale3D, Other.Scale3D, Alpha);
		}
	}
	// weighted accumulation helpers (FTransform::operator*(ScalarRegister) + AccumulateWithShortestRotation)
	Xform Scaled(double W) const { return Xform(Rotation * W, Translation * W, Scale3D * W); }
	void AccumulateWithShortestRotation(const Xform& Atom, double W) {
		const Quat BlendQ = Atom.Rotation * W;
		if ((Rotation | BlendQ) >= 0) Rotation = Rotation + BlendQ;
		else Rotation = Rotation - BlendQ;
		Translation += Atom.Translation * W;
		Scale3D += Atom.Scale3D * W;
	}
	void NormalizeRotation() { Rotation.Normalize(); }
	Vec3 GetLocation() const { return Translation; }
	void SetLocation(const Vec3& V) { Translation = V; }
};

// FAlphaBlend::AlphaToBlendOption subset used by the sample
enum class BlendOption { Linear, Cubic, HermiteCubic, Sinusoidal, QuadraticInOut, CubicInOut, QuarticInOut, QuinticInOut, CircularIn, CircularOut, CircularInOut, ExpIn, ExpOut, ExpInOut, Custom };
inline double AlphaToBlendOption(double InAlpha, BlendOption Opt) {
	switch (Opt) {
		case BlendOption::Sinusoidal: return Clamp((std::sin(InAlpha * PI - HALF_PI) + 1.0) / 2.0, 0.0, 1.0);
		case BlendOption::Cubic: return Clamp(CubicInterp(0.0, 0.0, 1.0, 0.0, InAlpha), 0.0, 1.0);
		case BlendOption::QuadraticInOut: return Clamp(InterpEaseInOut(0.0, 1.0, InAlpha, 2), 0.0, 1.0);
		case BlendOption::CubicInOut: return Clamp(InterpEaseInOut(0.0, 1.0, InAlpha, 3), 0.0, 1.0);
		case BlendOption::HermiteCubic: return Clamp(SmoothStep(0.0, 1.0, InAlpha), 0.0, 1.0);
		case BlendOption::QuarticInOut: return Clamp(InterpEaseInOut(0.0, 1.0, InAlpha, 4), 0.0, 1.0);
		case BlendOption::QuinticInOut: return Clamp(InterpEaseInOut(0.0, 1.0, InAlpha, 5), 0.0, 1.0);
		default: return Clamp(InAlpha, 0.0, 1.0);
	}
}

// FMath::QInterpConstantTo
inline Quat QInterpConstantTo(const Quat& Current, const Quat& Target, double DeltaTime, double InterpSpeed) {
	if (InterpSpeed <= 0.0 || Current.Equals(Target)) return Target;
	double DeltaInterpSpeed = Clamp(DeltaTime * InterpSpeed, 0.0, 1.0);
	double AngularDistance = std::max(SMALL_NUMBER, Quat::AngularDistance(Target, Current));
	double Alpha = Clamp(DeltaInterpSpeed / AngularDistance, 0.0, 1.0);
	return Quat::Slerp(Current, Target, Alpha);
}
// FMath::FInterpTo
inline double FInterpTo(double Current, double Target, double DeltaTime, double InterpSpeed) {
	if (InterpSpeed <= 0.0) return Target;
	const double Dist = Target - Current;
	if (Dist * Dist < SMALL_NUMBER) return Target;
	const double DeltaMove = Dist * Clamp(DeltaTime * InterpSpeed, 0.0, 1.0);
	return Current + DeltaMove;
}

// spring damper helpers (UE::Anim / FMath critically damped)
inline double InvExpApprox(double X) {
	constexpr double A = 1.00746054, B = 0.45053901, C = 0.25724632;
	return 1.0 / (1.0 + A * X + B * X * X + C * X * X * X);
}
inline void CriticalSpringDamper(double& InOutX, double& InOutV, double TargetX, double SmoothingTime, double DeltaTime) {
	if (SmoothingTime < SMALL_NUMBER) { InOutX = TargetX; InOutV = 0; return; }
	const double Omega = 2.0 / SmoothingTime;
	const double Exp = InvExpApprox(Omega * DeltaTime);
	const double Change = InOutX - TargetX;
	const double Temp = (InOutV + Change * Omega) * DeltaTime;
	InOutV = (InOutV - Temp * Omega) * Exp;
	InOutX = TargetX + (Change + Temp) * Exp;
}
inline void CriticalSpringDamperVec(Vec3& InOutX, Vec3& InOutV, const Vec3& TargetX, double SmoothingTime, double DeltaTime) {
	if (SmoothingTime < SMALL_NUMBER) { InOutX = TargetX; InOutV = Vec3(); return; }
	const double Omega = 2.0 / SmoothingTime;
	const double Exp = InvExpApprox(Omega * DeltaTime);
	const Vec3 Change = InOutX - TargetX;
	const Vec3 Temp = (InOutV + Change * Omega) * DeltaTime;
	InOutV = (InOutV - Temp * Omega) * Exp;
	InOutX = TargetX + (Change + Temp) * Exp;
}
// FMath::ExponentialSmoothingApprox
inline void ExponentialSmoothingApprox(double& InOutValue, double Target, double DeltaTime, double SmoothingTime) {
	if (SmoothingTime > KINDA_SMALL_NUMBER) {
		InOutValue = Target + (InOutValue - Target) * InvExpApprox(DeltaTime * (2.0 / SmoothingTime) * 0.5 * 2.0);
	} else InOutValue = Target;
}

} // namespace ue
