#include "gasp_animator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>

using namespace gasp;

namespace godot {

static std::string StdS(const String& s) { return std::string(s.utf8().get_data()); }
static ue::Vec3 V(const Vector3& v) { return ue::Vec3(v.x, v.y, v.z); }
static ue::Quat Q(const Quaternion& q) { return ue::Quat(q.x, q.y, q.z, q.w); }
static Vector3 GV(const ue::Vec3& v) { return Vector3(v.X, v.Y, v.Z); }
static Quaternion GQ(const ue::Quat& q) { return Quaternion(q.X, q.Y, q.Z, q.W); }

void GaspAnimator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "data_dir"), &GaspAnimator::initialize);
	ClassDB::bind_method(D_METHOD("reset", "comp_pos", "comp_rot"), &GaspAnimator::reset);
	ClassDB::bind_method(D_METHOD("update", "delta", "inputs"), &GaspAnimator::update);
	ClassDB::bind_method(D_METHOD("apply_to_skeleton", "skeleton"), &GaspAnimator::apply_to_skeleton);
	ClassDB::bind_method(D_METHOD("get_curve", "name"), &GaspAnimator::get_curve);
	ClassDB::bind_method(D_METHOD("get_state"), &GaspAnimator::get_state);
	ClassDB::bind_method(D_METHOD("get_root_offset_ue"), &GaspAnimator::get_root_offset_ue);
	ClassDB::bind_method(D_METHOD("get_database_tags"), &GaspAnimator::get_database_tags);
	ClassDB::bind_method(D_METHOD("get_fired_notifies"), &GaspAnimator::get_fired_notifies);
	ClassDB::bind_method(D_METHOD("set_physics_space", "space", "exclude"), &GaspAnimator::set_physics_space);
	ClassDB::bind_method(D_METHOD("set_post_process_scales", "scales"), &GaspAnimator::set_post_process_scales);
	ClassDB::bind_method(D_METHOD("get_foot_placement_debug"), &GaspAnimator::get_foot_placement_debug);
	ClassDB::bind_method(D_METHOD("get_bone_cs_ue", "bone"), &GaspAnimator::get_bone_cs_ue);
	ClassDB::bind_method(D_METHOD("montage_play", "montage", "play_rate", "start_position"), &GaspAnimator::montage_play);
	ClassDB::bind_method(D_METHOD("montage_stop", "blend_time"), &GaspAnimator::montage_stop);
	ClassDB::bind_method(D_METHOD("montage_tick", "delta", "owner"), &GaspAnimator::montage_tick);
	ClassDB::bind_method(D_METHOD("montage_state"), &GaspAnimator::montage_state);
	ClassDB::bind_method(D_METHOD("set_warp_target", "name", "pos", "rot"), &GaspAnimator::set_warp_target);
	ClassDB::bind_method(D_METHOD("remove_warp_target", "name"), &GaspAnimator::remove_warp_target);
	ClassDB::bind_method(D_METHOD("is_anim_looping", "anim"), &GaspAnimator::is_anim_looping);
	ClassDB::bind_method(D_METHOD("get_anim_curve", "anim", "curve", "time"), &GaspAnimator::get_anim_curve);
	ClassDB::bind_method(D_METHOD("motion_match_anims", "anims", "databases"), &GaspAnimator::motion_match_anims);
	ClassDB::bind_method(D_METHOD("motion_match_montages", "montages", "interaction_pos", "interaction_rot"), &GaspAnimator::motion_match_montages);
	ClassDB::bind_method(D_METHOD("get_montage_curve", "montage", "curve", "time"), &GaspAnimator::get_montage_curve);
	ClassDB::bind_method(D_METHOD("get_montage_warp_windows", "montage", "target"), &GaspAnimator::get_montage_warp_windows);
	ClassDB::bind_static_method("GaspAnimator", D_METHOD("ue_to_godot_pos", "v"), &GaspAnimator::ue_to_godot_pos);
	ClassDB::bind_static_method("GaspAnimator", D_METHOD("godot_to_ue_pos", "v"), &GaspAnimator::godot_to_ue_pos);
	ClassDB::bind_static_method("GaspAnimator", D_METHOD("ue_to_godot_quat", "q"), &GaspAnimator::ue_to_godot_quat);
	ClassDB::bind_static_method("GaspAnimator", D_METHOD("godot_to_ue_quat", "q"), &GaspAnimator::godot_to_ue_quat);
}

