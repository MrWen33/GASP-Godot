// Port of the UE 5.5 Position Based IK solver (Plugins/Experimental/FullBodyIK: PBIKSolver/PBIKBody/PBIKConstraint)
// as driven by UIKRigFBIKSolver. UE units (cm), UE math conventions.
#pragma once
#include "uemath.h"
#include <memory>
#include <string>
#include <vector>

namespace pbik {
using namespace ue;

enum class ELimitType : uint8_t { Free, Limited, Locked };
enum class ERootBehavior : uint8_t { PrePull, PinToInput, Free };

struct Rotator { double Pitch = 0, Yaw = 0, Roll = 0; };

struct BoneSettings {
	float RotationStiffness = 0.0f, PositionStiffness = 0.0f;
	ELimitType X = ELimitType::Free; float MinX = 0, MaxX = 0;
	ELimitType Y = ELimitType::Free; float MinY = 0, MaxY = 0;
	ELimitType Z = ELimitType::Free; float MinZ = 0, MaxZ = 0;
	bool bUsePreferredAngles = false;
	Rotator PreferredAngles;
};

struct RootPrePullSettings {
	float RotationAlpha = 0.0f, RotationAlphaX = 1, RotationAlphaY = 1, RotationAlphaZ = 1;
	float PositionAlpha = 1.0f, PositionAlphaX = 1, PositionAlphaY = 1, PositionAlphaZ = 1;
};

struct SolverSettings {
	int Iterations = 20;
	int SubIterations = 0;
	float MassMultiplier = 1.0f;
	bool bAllowStretch = false;
	ERootBehavior RootBehavior = ERootBehavior::PrePull;
	RootPrePullSettings PrePullRootSettings;
	float GlobalPullChainAlpha = 1.0f;
	float MaxAngle = 30.f;
	float OverRelaxation = 1.3f;
};

struct EffectorSettings {
	float PositionAlpha = 1.0f, RotationAlpha = 1.0f, StrengthAlpha = 1.0f;
	int ChainDepth = 0;
	float PullChainAlpha = 1.0f, PinRotation = 1.0f;
};

struct RigidBody;
struct PinConstraint;

struct Bone {
	std::string Name;
	int ParentIndex = -2;
	Vec3 Position;
	Quat Rotation;
	Vec3 Scale = Vec3(1, 1, 1);
	Vec3 LocalPositionFromInput;
	Quat LocalRotationFromInput;
	RigidBody* Body = nullptr;
	Bone* Parent = nullptr;
	std::vector<Bone*> Children;
	bool bIsSolverRoot = false, bIsSolved = false, bIsSubRoot = false;
	float Length = 0.f;
	Bone(const std::string& N, int P, const Vec3& Pos, const Quat& Rot, bool bRoot) : Name(N), ParentIndex(P), Position(Pos), Rotation(Rot), bIsSolverRoot(bRoot) {}
	void UpdateFromInputs();
};

struct Effector;

struct RigidBody {
	Bone* B = nullptr;
	BoneSettings J;
	PinConstraint* Pin = nullptr;
	Effector* Eff = nullptr;
	const SolverSettings* Settings = nullptr;
	Vec3 Position;
	Quat Rotation;
	Vec3 InputPosition;
	Quat InitialRotation;
	Vec3 BoneLocalPosition;
	std::vector<Vec3> ChildLocalPositions;
	float InvMass = 0.f, Mass = 0.f;
	bool bIsLockedBySubSolve = false, bIsPartOfSubChain = false;
	int NumBonesToRoot = 0;
	explicit RigidBody(Bone* InBone) : B(InBone) {}
	void Initialize(const Bone* SolverRoot);
	void UpdateFromInputs(const SolverSettings& S);
	void UpdateTransformAndMassFromBones();
	RigidBody* GetParentBody() const { return (B && B->Parent) ? B->Parent->Body : nullptr; }
	bool IsFullyLocked() const { return bIsLockedBySubSolve || InvMass <= SMALL_NUMBER; }
	void ApplyPushToRotateBody(const Vec3& Push, const Vec3& Offset);
	void ApplyPositionDelta(const Vec3& DeltaP);
	void ApplyRotationDelta(const Quat& DeltaQ);
};

struct Constraint {
	bool bEnabled = true;
	virtual ~Constraint() = default;
	virtual void Solve(const SolverSettings& S) = 0;
	virtual void RemoveStretch(float) {}
	virtual void UpdateFromInputs() {}
};

struct JointConstraint : Constraint {
	RigidBody* A; RigidBody* Bb;
	Vec3 PinPointLocalToA, PinPointLocalToB;
	Vec3 XOrig, YOrig, ZOrig, XA, YA, ZA, XB, YB, ZB, ZBProjOnX, ZBProjOnY, YBProjOnZ;
	float AngleX = 0, AngleY = 0, AngleZ = 0;
	JointConstraint(RigidBody* InA, RigidBody* InB);
	void Solve(const SolverSettings& S) override;
	void RemoveStretch(float Percent) override;
	void UpdateFromInputs() override;
	Vec3 GetPositionCorrection(Vec3& OutBodyToA, Vec3& OutBodyToB) const;
	void ApplyRotationCorrection(const Quat& DeltaQ) const;
	void UpdateJointLimits();
	void RotateWithinLimits(float MinAngle, float MaxAngle, float CurrentAngle, const Vec3& RotAxis, const Vec3& CurVec, const Vec3& RefVec) const;
	void RotateToAlignAxes(const Vec3& AxisA, const Vec3& AxisB) const;
	void UpdateLocalRotateAxes(bool bX, bool bY, bool bZ);
	void DecomposeRotationAngles();
	static float SignedAngleBetweenNormals(const Vec3& From, const Vec3& To, const Vec3& Axis);
};

struct PinConstraint : Constraint {
	Vec3 GoalPosition;
	Quat GoalRotation;
	RigidBody* A;
	Vec3 PinPointLocalToA;
	float Alpha = 1.0f;
	PinConstraint(RigidBody* InBody, const Vec3& InPinPositionOrig, const Quat& InPinRotationOrig);
	void Solve(const SolverSettings& S) override;
	void SetGoal(const Vec3& P, const Quat& R, float InAlpha) { GoalPosition = P; GoalRotation = R; Alpha = InAlpha; }
	Vec3 GetPositionCorrection(Vec3& OutBodyToPinPoint) const;
};

struct Effector {
	Vec3 Position; Quat Rotation;
	Vec3 PositionOrig; Quat RotationOrig;
	Vec3 PositionGoal; Quat RotationGoal;
	EffectorSettings Settings;
	Bone* B;
	PinConstraint* Pin = nullptr;
	RigidBody* ChainRootBody = nullptr;
	int ChainDepthInitializedWith = -1;
	float DistToChainRootInInputPose = 0;
	std::vector<float> DistancesFromEffector;
	float DistToChainRootAlongBones = 0;
	explicit Effector(Bone* InBone);
	void SetGoal(const Vec3& P, const Quat& R, const EffectorSettings& S);
	void UpdateChainStates();
	void UpdateFromInputs(const Bone& SolverRoot);
	float CalculateDistanceToChainRoot() const;
	void ApplyPreferredAngles() const;
};

class Solver {
public:
	bool Initialize();
	void Solve(const SolverSettings& S);
	void Reset();
	bool IsReadyToSimulate() const { return bReadyToSimulate; }
	void SetBoneTransform(int Index, const Xform& T);
	BoneSettings* GetBoneSettings(int Index);
	void SetEffectorGoal(int Index, const Vec3& P, const Quat& R, const EffectorSettings& S);
	void GetBoneGlobalTransform(int Index, Xform& Out) const;
	int GetNumBones() const { return (int)Bones.size(); }
	int GetBoneIndex(const std::string& Name) const;
	int AddBone(const std::string& Name, int ParentIndex, const Vec3& Pos, const Quat& Rot, bool bIsSolverRoot);
	int AddEffector(const std::string& BoneName);
private:
	bool InitBones();
	bool InitBodies();
	bool InitConstraints();
	void UpdateEffectorDepths(bool bForceUpdate);
	void AddBodyForBone(Bone* B);
	void UpdateBodies(const SolverSettings& S);
	void UpdateBonesFromBodies();
	void SolveConstraints(const SolverSettings& S);
	void ApplyRootPrePull(ERootBehavior RootBehavior, const RootPrePullSettings& P);
	void ApplyPullChainAlpha(float GlobalPullChainAlpha);
	void ApplyPreferredAngles();
	static Quat GetRotationFromDeformedPoints(const std::vector<Vec3>& Initial, const std::vector<Vec3>& Current, Vec3& OutInitialCentroid, Vec3& OutCurrentCentroid);

	Bone* SolverRoot = nullptr;
	std::vector<Bone> Bones;
	std::vector<RigidBody> Bodies;
	std::vector<std::unique_ptr<Constraint>> Constraints;
	std::vector<Effector> Effectors;
	bool bHasSubChains = false;
	bool bReadyToSimulate = false;
};

} // namespace pbik
