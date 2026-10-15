// Motion matching: port of UE PoseSearch runtime (query building, PCA-KNN/brute-force search, continuing pose, MM state).
#pragma once
#include "anim_lib.h"
#include <map>

namespace gasp {

struct TrajectorySample {
	Vec3 Position;
	Quat Facing;
	float AccumulatedSeconds = 0;
	Xform GetTransform() const { return Xform(Facing, Position); }
	TrajectorySample Lerp(const TrajectorySample& O, double A) const {
		TrajectorySample R;
		R.Facing = Quat::FastLerp(Facing, O.Facing, A).GetNormalized();
		R.Position = Vec3::Lerp(Position, O.Position, A);
		R.AccumulatedSeconds = (float)ue::Lerp(AccumulatedSeconds, O.AccumulatedSeconds, A);
		return R;
	}
};

struct Trajectory {
	std::vector<TrajectorySample> Samples;
	TrajectorySample GetSampleAtTime(float Time, bool bExtrapolate = false) const;
};

// FPoseHistory with PoseCount=2, SamplingInterval=0 and a future root entry (RootBoneRecoveryTime)
struct PoseHistoryEntry {
	float AccumulatedSeconds = 0;
	std::vector<Xform> CS; // component space transforms of collected bones (index into PoseHistory::Bones)
	std::vector<float> Curves;
};

class PoseHistory {
public:
	std::vector<int> Bones;          // skeleton bone index per slot (slot 0 = root)
	std::vector<int> CollectedCurves;
	int MaxNumPoses = 2;
	float RootBoneRecoveryTime = 0.3f;
	float RootBoneTranslationRecoveryRatio = 1.f;
	float RootBoneRotationRecoveryRatio = 1.f;
	std::vector<PoseHistoryEntry> Entries;
	Trajectory Traj;