Vector3 GaspAnimator::ue_to_godot_pos(const Vector3& v) { return Vector3(v.x, v.z, v.y) * 0.01f; }
Vector3 GaspAnimator::godot_to_ue_pos(const Vector3& v) { return Vector3(v.x, v.z, v.y) * 100.f; }
Quaternion GaspAnimator::ue_to_godot_quat(const Quaternion& q) { return Quaternion(-q.x, -q.z, -q.y, q.w); }
Quaternion GaspAnimator::godot_to_ue_quat(const Quaternion& q) { return Quaternion(-q.x, -q.z, -q.y, q.w); }

bool GaspAnimator::initialize(const String& data_dir) {
	String Abs = ProjectSettings::get_singleton()->globalize_path(data_dir);
	const std::string Dir = StdS(Abs);
	if (!AnimLibrary::Get().Load(Dir)) return false;
	if (!MotionMatchingLibrary::Get().Load(Dir)) return false;
	if (!BlendSpaceLibrary::Get().Load(Dir)) UtilityFunctions::push_warning("gasp: blendspaces.json missing");
	Graph.Init();
	bReady = true;
	return true;
}

void GaspAnimator::reset(const Vector3& comp_pos, const Quaternion& comp_rot) {
	if (!bReady) return;
	Graph.Reset(ue::Xform(Q(comp_rot).GetNormalized(), V(comp_pos)));
}

static OffsetRootMode ParseOffsetMode(const Variant& v, OffsetRootMode Def) {
	if (v.get_type() == Variant::NIL) return Def;
	const String s = v;
	if (s == "Accumulate") return OffsetRootMode::Accumulate;
	if (s == "Interpolate") return OffsetRootMode::Interpolate;
	if (s == "Release") return OffsetRootMode::Release;
	if (s == "LockOffsetAndConsumeAnimation") return OffsetRootMode::LockOffsetAndConsumeAnimation;
	if (s == "LockOffsetIncreaseAndConsumeAnimation") return OffsetRootMode::LockOffsetIncreaseAndConsumeAnimation;
	if (s == "LockOffsetAndIgnoreAnimation") return OffsetRootMode::LockOffsetAndIgnoreAnimation;
	return Def;
}

