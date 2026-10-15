import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, os
OUT = REPO + r"/tools/ue/out/cdo"
os.makedirs(OUT, exist_ok=True)
BPS = ["/Game/Blueprints/CBP_SandboxCharacter", "/Game/Blueprints/ABP_SandboxCharacter", "/Game/Blueprints/PC_Sandbox", "/Game/Blueprints/GM_Sandbox",
       "/Game/Blueprints/Cameras/CameraDirector_SandboxCharacter", "/Game/LevelPrototyping/LevelBlock", "/Game/LevelPrototyping/LevelBlock_Traversable",
       "/Game/LevelPrototyping/LevelVisuals", "/Game/LevelPrototyping/LevelButton", "/Game/LevelPrototyping/Teleporter_Sender", "/Game/LevelPrototyping/Teleporter_Destination",
       "/Game/Blueprints/AnimNotifies/BP_NotifyState_EarlyTransition", "/Game/Blueprints/AnimNotifies/BP_NotifyState_MontageBlendOut", "/Game/Blueprints/AnimNotifies/BP_AnimNotify_FoleyEvent",
       "/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_Manny", "/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_Quinn", "/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_UE4_Mannequin"]
def exp(obj, fn):
    t = unreal.AssetExportTask(); t.object = obj; t.filename = fn; t.automated = True; t.prompt = False; t.replace_identical = True
    t.exporter = unreal.ObjectExporterT3D()
    return unreal.Exporter.run_asset_export_task(t)
for p in BPS:
    name = p.split("/")[-1]
    bp = unreal.load_asset(p)
    gc = bp.generated_class()
    cdo = unreal.get_default_object(gc)
    unreal.log("CDO %s %s" % (name, exp(cdo, OUT + "/" + name + ".t3d")))
    # also the Simple Construction Script component templates
    try:
        scs = bp.get_editor_property("simple_construction_script")
        if scs:
            for i, node in enumerate(scs.get_all_nodes()):
                tmpl = node.get_editor_property("component_template")
                if tmpl:
                    exp(tmpl, OUT + "/" + name + "__" + tmpl.get_name() + ".t3d")
    except Exception as e:
        unreal.log("SCS ERR %s %s" % (name, e))
# level actors
world = unreal.load_asset("/Game/Levels/DefaultLevel")
unreal.log("CDO DONE")
