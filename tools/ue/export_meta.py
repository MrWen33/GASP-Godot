import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, os, json
ROOT = REPO + r"/tools/ue/out"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)
# Per-animation metadata for UEFN mannequin sequences + montages
AL = unreal.AnimationLibrary
meta = {}
for a in assets:
    cls = str(a.asset_class_path.asset_name); path = str(a.package_name)
    if cls not in ("AnimSequence", "AnimMontage") or not path.startswith("/Game/Characters/UEFN_Mannequin"):
        continue
    anim = unreal.load_asset(path)
    m = {"class": cls, "length": anim.get_editor_property("sequence_length") if cls == "AnimSequence" else anim.get_play_length()}
    if cls == "AnimSequence":
        try:
            fr = AL.get_frame_rate(anim); m["fps"] = fr.numerator / fr.denominator
        except Exception as e: m["fps_err"] = str(e)
        m["num_frames"] = AL.get_num_frames(anim)
        m["root_motion"] = anim.get_editor_property("enable_root_motion")
        m["force_root_lock"] = anim.get_editor_property("force_root_lock")
        m["loop"] = anim.get_editor_property("loop") if hasattr(anim, "loop") else None
        curves = {}
        for cn in AL.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT):
            times, vals = AL.get_float_keys(anim, cn)
            curves[str(cn)] = [list(times), list(vals)]
        m["curves"] = curves
        try:
            m["sync_markers"] = [[str(s.marker_name), s.time] for s in AL.get_animation_sync_markers(anim)]
        except Exception as e: m["sync_err"] = str(e)
    ev = []
    for e in AL.get_animation_notify_events(anim):
        ev.append(e.export_text())
    m["notifies"] = ev
    meta[path] = m
with open(ROOT + "/anim_meta.json", "w") as f:
    json.dump(meta, f, indent=0)
unreal.log("META DONE %d" % len(meta))