void GaspAnimator::update(double delta, const Dictionary& in) {
	if (!bReady) return;
	GraphInputs G;
	G.ComponentTransform = ue::Xform(Q(in.get("comp_rot", Quaternion())).GetNormalized(), V(in.get("comp_pos", Vector3())));
	PackedFloat32Array T = in.get("traj", PackedFloat32Array());
	for (int i = 0; i + 7 < T.size(); i += 8) {
		TrajectorySample S;
		S.AccumulatedSeconds = T[i];
		S.Position = ue::Vec3(T[i + 1], T[i + 2], T[i + 3]);
		S.Facing = ue::Quat(T[i + 4], T[i + 5], T[i + 6], T[i + 7]).GetNormalized();
		G.Traj.Samples.push_back(S);
	}
	PackedStringArray Dbs = in.get("databases", PackedStringArray());
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	for (int i = 0; i < Dbs.size(); ++i) {
		const int d = MML.FindDatabase(StdS(Dbs[i]));
		if (d >= 0 && std::find(G.Databases.begin(), G.Databases.end(), d) == G.Databases.end()) G.Databases.push_back(d);
	}
	G.Interrupt = (InterruptMode)(int)in.get("interrupt", 0);
	G.MMBlendTime = (float)(double)in.get("mm_blend_time", 0.5);
	G.NotifyRecencyTimeOut = (float)(double)in.get("notify_recency_timeout", 0.2);
	G.bFootPlacementEnabled = (bool)in.get("foot_placement", true);
	G.FootChar.bWalkingWithFloor = (bool)in.get("fp_walking", true);
	G.FootChar.bHasFloor = (bool)in.get("fp_floor_hit", true);
	G.FootChar.FloorLocation = V(in.get("fp_floor_location", Vector3()));
	G.FootChar.FloorNormal = V(in.get("fp_floor_normal", Vector3(0, 0, 1)));
	G.FootChar.Velocity = V(in.get("velocity", Vector3()));
	G.LocomotionDirection = V(in.get("locomotion_dir", Vector3()));
	G.DesiredFacing = Q(in.get("desired_facing", Quaternion())).GetNormalized();
	G.bEnableSteering = (bool)in.get("enable_steering", false);
	G.bTurnInPlaceSteering = std::find(Graph.CurrentDatabaseTags.begin(), Graph.CurrentDatabaseTags.end(), "TurnInPlace") != Graph.CurrentDatabaseTags.end();
	G.OffsetTranslationMode = ParseOffsetMode(in.get("offset_translation_mode", Variant()), OffsetRootMode::Interpolate);
	G.OffsetRotationMode = ParseOffsetMode(in.get("offset_rotation_mode", Variant()), OffsetRootMode::Accumulate);
	G.OffsetTranslationHalflife = (float)(double)in.get("offset_translation_halflife", 0.2);
	G.OffsetRotationHalflife = (float)(double)in.get("offset_rotation_halflife", 0.1);
	G.OffsetMaxTranslationError = (float)(double)in.get("offset_max_translation", 30.0);
	G.bOffsetClampToTranslationVelocity = (bool)in.get("offset_clamp_velocity", false);
	G.bOffsetRootEnabled = (bool)in.get("offset_root_enabled", true);
	if (in.has("interaction_pos")) {
		G.Overrides.bHasInteraction = true;
		G.Overrides.InteractionWorld = ue::Xform(Q(in.get("interaction_rot", Quaternion())).GetNormalized(), V(in["interaction_pos"]));
	}
	G.bIsMoving = (bool)in.get("is_moving", false);
	G.DebugDisable = (int)in.get("debug_disable", 0);
	G.LeanAmount = (float)(double)in.get("lean", 0.0);
	G.bEnableAO = (bool)in.get("enable_ao", false);
	G.AOYaw = (float)(double)in.get("ao_yaw", 0.0);
	G.AOPitch = (float)(double)in.get("ao_pitch", 0.0);
	G.bUseStateMachine = (bool)in.get("use_sm", false);
	if (G.bUseStateMachine) {
		const String SA = in.get("sm_anim", String());
		G.SMAnim = SA.is_empty() ? -1 : AnimLibrary::Get().FindAnim(StdS(SA));
		G.SMStartTime = (float)(double)in.get("sm_start_time", 0.0);
		G.bSMLoop = (bool)in.get("sm_loop", true);
		G.SMBlendTime = (float)(double)in.get("sm_blend_time", 0.2);
		G.SMBlendProfile = StdS(String(in.get("sm_blend_profile", String())));
		G.bSMForceBlend = (bool)in.get("sm_force_blend", false);
		G.bSMStops = (bool)in.get("sm_stops", false);
		G.Speed2D = (float)(double)in.get("speed_2d", 0.0);
		// Steering_2 alpha comes from the Enable_TurnInPlaceSteering curve of the state machine blend stack
	}
	Graph.Update((float)delta, G);
}

