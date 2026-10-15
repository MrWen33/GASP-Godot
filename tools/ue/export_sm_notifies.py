import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Properties of the notify states used by the experimental state machine: BP_NotifyState_EarlyTransition
(TransitionCondition / TransitionDestination / GaitNotEqual) and AnimNotifyState_PoseSearchBranchIn (Database)."""
import unreal, json, re
OUT = REPO + r"/tools/ue/out/sm_notifies.json"
reg = unreal.AssetRegistryHelpers.get_asset_registry()
res = {}
log = []
for ad in reg.get_assets_by_path("/Game/Characters/UEFN_Mannequin/Animations", recursive=True):
    if str(ad.asset_class_path.asset_name) != "AnimSequence":
        continue
    seq = ad.get_asset()
    entries = []
    for ev in unreal.AnimationLibrary.get_animation_notify_events(seq):
        ns = ev.get_editor_property("notify_state_class")
        txt = ev.export_text()
        if ns is None:
            continue
        cls = ns.get_class().get_name()
        if cls not in ("BP_NotifyState_EarlyTransition_C", "AnimNotifyState_PoseSearchBranchIn"):
            continue
        e = {"class": cls, "time": float(re.findall(r"LinkValue=([-0-9.]+)", txt)[-1]), "duration": float(re.search(r"Duration=([-0-9.]+)", txt).group(1))}
        if cls == "AnimNotifyState_PoseSearchBranchIn":
            db = ns.get_editor_property("database")
            e["database"] = db.get_path_name() if db else None
        else:
            for p in ("TransitionCondition", "TransitionDestination", "GaitNotEqual"):
                try:
                    v = ns.get_editor_property(p)
                    e[p] = int(v) if not isinstance(v, (int, float)) and hasattr(v, "__int__") else (v if isinstance(v, (int, float)) else str(v))
                except Exception as ex:
                    e[p] = "ERR " + str(ex)
        entries.append(e)
    if entries:
        res[str(ad.package_name)] = entries
json.dump(res, open(OUT, "w"), indent=1)
