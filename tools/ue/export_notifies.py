import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Exports every notify (time, duration, class, key properties) for UEFN anim sequences and montages.
import unreal, json, re

OUT = REPO + r"/tools/ue/out/notifies.json"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game/Characters/UEFN_Mannequin", recursive=True)
AL = unreal.AnimationLibrary
PROPS = ["cost_addend", "b_use_mirroring", "early_transition_destination", "transition_condition", "blend_out_condition",
         "blend_out_time", "blend_profile", "warp_target_name", "event", "side", "volume_multiplier", "pitch_multiplier",
         "b_warp_translation", "b_ignore_z_axis", "b_warp_rotation", "rotation_type", "warp_rotation_time_multiplier",
         "warp_point_anim_provider", "warp_point_anim_bone_name", "warp_point_anim_transform", "rotation_method",
         "warp_max_rotation_rate", "foley_event", "b_flip_tag_if_mirrored", "tag"]

def props(o):
    d = {"class": o.get_class().get_name()}
    for p in PROPS:
        try:
            v = o.get_editor_property(p)
        except Exception:
            continue
        if isinstance(v, (int, float, bool, str)):
            d[p] = v
        else:
            d[p] = str(v)
    # motion warping root motion modifier
    try:
        rmm = o.get_editor_property("root_motion_modifier")
        if rmm:
            dd = {"class": rmm.get_class().get_name()}
            for p in PROPS:
                try:
                    v = rmm.get_editor_property(p)
                    dd[p] = v if isinstance(v, (int, float, bool, str)) else str(v)
                except Exception:
                    pass
            d["modifier"] = dd
    except Exception:
        pass
    return d

out = {}
for a in assets:
    cls = str(a.asset_class_path.asset_name)
    if cls not in ("AnimSequence", "AnimMontage"):
        continue
    path = str(a.package_name)
    anim = unreal.load_asset(path)
    evs = []
    for e in AL.get_animation_notify_events(anim):
        txt = e.export_text()
        start = float(re.findall(r"LinkValue=([-0-9.]+)", txt)[-1])
        dur = float(re.search(r"Duration=([-0-9.]+)", txt).group(1))
        name = re.search(r'NotifyName="([^"]*)"', txt).group(1)
        item = {"name": name, "time": start, "duration": dur}
        for k in ("notify", "notify_state_class"):
            try:
                o = e.get_editor_property(k)
            except Exception as ex:
                item[k + "_err"] = str(ex)
                continue
            if o:
                item[k] = props(o)
        evs.append(item)
    out[path] = evs
with open(OUT, "w") as f:
    json.dump(out, f, indent=0)