void GaspAnimator::apply_to_skeleton(Skeleton3D* skeleton) {
	if (!bReady || !skeleton) return;
	const AnimLibrary& Lib = AnimLibrary::Get();
	if (skeleton != LastSkeleton) {
		SkeletonMap.assign(Lib.NumBones(), -1);
		for (int b = 0; b < Lib.NumBones(); ++b) SkeletonMap[b] = skeleton->find_bone(String(Lib.BoneNames[b].c_str()));
		LastSkeleton = skeleton;
	}
	// post-process anim blueprint (ABP_UEFN_Mannequin_PostProcess: ModifyBone scale replace)
	std::vector<ue::Xform> Local = Graph.Output.Bones;
	ApplyComponentSpaceScaleReplace(Local, Lib.Parents, PostScales);
	for (int b = 0; b < Lib.NumBones(); ++b) {
		const int sb = SkeletonMap[b];
		if (sb < 0) continue;
		const ue::Xform& X = Local[b];
		skeleton->set_bone_pose_position(sb, Vector3(X.Translation.X, -X.Translation.Y, X.Translation.Z) * 0.01f);
		skeleton->set_bone_pose_rotation(sb, Quaternion(-X.Rotation.X, X.Rotation.Y, -X.Rotation.Z, X.Rotation.W).normalized());
		skeleton->set_bone_pose_scale(sb, Vector3(X.Scale3D.X, X.Scale3D.Y, X.Scale3D.Z)); // the Y reflection keeps per-axis scale
	}
}

double GaspAnimator::get_curve(const String& name) const {
	const int c = AnimLibrary::Get().FindCurve(StdS(name));
	return Graph.Output.GetCurve(c);
}

Dictionary GaspAnimator::get_state() const {
	Dictionary D;
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	const AnimLibrary& Lib = AnimLibrary::Get();
	D["database"] = Graph.CurrentDatabase >= 0 ? String(MML.Db(Graph.CurrentDatabase).Name.c_str()) : String();
	const int A = Graph.Stack.GetAnim();
	D["anim"] = A >= 0 ? String(Lib.Clip(A).Name.c_str()) : String();
	D["anim_time"] = Graph.Stack.GetAccumulatedTime();
	D["anim_length"] = Graph.Stack.GetCurrentAssetLength();
	D["anim_looping"] = Graph.Stack.IsCurrentAssetLooping();
	D["cost"] = Graph.MM.LastSearchCost;
	D["play_rate"] = Graph.MM.WantedPlayRate;
	D["blend_stack"] = (int)Graph.Stack.Players.size();
	{
		String PS;
		for (const BlendStackPlayer& P : Graph.Stack.Players)
			PS += String("[{0} t={1} blend={2}/{3} w={4}]").format(Array::make(P.Anim, P.AccumulatedTime, P.CurrentBlendInTime, P.TotalBlendInTime, P.BlendWeight));
		D["players"] = PS;
		D["mm_blend_time_in"] = Graph.GetInputs().MMBlendTime;
	}
	D["jumped"] = Graph.MM.bJumpedToPose;
	{
		PackedFloat32Array Q, P;
		for (float v : Graph.MM.DebugQuery) Q.push_back(v);
		for (float v : Graph.MM.DebugPose) P.push_back(v);
		D["query"] = Q;
		D["pose_features"] = P;
		D["schema"] = Graph.MM.DebugSchema;
		D["continuing_cost"] = Graph.MM.DebugContinuingCost;
		D["can_advance"] = Graph.MM.DebugCanAdvance;
		String DC;
		for (const auto& dc : Graph.MM.DebugDbCosts) DC += String("{0}={1} ").format(Array::make(String(MML.Db(dc.first).Name.c_str()).replace("PSD_Dense_", ""), dc.second));
		D["db_costs"] = DC;
	}
	const int SA = Graph.SMStack.GetAnim();
	D["sm_anim"] = SA >= 0 ? String(Lib.Clip(SA).Name.c_str()) : String();
	D["sm_time"] = Graph.SMStack.GetAccumulatedTime();
	D["sm_length"] = Graph.SMStack.GetCurrentAssetLength();
	D["sm_looping"] = Graph.SMStack.IsCurrentAssetLooping();
	D["sm_blend_stack"] = (int)Graph.SMStack.Players.size();
	D["sm_play_rate"] = Graph.SMStack.Players.empty() ? 1.0 : (double)Graph.SMStack.Players[0].PlayRate;
	return D;
}

