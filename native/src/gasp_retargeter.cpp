#include "gasp_retargeter.h"
#include "gasp_animator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace gasp;

namespace godot {

static std::string S(const Variant& v) { return std::string(String(v).utf8().get_data()); }
static ue::Vec3 V3(const Variant& v) { Array a = v; return a.size() >= 3 ? ue::Vec3((double)a[0], (double)a[1], (double)a[2]) : ue::Vec3(); }
static ue::Quat Q4(const Variant& v) { Array a = v; return a.size() >= 4 ? ue::Quat((double)a[0], (double)a[1], (double)a[2], (double)a[3]).GetNormalized() : ue::Quat(); }
static ue::Quat Rot(const Variant& v) { Array a = v; return a.size() >= 3 ? ue::Quat::MakeFromEulerPYR((double)a[0], (double)a[1], (double)a[2]) : ue::Quat(); }

static pbik::ELimitType Limit(const Variant& v) {
	const String s = v;
	if (s == "Limited") return pbik::ELimitType::Limited;
	if (s == "Locked") return pbik::ELimitType::Locked;
	return pbik::ELimitType::Free;
}

static IKRigDef ParseRig(const Dictionary& d) {
	IKRigDef R;
	R.RetargetRoot = S(d.get("root", "None"));
	Array chains = d.get("chains", Array());
	for (int i = 0; i < chains.size(); ++i) {
		Dictionary c = chains[i];
		R.Chains.push_back({ S(c["name"]), S(c["start"]), S(c["end"]), S(c.get("goal", "None")) });
	}
	Dictionary goals = d.get("goals", Dictionary());
	Array gk = goals.keys();
	for (int i = 0; i < gk.size(); ++i) R.GoalBones[S(gk[i])] = S(((Dictionary)goals[gk[i]])["bone"]);
	if (d.get("solver", Variant()).get_type() == Variant::DICTIONARY) {
		Dictionary s = d["solver"];
		R.bHasSolver = true;
		R.SolverRoot = S(s["root"]);
		R.Solver.Iterations = (int)s.get("iterations", 20);
		R.Solver.SubIterations = (int)s.get("sub_iterations", 0);
		R.Solver.MassMultiplier = (float)(double)s.get("mass_multiplier", 1.0);
		R.Solver.bAllowStretch = (bool)s.get("allow_stretch", false);
		const String rb = s.get("root_behavior", "PrePull");
		R.Solver.RootBehavior = rb == "PinToInput" ? pbik::ERootBehavior::PinToInput : (rb == "Free" ? pbik::ERootBehavior::Free : pbik::ERootBehavior::PrePull);
		R.Solver.GlobalPullChainAlpha = (float)(double)s.get("pull_chain_alpha", 1.0);
		R.Solver.MaxAngle = (float)(double)s.get("max_angle", 30.0);
		R.Solver.OverRelaxation = (float)(double)s.get("over_relaxation", 1.3);
	}
	Array effs = d.get("effectors", Array());
	for (int i = 0; i < effs.size(); ++i) {
		Dictionary e = effs[i];
		IKRigEffectorDef E;
		E.Goal = S(e["goal"]); E.Bone = S(e["bone"]);
		E.ChainDepth = (int)e.get("chain_depth", 0);
		E.Strength = (float)(double)e.get("strength", 1.0);
		E.PullChain = (float)(double)e.get("pull_chain", 1.0);
		E.PinRotation = (float)(double)e.get("pin_rotation", 1.0);
		R.Effectors.push_back(E);
	}
	Array bss = d.get("bone_settings", Array());
	for (int i = 0; i < bss.size(); ++i) {
		Dictionary b = bss[i];
		IKRigBoneSettingDef B;
		B.Bone = S(b["bone"]);
		B.S.RotationStiffness = (float)(double)b.get("rotation_stiffness", 0.0);
		B.S.PositionStiffness = (float)(double)b.get("position_stiffness", 0.0);
		B.S.X = Limit(b.get("x", "Free")); B.S.MinX = (float)(double)b.get("min_x", 0.0); B.S.MaxX = (float)(double)b.get("max_x", 0.0);
		B.S.Y = Limit(b.get("y", "Free")); B.S.MinY = (float)(double)b.get("min_y", 0.0); B.S.MaxY = (float)(double)b.get("max_y", 0.0);
		B.S.Z = Limit(b.get("z", "Free")); B.S.MinZ = (float)(double)b.get("min_z", 0.0); B.S.MaxZ = (float)(double)b.get("max_z", 0.0);
		B.S.bUsePreferredAngles = (bool)b.get("use_preferred", false);
		const ue::Vec3 P = V3(b.get("preferred", Array()));
		// UIKRig_FBIKBoneSettings::CopyToCoreStruct: Pitch = Y, Yaw = Z, Roll = X
		B.S.PreferredAngles.Pitch = P.Y; B.S.PreferredAngles.Yaw = P.Z; B.S.PreferredAngles.Roll = P.X;
		R.BoneSettings.push_back(B);
	}
	Array ex = d.get("excluded", Array());
	for (int i = 0; i < ex.size(); ++i) R.Excluded.push_back(S(ex[i]));
	return R;
}

static RetargetPoseDef ParsePose(const Dictionary& d) {
	RetargetPoseDef P;
	P.RootOffset = V3(d.get("root_offset", Array()));
	Dictionary rots = d.get("rotations", Dictionary());
	Array k = rots.keys();
	for (int i = 0; i < k.size(); ++i) P.Rotations.push_back({ S(k[i]), Q4(rots[k[i]]) });
	return P;
}

static ERotationMode RotMode(const String& s) {
	if (s == "OneToOne") return ERotationMode::OneToOne;
	if (s == "OneToOneReversed") return ERotationMode::OneToOneReversed;
	if (s == "None") return ERotationMode::None;
	return ERotationMode::Interpolated;
}

static ETranslationMode TransMode(const String& s) {
	if (s == "GloballyScaled") return ETranslationMode::GloballyScaled;
	if (s == "Absolute") return ETranslationMode::Absolute;
	return ETranslationMode::None;
}

void GaspRetargeter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "rtg", "target"), &GaspRetargeter::setup);
	ClassDB::bind_method(D_METHOD("update", "animator", "target"), &GaspRetargeter::update);
	ClassDB::bind_method(D_METHOD("get_ik_goals"), &GaspRetargeter::get_ik_goals);
	ClassDB::bind_method(D_METHOD("set_post_process_scales", "scales"), &GaspRetargeter::set_post_process_scales);
}

