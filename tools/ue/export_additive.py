import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, json
out = {}
reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/Characters/UEFN_Mannequin/Animations", recursive=True)
for ad in assets:
    if str(ad.asset_class_path.asset_name) != "AnimSequence":
        continue
    a = ad.get_asset()
    t = a.get_editor_property("additive_anim_type")
    if str(t) == "AdditiveAnimationType.AAT_NONE":
        continue
    seq = a.get_editor_property("ref_pose_seq")
    out[a.get_path_name().split(".")[0]] = {
        "additive_anim_type": str(t).split(".")[-1],
        "ref_pose_type": str(a.get_editor_property("ref_pose_type")).split(".")[-1],
        "ref_pose_seq": seq.get_path_name().split(".")[0] if seq else None,
        "ref_frame_index": a.get_editor_property("ref_frame_index"),
    }
json.dump(out, open(REPO + "/tools/ue/out/additive.json", "w"), indent=1)