Transform3D GaspAnimator::get_root_offset_ue() const {
	const ue::Xform& X = Graph.SimulatedRoot;
	return Transform3D(Basis(GQ(X.Rotation)), GV(X.Translation));
}

GaspAnimator::~GaspAnimator() {
	if (TraceShape.is_valid() && PhysicsServer3D::get_singleton()) PhysicsServer3D::get_singleton()->free_rid(TraceShape);
}

void GaspAnimator::set_physics_space(const RID& space, const RID& exclude) {
	TraceSpace = space;
	TraceExclude = exclude;
	PhysicsServer3D* PS = PhysicsServer3D::get_singleton();
	if (!TraceShape.is_valid()) {
		TraceShape = PS->sphere_shape_create();
		PS->shape_set_data(TraceShape, 0.05);
	}
	Graph.FootTrace = [this](const ue::Vec3& S, const ue::Vec3& E, double R, ue::Vec3& P, ue::Vec3& N) { return FootTrace(S, E, R, P, N); };
}

// UWorld::SweepSingleByChannel(sphere) on the visibility channel, in UE space
bool GaspAnimator::FootTrace(const ue::Vec3& Start, const ue::Vec3& End, double Radius, ue::Vec3& OutP, ue::Vec3& OutN) {
	if (!TraceSpace.is_valid()) return false;
	PhysicsServer3D* PS = PhysicsServer3D::get_singleton();
	PhysicsDirectSpaceState3D* DS = PS->space_get_direct_state(TraceSpace);
	if (!DS) return false;
	PS->shape_set_data(TraceShape, Radius * 0.01);
	const Vector3 S = ue_to_godot_pos(GV(Start)), E = ue_to_godot_pos(GV(End));
	Ref<PhysicsShapeQueryParameters3D> Q3;
	Q3.instantiate();
	Q3->set_shape_rid(TraceShape);
	Q3->set_transform(Transform3D(Basis(), S));
	Q3->set_motion(E - S);
	Q3->set_collision_mask(1);
	TypedArray<RID> Ex;
	if (TraceExclude.is_valid()) Ex.push_back(TraceExclude);
	Q3->set_exclude(Ex);
	const PackedFloat32Array R = DS->cast_motion(Q3);
	if (R.size() < 2 || R[1] >= 1.0f) return false;
	Q3->set_transform(Transform3D(Basis(), S + (E - S) * R[1]));
	Q3->set_motion(Vector3());
	const Dictionary Info = DS->get_rest_info(Q3);
	if (Info.is_empty()) {
		Ref<PhysicsRayQueryParameters3D> RQ = PhysicsRayQueryParameters3D::create(S, E, 1, Ex);
		const Dictionary H = DS->intersect_ray(RQ);
		if (H.is_empty()) return false;
		OutP = V(godot_to_ue_pos(H["position"]));
		OutN = V(Vector3(((Vector3)H["normal"]).x, ((Vector3)H["normal"]).z, ((Vector3)H["normal"]).y));
		return true;
	}
	OutP = V(godot_to_ue_pos(Info["point"]));
	const Vector3 GN = Info["normal"];
	OutN = ue::Vec3(GN.x, GN.z, GN.y).GetSafeNormal();
	return true;
}

void GaspAnimator::set_post_process_scales(const Dictionary& scales) {
	PostScales.clear();
	const Array K = scales.keys();
	for (int i = 0; i < K.size(); ++i) {
		const Vector3 s = scales[K[i]];
		PostScales.push_back({ AnimLibrary::Get().FindBone(StdS(K[i])), ue::Vec3(s.x, s.y, s.z) });
	}
}

