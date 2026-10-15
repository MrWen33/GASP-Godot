import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Bakes every UEFN mannequin AnimSequence into raw local-space bone keys + per-key curve values (UE space, cm).
# Output: out/anims/anims.json (index) + out/anims/anims.bin (float32 blocks).
import unreal, json, os, struct, array

OUT = REPO + r"/tools/ue/out/anims"
os.makedirs(OUT, exist_ok=True)
LOG = []

ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/Characters/UEFN_Mannequin", recursive=True)
AP = unreal.AnimPoseExtensions
LOCAL = unreal.AnimPoseSpaces.LOCAL

opts = unreal.AnimPoseEvaluationOptions()
opts.set_editor_property("extract_root_motion", False)
opts.set_editor_property("evaluate_curves", True)
opts.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)

skel = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin")
ref = AP.get_reference_pose(skel)
bone_names = [str(b) for b in AP.get_bone_names(ref)]

def tr(t):
    q = t.rotation; p = t.translation; s = t.scale3d
    return (p.x, p.y, p.z, q.x, q.y, q.z, q.w, s.x, s.y, s.z)

index = {"bones": bone_names, "ref_local": [tr(AP.get_ref_bone_pose(ref, b, LOCAL)) for b in bone_names],
         "ref_world": [tr(AP.get_ref_bone_pose(ref, b, unreal.AnimPoseSpaces.WORLD)) for b in bone_names],
         "stride": 10, "anims": []}

binf = open(OUT + "/anims.bin", "wb")
offset = 0
count = 0
for a in assets:
    if str(a.asset_class_path.asset_name) != "AnimSequence":
        continue
    path = str(a.package_name)
    anim = unreal.load_asset(path)
    if anim.get_editor_property("skeleton") != skel:
        LOG.append("SKIP other skeleton " + path)
        continue
    length = anim.get_editor_property("sequence_length")
    nframes = unreal.AnimationLibrary.get_num_frames(anim)
    fps = nframes / length if length > 0 else 30.0
    nkeys = nframes + 1
    curve_names = [str(c) for c in unreal.AnimationLibrary.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT)]
    buf = array.array("f")
    cbuf = array.array("f")
    for k in range(nkeys):
        t = min(k / fps, length)
        pose = AP.get_anim_pose_at_time(anim, t, opts)
        for b in bone_names:
            buf.extend(tr(AP.get_bone_pose(pose, b, LOCAL)))
        for c in curve_names:
            cbuf.append(AP.get_curve_weight(pose, c))
    def prop(n, d=None):
        try: return anim.get_editor_property(n)
        except Exception: return d
    entry = {"path": path, "name": path.split("/")[-1], "length": length, "fps": fps, "num_keys": nkeys,
             "root_motion": prop("enable_root_motion"), "force_root_lock": prop("force_root_lock"),
             "loop": prop("loop"), "root_lock": str(prop("root_motion_root_lock")),
             "curves": curve_names, "offset": offset, "curve_offset": offset + len(buf) * 4}
    binf.write(buf.tobytes()); binf.write(cbuf.tobytes())
    offset += (len(buf) + len(cbuf)) * 4
    index["anims"].append(entry)
    count += 1
binf.close()
with open(OUT + "/anims.json", "w") as f:
    json.dump(index, f)
LOG.append("BAKED %d bytes %d" % (count, offset))
open(OUT + "/bake_log.txt", "w").write("\n".join(LOG))
