import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Export class defaults of the foley anim notify blueprints (Event tag, Side, Volume/Pitch multipliers)."""
import unreal, json

OUT = REPO + r"/godot/data/foley_notifies.json"
names = ["BP_AnimNotify_FoleyEvent", "BP_AnimNotify_FoleyEvent_Handplant_L", "BP_AnimNotify_FoleyEvent_Handplant_R",
         "BP_AnimNotify_FoleyEvent_Jump", "BP_AnimNotify_FoleyEvent_Land", "BP_AnimNotify_FoleyEvent_Run_L",
         "BP_AnimNotify_FoleyEvent_Run_R", "BP_AnimNotify_FoleyEvent_Scuff_L", "BP_AnimNotify_FoleyEvent_Scuff_R",
         "BP_AnimNotify_FoleyEvent_Walk_L", "BP_AnimNotify_FoleyEvent_Walk_R"]
res = {}
for n in names:
    try:
        cls = unreal.load_object(None, "/Game/Blueprints/AnimNotifies/%s.%s_C" % (n, n))
        cdo = unreal.get_default_object(cls)
        d = {}
        for p in ["Event", "Side", "VolumeMultiplier", "PitchMultiplier"]:
            try:
                v = cdo.get_editor_property(p)
                d[p] = str(v.tag_name) if hasattr(v, "tag_name") else (v if isinstance(v, (int, float)) else str(v))
            except Exception as e:
                d[p] = "ERR " + str(e)
        res[n + "_C"] = d
    except Exception as e:
        res[n] = str(e)
# roll notify
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for ad in reg.get_assets_by_path("/Game", recursive=True):
        if str(ad.asset_name) == "BP_FoleyEvent_Roll":
            res["BP_FoleyEvent_Roll_path"] = str(ad.package_name)
except Exception as e:
    res["roll_err"] = str(e)
open(OUT, "w").write(json.dumps(res, indent=1))
print("foley cdo done")