Dictionary GaspAnimator::get_foot_placement_debug() const {
	Dictionary D;
	const FootPlacement& F = Graph.Feet;
	Array Legs;
	for (int i = 0; i < 2; ++i) {
		Dictionary L;
		L["input"] = GV(F.Debug[i].InputFootWS);
		L["output"] = GV(F.Debug[i].OutputFootWS);
		L["plant"] = (int)F.Debug[i].Plant;
		L["speed"] = F.Debug[i].Speed;
		L["alignment"] = F.Debug[i].AlignmentAlpha;
		Legs.push_back(L);
	}
	D["legs"] = Legs;
	D["pelvis_in"] = GV(F.DebugPelvisInWS);
	D["pelvis_out"] = GV(F.DebugPelvisOutWS);
	return D;
}

static Dictionary NotifyDict(const FiredNotify& F) {
	Dictionary D;
	D["name"] = String(F.Event->Name.c_str());
	D["class"] = String(F.Event->Class.c_str());
	D["side"] = String(F.Event->Side.c_str());
	D["weight"] = F.Weight;
	D["montage"] = F.bMontage;
	return D;
}

Array GaspAnimator::get_fired_notifies() const {
	Array R;
	for (const FiredNotify& F : Graph.MMNotifies) R.push_back(NotifyDict(F));
	for (const FiredNotify& F : Graph.Montage.Fired) R.push_back(NotifyDict(F));
	return R;
}

PackedStringArray GaspAnimator::get_database_tags() const {
	PackedStringArray R;
	for (const auto& t : Graph.CurrentDatabaseTags) R.push_back(String(t.c_str()));
	return R;
}

Transform3D GaspAnimator::get_bone_cs_ue(const String& bone) const {
	const int b = AnimLibrary::Get().FindBone(StdS(bone));
	if (b < 0 || b >= (int)Graph.OutputCS.size()) return Transform3D();
	const ue::Xform& X = Graph.OutputCS[b];
	return Transform3D(Basis(GQ(X.Rotation)), GV(X.Translation));
}

static int FindMontageByName(const String& Name) {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const std::string N = StdS(Name);
	const int Direct = Lib.FindMontage(N);
	if (Direct >= 0) return Direct;
	for (int i = 0; i < Lib.NumMontages(); ++i) {
		const std::string& P = Lib.Montage(i).Path;
		const size_t Slash = P.rfind('/');
		if (P.compare(Slash == std::string::npos ? 0 : Slash + 1, std::string::npos, N) == 0) return i;
	}
	return -1;
}

bool GaspAnimator::montage_play(const String& montage, double play_rate, double start_position) {
	if (!bReady) return false;
	const int M = FindMontageByName(montage);
	if (M < 0 || AnimLibrary::Get().Montage(M).Anim < 0) return false;
	Graph.Montage.Play(M, (float)play_rate, (float)start_position);
	return true;
}

void GaspAnimator::montage_stop(double blend_time) {
	if (!bReady) return;
	Graph.Montage.bInterrupted = true;
	Graph.Montage.Stop((float)blend_time, BlendOption::HermiteCubic, nullptr);
}

