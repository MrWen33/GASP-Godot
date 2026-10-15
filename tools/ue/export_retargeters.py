import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""T3D of IK retargeters / IK rigs + ABP_GenericRetarget CDO (IKRetargeter_Map) + per-CBP mesh component tags."""
import unreal, os, json

OUT = REPO + r"/tools/ue/out/characters/t3d"
os.makedirs(OUT, exist_ok=True)

def t3d(obj, name):
    t = unreal.AssetExportTask()
    t.object = obj
    t.filename = OUT + "/" + name + ".t3d"
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    t.exporter = unreal.ObjectExporterT3D()
    return unreal.Exporter.run_asset_export_task(t)

paths = ["/Game/Characters/Echo/Rigs/RTG_UEFN_to_Echo", "/Game/Characters/Paragon/Heroes/TwinBlast/Rigs/RTG_UEFN_to_TwinBlast",
         "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin", "/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin",
         "/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin",
         "/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget", "/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget",
         "/Game/Characters/Paragon/Heroes/TwinBlast/Rigs/IK_TwinBlast_Retarget", "/Game/Characters/Echo/Rigs/IK_Echo_Retarget"]
res = {}
for p in paths:
    a = unreal.load_asset(p)
    res[p] = bool(t3d(a, p.split("/")[-1]))

cdo = unreal.get_default_object(unreal.load_object(None, "/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget.ABP_GenericRetarget_C"))
m = {}
try:
    mp = cdo.get_editor_property("IKRetargeter_Map")
    for k in mp.keys():
        v = mp[k]
        m[str(k)] = v.get_path_name() if v else None
except Exception as e:
    m["error"] = str(e)
res["IKRetargeter_Map"] = m

# component tags of the visible meshes in each CBP
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
tags = {}
for n in ["Manny", "Quinn", "UE4_Mannequin", "Echo", "TwinBlast"]:
    bp = unreal.load_asset("/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_" + n)
    lst = []
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        obj = lib.get_object(lib.get_data(h))
        if isinstance(obj, unreal.SkinnedMeshComponent):
            try:
                mesh = obj.get_editor_property("skeletal_mesh_asset")
            except Exception:
                mesh = None
            try:
                lp = obj.get_editor_property("leader_pose_component")
            except Exception:
                lp = None
            mats = []
            try:
                mats = [x.get_path_name() if x else None for x in obj.get_editor_property("override_materials")]
            except Exception:
                pass
            lst.append({"name": obj.get_name(), "tags": [str(t) for t in obj.get_editor_property("component_tags")],
                        "mesh": mesh.get_path_name() if mesh else None,
                        "anim": str(obj.get_editor_property("anim_class").get_path_name()) if obj.get_editor_property("anim_class") else None,
                        "leader": lp.get_name() if lp else None, "mats": mats,
                        "parent": obj.get_attach_parent().get_name() if obj.get_attach_parent() else None,
                        "socket": str(obj.get_attach_socket_name())})
    tags[n] = lst
    t3d(bp, "CBP_" + n)
res["components"] = tags
open(OUT + "/../retargeters.json", "w").write(json.dumps(res, indent=1))
print("done")