	void Init(const std::vector<int>& InBones, const std::vector<int>& InCurves);
	int SlotOf(int Bone) const;
	// FPoseHistory::EvaluateComponentSpace_AnyThread
	void Evaluate(float DeltaTime, const std::vector<Xform>& ComponentSpacePose, const Pose& CurvesPose);
	bool GetTransformAtTime(float Time, Xform& Out, int Bone, bool bWorldSpace, bool bRelativeToRoot) const;
	bool GetCurveValueAtTime(float Time, int Curve, float& Out) const;
	bool IsEmpty() const { return Entries.empty(); }
};

enum class InputQueryPose { Character, Continuing };
enum class Strip { None, Z, XY };
enum class ChannelType { Position, Velocity, Heading, Curve };

struct Channel {
	ChannelType Type = ChannelType::Position;
	int Bone = 0;          // skeleton index (0 = root)
	int OriginBone = 0;
	std::string BoneName, OriginName;
	float SampleTimeOffset = 0, OriginTimeOffset = 0;
	InputQueryPose Input = InputQueryPose::Character;
	Strip Stripping = Strip::None;
	int Cardinality = 0, Offset = 0;
	int HeadingAxis = 0;
	int CurveIdx = -1;     // FPoseSearchFeatureChannel_Curve
	bool bCharSpaceVel = false, bNormalize = false, bBPOverride = false, bTrajectory = false;
	std::string Key; // unique identifier for cross-schema caching
};

struct Schema {
	std::string Name;
	int Cardinality = 0;
	int SampleRate = 30;
	std::vector<Channel> Channels;
	int Index = 0;
};

struct DbAsset {
	int Anim = -1;
	int Montage = -1;
	int SourceAssetIdx = 0;
	float PlayLength = 0;
	bool bLooping = false, bDisableReselection = false;
	int FirstPose = 0, FirstSample = 0, LastSample = 0;
	int NotifyAnim = -1, NotifyMontage = -1;
	int NumPoses() const { return LastSample + 1 - FirstSample; }
	float TimeFromPose(int PoseIdx, int Rate) const { return (FirstSample + (PoseIdx - FirstPose)) / float(Rate); }
	int PoseFromTime(float Time, int Rate) const {
		const int N = NumPoses();
		int Off = (int)std::lround(Rate * Time) - FirstSample;
		if (bLooping) {
			if (Off < 0) Off = (Off % N) + N;
			else if (Off >= N) Off = Off % N;
			return FirstPose + Off;
		}
		if (Off >= 0 && Off < N) return FirstPose + Off;
		return -1;
	}
};

struct Database {
	std::string Name;
	int Index = 0;
	int SchemaIdx = 0;
	int N = 0, D = 0, K = 0;
	bool bBruteForce = false;
	int KDNeighbors = 200;
	float ContinuingPoseCostBias = 0, MinCostAddend = 0;
	std::vector<std::string> Tags;
	std::vector<float> Values, WeightsSqrt, Mean, Proj, PCAValues, CostAddend;
	std::vector<int> Flags;
	std::vector<DbAsset> Assets;
	std::vector<int> PoseToAsset;
	std::vector<int> SelectablePCA; // poses in the kd structure (non block-transition)
	const float* PoseValues(int i) const { return Values.data() + (size_t)i * D; }
	bool IsBlockTransition(int i) const { return Flags[i] & 1; }
};

struct SearchResult {
	float PoseCost = 3.402823e38f;
	int PoseIdx = -1;
	int Database = -1;
	float AssetTime = 0;
	bool bIsContinuingPoseSearch = false;
	bool IsValid() const { return PoseIdx >= 0 && Database >= 0; }
	void Reset() { *this = SearchResult(); }
};

enum class InterruptMode { DoNotInterrupt, InterruptOnDatabaseChange, InterruptOnDatabaseChangeAndInvalidateContinuingPose, ForceInterrupt, ForceInterruptAndInvalidateContinuingPose };

class MotionMatchingLibrary {
public:
	static MotionMatchingLibrary& Get();
	bool Load(const std::string& DataDir);
	int FindDatabase(const std::string& Name) const;
	const Database& Db(int i) const { return Databases[i]; }
	const Schema& Sch(int i) const { return Schemas[i]; }
	int NumDatabases() const { return (int)Databases.size(); }
	std::vector<Database> Databases;
	std::vector<Schema> Schemas;
	bool bLoaded = false;
};

// override providers for BP query channels (traversal interaction transform)
struct QueryOverrides {
	bool bHasInteraction = false;
	Xform InteractionWorld;
};

class SearchContext {
public:
	SearchContext(const PoseHistory* InHistory, const SearchResult& InCurrent, const std::map<std::pair<int, int>, float>* InPoseIndicesHistory, const QueryOverrides* InOverrides);
	const std::vector<float>& GetOrBuildQuery(int SchemaIdx);
	const std::vector<float>* GetCachedQuery(int SchemaIdx) const;
	bool bUseCachedChannelData = false;
	float CurrentBestTotalCost = 3.402823e38f;
	const SearchResult& Current;
	const PoseHistory* History;
	const std::map<std::pair<int, int>, float>* PoseIndicesHistory;
	const QueryOverrides* Overrides;
	std::vector<float> CurrentResultPoseVector;
	bool bHasCurrentPoseVector = false;
private:
	struct CachedQuery { int Schema; std::vector<float> Values; };
	std::vector<CachedQuery> Cached;
	std::unordered_map<std::string, std::pair<int, int>> CachedChannels; // key -> (cached query idx, channel offset)
	std::unordered_map<uint64_t, Xform> CachedTransforms;
	int BuildingSchema = -1;
	Xform GetWorldBoneTransformAtTime(float Time, int Bone);
	Xform GetWorldRootBoneTransformAtTime(float Time);
	Vec3 GetSamplePositionInternal(float SampleTime, float OriginTime, int Bone, int Origin, const Vec3* WorldOverride);
	Quat GetSampleRotationInternal(float SampleTime, float OriginTime, int Bone, int Origin, const Quat* WorldOverride);
	bool CanUseCurrentResult() const;
	void BuildQuery(const Schema& S, std::vector<float>& Out);
};

// FMotionMatchingState + UPoseSearchLibrary::UpdateMotionMatchingState
class MotionMatchingState {
public:
	SearchResult CurrentSearchResult;
	float ElapsedPoseSearchTime = 1e30f;
	float WantedPlayRate = 1.f;
	bool bJumpedToPose = false;
	std::map<std::pair<int, int>, float> PoseIndicesHistory; // (db, pose) -> age

	void Reset();
	void AdjustAssetTime(float AssetTime);
	void Update(float DeltaTime, const std::vector<int>& Databases, InterruptMode Mode, const PoseHistory* History,
		float PoseReselectHistory, float SearchThrottleTime, float PlayRateMin, float PlayRateMax, const QueryOverrides* Overrides);
	float LastSearchCost = 0;
	// debug: query and selected pose features of the last search (schema of the selected database)
	std::vector<float> DebugQuery, DebugPose;
	int DebugSchema = -1;
	float DebugContinuingCost = 0.f; // 3.4e38 when the continuing pose was not evaluated
	std::vector<std::pair<int, float>> DebugDbCosts; // best cost per searched database (last search)
	bool DebugCanAdvance = false;
};

// helpers shared with graph code
bool CanAdvance(const SearchResult& R, float DeltaTime);
void UpdateSearchResultTime(SearchResult& R, float NewAssetTime);
const DbAsset* GetSearchIndexAsset(const SearchResult& R);

// Free-standing motion match over a set of databases/assets (PoseSearchLibrary::MotionMatch used for traversal montages)
SearchResult MotionMatchAssets(int DatabaseIdx, const std::vector<int>& AssetMontages, const PoseHistory* History, const QueryOverrides* Overrides);
// UPoseSearchLibrary::MotionMatch(AssetsToSearch = anim sequences): each asset is searched in the database named by its
// PoseSearchBranchIn notify (the database entry synchronised from that notify); the best result over all databases wins.
SearchResult MotionMatchAnimAssets(const std::vector<int>& Anims, const std::vector<int>& Databases, const PoseHistory* History);

} // namespace gasp