Dictionary GaspAnimator::montage_tick(double delta, const Dictionary& o) {
	Dictionary R;
	if (!bReady) return R;
	WarpOwnerState S;
	const ue::Quat MeshQ = Q(o.get("mesh_rot", Quaternion())).GetNormalized();
	const ue::Vec3 MeshP = V(o.get("mesh_pos", Vector3()));
	const ue::Quat ActorQ = Q(o.get("actor_rot", Quaternion())).GetNormalized();
	const ue::Vec3 ActorP = V(o.get("actor_pos", Vector3()));
	S.MeshTransform = ue::Xform(MeshQ, MeshP);
	S.ActorQuat = ActorQ;
	S.VisualRootLocation = V(o.get("visual_root", Vector3()));
	S.MeshRotationOffset = (ActorQ.Inverse() * MeshQ).GetNormalized();
	S.bHasAcceleration = (bool)o.get("has_acceleration", false);
	S.bIsFalling = (bool)o.get("is_falling", false);
	bool bHasRM = false;
	const ue::Xform RM = Graph.Montage.Advance((float)delta, S, bHasRM);
	R["root_motion"] = bHasRM;
	if (bHasRM) {
		// USkeletalMeshComponent::ConvertLocalRootMotionToWorld
		const ue::Xform ActorToWorld(ActorQ, ActorP);
		const ue::Xform ComponentToActor = ActorToWorld.GetRelativeTransform(S.MeshTransform);
		const ue::Xform NewComponentToWorld = RM * S.MeshTransform;
		const ue::Xform NewActorTransform = ComponentToActor * NewComponentToWorld;
		const ue::Vec3 DeltaWorldTranslation = NewActorTransform.Translation - ActorToWorld.Translation;
		const ue::Quat NewWorldRotation = S.MeshTransform.Rotation * RM.Rotation;
		const ue::Quat DeltaWorldRotation = (NewWorldRotation * S.MeshTransform.Rotation.Inverse()).GetNormalized();
		R["delta_pos"] = GV(DeltaWorldTranslation);
		R["delta_rot"] = GQ(DeltaWorldRotation);
	}
	R["blend_out_started"] = Graph.Montage.bBlendOutStartedThisFrame;
	R["ended"] = Graph.Montage.bEndedThisFrame;
	R["interrupted"] = Graph.Montage.bInterrupted;
	R["playing"] = Graph.Montage.bPlaying;
	R["weight"] = Graph.Montage.GetWeight();
	return R;
}

Dictionary GaspAnimator::montage_state() const {
	Dictionary D;
	const MontagePlayer& M = Graph.Montage;
	D["playing"] = M.bPlaying;
	// UAnimInstance::IsSlotActive: some montage instance on the slot is active (FAnimMontageInstance::IsActive = not
	// stopped), i.e. false as soon as the montage starts blending out, although it still has pose weight
	D["slot_active"] = M.bPlaying && !M.bStopping;
	D["slot_weight"] = M.IsSlotActive() ? std::max(M.GetWeight(), M.GetOutgoingWeight()) : 0.f;
	D["stopping"] = M.bStopping;
	D["weight"] = M.GetWeight();
	D["position"] = M.Position;
	D["montage"] = M.Montage >= 0 ? String(AnimLibrary::Get().Montage(M.Montage).Path.c_str()) : String();
	return D;
}

void GaspAnimator::set_warp_target(const String& name, const Vector3& pos, const Quaternion& rot) {
	Graph.Montage.AddOrUpdateWarpTarget(StdS(name), V(pos), Q(rot).GetNormalized());
}

void GaspAnimator::remove_warp_target(const String& name) {
	Graph.Montage.RemoveWarpTarget(StdS(name));
}

// UPoseSearchLibrary::IsAnimationAssetLooping
bool GaspAnimator::is_anim_looping(const String& anim) const {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const int A = Lib.FindAnim(StdS(anim));
	return A >= 0 && Lib.Clip(A).bLoop;
}

// UAnimationWarpingLibrary::GetCurveValueFromAnimation (OutValue)
double GaspAnimator::get_anim_curve(const String& anim, const String& curve, double time) const {
	const AnimLibrary& Lib = AnimLibrary::Get();
	const int A = Lib.FindAnim(StdS(anim));
	if (A < 0) return 0.0;
	return Lib.SampleCurve(A, Lib.FindCurve(StdS(curve)), (float)time);
}

