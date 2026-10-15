import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, time
_L = []
class _U:
    pass
_log = lambda s: _L.append(str(s))
anim = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Loop_F")
skel = anim.get_editor_property("skeleton")
opts = unreal.AnimPoseEvaluationOptions()
_log("OPTS " + str([d for d in dir(opts) if not d.startswith("_")]))
t0 = time.time()
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.5, opts)
names = unreal.AnimPoseExtensions.get_bone_names(pose)
_log("NBONES %d %s" % (len(names), names[:5]))
n = 0
for f in range(30):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, f / 30.0, opts)
    for b in names:
        t = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.LOCAL)
        n += 1
dt = time.time() - t0
_log("TIMING %d calls %.3fs" % (n, dt))
ref = unreal.AnimPoseExtensions.get_reference_pose(skel)
_log("REF root %s pelvis %s" % (unreal.AnimPoseExtensions.get_ref_bone_pose(ref, "root", unreal.AnimPoseSpaces.LOCAL), unreal.AnimPoseExtensions.get_ref_bone_pose(ref, "pelvis", unreal.AnimPoseSpaces.LOCAL)))
p0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.0, opts)
p1 = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 1.0, opts)
_log("ROOT0 %s" % unreal.AnimPoseExtensions.get_bone_pose(p0, "root", unreal.AnimPoseSpaces.LOCAL))
_log("ROOT1 %s" % unreal.AnimPoseExtensions.get_bone_pose(p1, "root", unreal.AnimPoseSpaces.LOCAL))
for f in ["get_parent_bone_name", "get_curve_weights", "get_curve_names"]:
    _log("HAS %s %s" % (f, hasattr(unreal.AnimPoseExtensions, f)))

open(REPO + r"/tools/ue/out/probe_pose.txt","w").write(chr(10).join(_L))