bool GaspRetargeter::setup(const Dictionary& rtg, Skeleton3D* target) {
	bReady = false;
	if (!target) return false;
	const AnimLibrary& Lib = AnimLibrary::Get();
	// source: UEFN mannequin reference skeleton
	SourceDef.Names = Lib.BoneNames;
	SourceDef.Parents = Lib.Parents;
	SourceDef.RefLocal = Lib.RefPose;
	// target: Skeleton3D rest pose converted to UE local space (inverse of p'=(x,-y,z)*0.01, q'=(-x,y,-z,w))
	TargetDef = SkeletonDef();
	TargetToSkeleton.clear();
	const int N = target->get_bone_count();
	for (int b = 0; b < N; ++b) {
		TargetDef.Names.push_back(std::string(target->get_bone_name(b).utf8().get_data()));
		TargetDef.Parents.push_back(target->get_bone_parent(b));
		const Transform3D R = target->get_bone_rest(b);
		const Vector3 p = R.origin;
		const Quaternion q = R.basis.get_rotation_quaternion();
		const Vector3 s = R.basis.get_scale();
		TargetDef.RefLocal.push_back(ue::Xform(ue::Quat(-q.x, q.y, -q.z, q.w).GetNormalized(), ue::Vec3(p.x, -p.y, p.z) * 100.0, ue::Vec3(s.x, s.y, s.z)));
		TargetToSkeleton.push_back(b);
	}
	for (int b = 0; b < N; ++b)
		if (TargetDef.Parents[b] >= b) { UtilityFunctions::push_error("GaspRetargeter: target skeleton is not parent-before-child ordered"); return false; }
	RetargeterDef D;
	D.SourceRig = ParseRig(rtg["source_rig"]);
	D.TargetRig = ParseRig(rtg["target_rig"]);
	Dictionary rs = rtg.get("root_settings", Dictionary());
	D.Root.RotationAlpha = (double)rs.get("rotation_alpha", 1.0);
	D.Root.TranslationAlpha = (double)rs.get("translation_alpha", 1.0);
	D.Root.BlendToSource = (double)rs.get("blend_to_source", 0.0);
	D.Root.BlendToSourceWeights = V3(rs.get("blend_to_source_weights", Array::make(1, 1, 1)));
	D.Root.ScaleHorizontal = (double)rs.get("scale_horizontal", 1.0);
	D.Root.ScaleVertical = (double)rs.get("scale_vertical", 1.0);
	D.Root.TranslationOffset = V3(rs.get("translation_offset", Array()));
	D.Root.RotationOffset = Rot(rs.get("rotation_offset", Array()));
	D.Root.AffectIKHorizontal = (double)rs.get("affect_ik_horizontal", 1.0);
	D.Root.AffectIKVertical = (double)rs.get("affect_ik_vertical", 0.0);
	Dictionary gs = rtg.get("global_settings", Dictionary());
	D.bEnableRoot = (bool)gs.get("enable_root", true);
	D.bEnableFK = (bool)gs.get("enable_fk", true);
	D.bEnableIK = (bool)gs.get("enable_ik", true);
	Array chains = rtg.get("chains", Array());
	for (int i = 0; i < chains.size(); ++i) {
		Dictionary c = chains[i];
		ChainMapDef M;
		M.Source = S(c["source"]); M.Target = S(c["target"]);
		Dictionary fk = c["fk"], ik = c["ik"];
		M.FK.EnableFK = (bool)fk.get("enable", true);
		M.FK.RotationMode = RotMode(fk.get("rotation_mode", "Interpolated"));
		M.FK.RotationAlpha = (double)fk.get("rotation_alpha", 1.0);
		M.FK.TranslationMode = TransMode(fk.get("translation_mode", "None"));
		M.FK.TranslationAlpha = (double)fk.get("translation_alpha", 1.0);
		M.FK.PoleVectorMatching = (double)fk.get("pole_vector_matching", 0.0);
		M.FK.PoleVectorMaintainOffset = (bool)fk.get("pole_vector_maintain_offset", false);
		M.FK.PoleVectorOffset = (double)fk.get("pole_vector_offset", 0.0);
		M.IK.EnableIK = (bool)ik.get("enable", true);
		M.IK.BlendToSource = (double)ik.get("blend_to_source", 0.0);
		M.IK.BlendToSourceWeights = V3(ik.get("blend_to_source_weights", Array::make(1, 1, 1)));
		M.IK.StaticOffset = V3(ik.get("static_offset", Array()));
		M.IK.StaticLocalOffset = V3(ik.get("static_local_offset", Array()));
		M.IK.StaticRotationOffset = Rot(ik.get("static_rotation_offset", Array()));
		M.IK.ScaleVertical = (double)ik.get("scale_vertical", 1.0);
		M.IK.Extension = (double)ik.get("extension", 1.0);
		D.Chains.push_back(M);
	}
	D.SourcePose = ParsePose(rtg.get("source_pose", Dictionary()));
	D.TargetPose = ParsePose(rtg.get("target_pose", Dictionary()));
	bReady = Processor.Initialize(SourceDef, TargetDef, D);
	CurveHandL = Lib.FindCurve("Retarget_Hand_L_BlendToSource");
	CurveHandR = Lib.FindCurve("Retarget_Hand_R_BlendToSource");
	return bReady;
}

