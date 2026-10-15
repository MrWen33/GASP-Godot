import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Inventory of the retargeted sandbox characters: SCS components of each CBP_SandboxCharacter_* child
(skeletal meshes, anim classes, transforms, material overrides), the ABP_GenericRetarget graph (T3D) and the
IK retargeter / IK rig assets."""
import unreal, json, os

OUT = REPO + r"/tools/ue/out/characters"
os.makedirs(OUT, exist_ok=True)
NAMES = ["Manny", "Quinn", "UE4_Mannequin", "Echo", "TwinBlast",
         "RetargetShowcase_Mannequins", "SetupComparison"]
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
PROPS = ["relative_location", "relative_rotation", "relative_scale3d", "skeletal_mesh_asset", "skeletal_mesh", "anim_class",
         "animation_mode", "visible", "hidden_in_game", "attach_socket_name", "override_materials", "leader_pose_component",
         "use_bounds_from_leader_pose_component", "component_tags", "skin_cache_usage", "visibility_based_anim_tick_option"]
res = {}
for n in NAMES:
    path = "/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_" + n
    bp = unreal.load_asset(path)
    if bp is None:
        res[n] = {"error": "missing"}
        continue
    comps = []
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        try:
            data = lib.get_data(h)
            obj = lib.get_object(data)
            if obj is None:
                continue
            d = {"name": str(lib.get_variable_name(data)), "class": obj.get_class().get_name()}
            try:
                ph = lib.get_data(data).get_editor_property("parent_object_handle") if False else None
            except Exception:
                pass
            try:
                ap = obj.get_attach_parent()
                d["attach_parent"] = ap.get_name() if ap else ""
            except Exception:
                pass
            for p in PROPS:
                try:
                    v = obj.get_editor_property(p)
                    if isinstance(v, unreal.Array):
                        v = [str(x.get_path_name()) if hasattr(x, "get_path_name") and x else str(x) for x in v]
                    elif hasattr(v, "get_path_name"):
                        v = v.get_path_name()
                    d[p] = str(v) if not isinstance(v, list) else v
                except Exception:
                    pass
            comps.append(d)
        except Exception as e:
            comps.append({"error": str(e)})
    res[n] = {"components": comps}
    try:
        unreal.AssetExportTask
        t = unreal.AssetExportTask()
        t.object = bp
        t.filename = OUT + "/CBP_" + n + ".t3d"
        t.exporter = None
        t.automated = True
        t.prompt = False
        t.replace_identical = True
        unreal.Exporter.run_asset_export_task(t)
    except Exception as e:
        res[n]["t3d_err"] = str(e)

# retarget anim blueprint + retargeters
for p in ["/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget"]:
    a = unreal.load_asset(p)
    t = unreal.AssetExportTask()
    t.object = a
    t.filename = OUT + "/" + p.split("/")[-1] + ".t3d"
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    unreal.Exporter.run_asset_export_task(t)

ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = []
for cls in ["IKRetargeter", "IKRigDefinition"]:
    for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/IKRig", cls)):
        assets.append({"class": cls, "path": str(ad.package_name)})
res["_ik_assets"] = assets
open(OUT + "/characters.json", "w").write(json.dumps(res, indent=1))
print("done", len(assets))