// UPoseSearchLibrary::MotionMatch(AssetsToSearch, PoseHistoryName 'PoseHistory') as called by SetBlendStackAnimFromChooser
// databases[i]: the PoseSearchBranchIn database of anims[i]
Dictionary GaspAnimator::motion_match_anims(const PackedStringArray& anims, const PackedStringArray& databases) {
	Dictionary R;
	R["success"] = false;
	if (!bReady) return R;
	const AnimLibrary& Lib = AnimLibrary::Get();
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	std::map<int, std::vector<int>> ByDb;
	for (int i = 0; i < anims.size() && i < databases.size(); ++i) {
		const int a = Lib.FindAnim(StdS(anims[i]));
		const int d = MML.FindDatabase(StdS(databases[i]));
		if (a >= 0 && d >= 0) ByDb[d].push_back(a);
	}
	SearchResult Best;
	for (auto& KV : ByDb) {
		const SearchResult Res = MotionMatchAnimAssets(KV.second, { KV.first }, &Graph.History);
		if (Res.IsValid() && Res.PoseCost < Best.PoseCost) Best = Res;
	}
	if (!Best.IsValid()) return R;
	const DbAsset* A = GetSearchIndexAsset(Best);
	if (!A || A->Anim < 0) return R;
	R["success"] = true;
	R["anim"] = String(Lib.Clip(A->Anim).Name.c_str());
	R["time"] = Best.AssetTime;
	R["cost"] = Best.PoseCost;
	R["database"] = String(MML.Db(Best.Database).Name.c_str());
	return R;
}

Dictionary GaspAnimator::motion_match_montages(const PackedStringArray& montages, const Vector3& interaction_pos, const Quaternion& interaction_rot) {
	Dictionary R;
	if (!bReady) return R;
	const MotionMatchingLibrary& MML = MotionMatchingLibrary::Get();
	std::vector<int> Ms;
	for (int i = 0; i < montages.size(); ++i) {
		const int m = FindMontageByName(montages[i]);
		if (m >= 0) Ms.push_back(m);
	}
	QueryOverrides O;
	O.bHasInteraction = true;
	O.InteractionWorld = ue::Xform(Q(interaction_rot).GetNormalized(), V(interaction_pos));
	SearchResult Best;
	// PoseSearchLibrary::MotionMatch searches the databases referenced by the assets (PSD_Traversal)
	for (int d = 0; d < MML.NumDatabases(); ++d) {
		bool bUses = false;
		for (const DbAsset& A : MML.Db(d).Assets) for (int m : Ms) bUses |= (A.Montage == m);
		if (!bUses) continue;
		SearchResult Res = MotionMatchAssets(d, Ms, &Graph.History, &O);
		if (Res.IsValid() && Res.PoseCost < Best.PoseCost) Best = Res;
	}
	if (!Best.IsValid()) return R;
	const DbAsset* A = GetSearchIndexAsset(Best);
	if (!A || A->Montage < 0) return R;
	R["montage"] = String(AnimLibrary::Get().Montage(A->Montage).Path.c_str());
	R["time"] = Best.AssetTime;
	R["play_rate"] = 1.0;
	R["cost"] = Best.PoseCost;
	R["database"] = String(MML.Db(Best.Database).Name.c_str());
	return R;
}

double GaspAnimator::get_montage_curve(const String& montage, const String& curve, double time) const {
	const int M = FindMontageByName(montage);
	if (M < 0) return 0.0;
	const AnimLibrary& Lib = AnimLibrary::Get();
	return Lib.SampleCurve(Lib.Montage(M).Anim, Lib.FindCurve(StdS(curve)), (float)time);
}

Array GaspAnimator::get_montage_warp_windows(const String& montage, const String& target) const {
	Array R;
	const int M = FindMontageByName(montage);
	if (M < 0) return R;
	const std::string T = StdS(target);
	for (const WarpWindow& W : AnimLibrary::Get().Montage(M).Warps) {
		if (W.Target == T) { Array w; w.push_back(W.Start); w.push_back(W.End); R.push_back(w); }
	}
	return R;
}

} // namespace godot