void GaspRetargeter::update(const Ref<GaspAnimator>& animator, Skeleton3D* target) {
	if (!bReady || animator.is_null() || !animator->is_ready() || !target) return;
	const AnimGraph& G = animator->graph();
	const Pose& Out = G.Output;
	const int NB = (int)SourceDef.Names.size();
	if ((int)Out.Bones.size() != NB) return;
	// CopyBoneTransformsFromSource: component space, scale stripped
	SourceCS.resize(NB);
	for (int b = 0; b < NB; ++b) {
		const int P = SourceDef.Parents[b];
		SourceCS[b] = P < 0 ? Out.Bones[b] : Out.Bones[b] * SourceCS[P];
	}
	for (ue::Xform& T : SourceCS) T.Scale3D = ue::Vec3(1, 1, 1);
	// ABP_GenericRetarget::GetRetargetProfile: arm IK blend-to-source driven by the Retarget_Hand_*_BlendToSource curves
	auto ApplyArm = [&](const char* Chain, int Curve) {
		const ChainMapDef* Asset = Processor.FindAssetChainSettings(Chain);
		ChainMapDef* Run = Processor.FindChainSettings(Chain);
		if (!Asset || !Run) return;
		const double A = (Curve >= 0 && Out.HasCurve(Curve)) ? Out.GetCurve(Curve) : 0.0;
		Run->FK = Asset->FK;
		Run->IK = Asset->IK;
		Run->IK.BlendToSource = Asset->IK.BlendToSource + (1.0 - Asset->IK.BlendToSource) * A;
		Run->IK.StaticLocalOffset = ue::Vec3::Lerp(Asset->IK.StaticLocalOffset, ue::Vec3(), A);
	};
	ApplyArm("LeftArm", CurveHandL);
	ApplyArm("RightArm", CurveHandR);
	const std::vector<ue::Xform>& R = Processor.RunRetargeter(SourceCS);
	// component -> local, then Godot bone pose (scale from the reference pose)
	const int NT = (int)TargetDef.Names.size();
	LocalOut.resize(NT);
	for (int b = 0; b < NT; ++b) {
		const int P = TargetDef.Parents[b];
		LocalOut[b] = P < 0 ? R[b] : R[b].GetRelativeTransform(R[P]);
		LocalOut[b].Scale3D = TargetDef.RefLocal[b].Scale3D;
	}
	ApplyComponentSpaceScaleReplace(LocalOut, TargetDef.Parents, PostScales);
	for (int b = 0; b < NT; ++b) {
		const ue::Xform& L = LocalOut[b];
		const int sb = TargetToSkeleton[b];
		target->set_bone_pose_position(sb, Vector3(L.Translation.X, -L.Translation.Y, L.Translation.Z) * 0.01f);
		target->set_bone_pose_rotation(sb, Quaternion(-L.Rotation.X, L.Rotation.Y, -L.Rotation.Z, L.Rotation.W).normalized());
		const ue::Vec3& Sc = L.Scale3D;
		target->set_bone_pose_scale(sb, Vector3(Sc.X, Sc.Y, Sc.Z));
	}
}

void GaspRetargeter::set_post_process_scales(const Dictionary& scales) {
	PostScales.clear();
	const Array K = scales.keys();
	for (int i = 0; i < K.size(); ++i) {
		const std::string Name = S(K[i]);
		int B = -1;
		for (int b = 0; b < (int)TargetDef.Names.size(); ++b) if (TargetDef.Names[b] == Name) B = b;
		const Vector3 s = scales[K[i]];
		if (B >= 0) PostScales.push_back({ B, ue::Vec3(s.x, s.y, s.z) });
	}
}

Dictionary GaspRetargeter::get_ik_goals() const {
	Dictionary D;
	return D;
}

} // namespace godot
