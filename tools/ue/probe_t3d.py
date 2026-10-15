import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, os
OUT = REPO + r"/tools/ue/out/t3d_probe"
os.makedirs(OUT, exist_ok=True)
MM = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData"
paths = [MM + "/Databases/Dense/PSD_Dense_Stand_Run_Loops", MM + "/Schemas/PSS_Default", MM + "/CHT_PoseSearchDatabases", "/Game/Blueprints/CBP_SandboxCharacter", "/Game/Blueprints/ABP_SandboxCharacter"]
for path in paths:
    obj = unreal.load_asset(path)
    for ext, exp in [("t3d", unreal.ObjectExporterT3D()), ("copy", None)]:
        t = unreal.AssetExportTask()
        t.object = obj
        t.filename = OUT + "/" + path.split("/")[-1] + "." + ext
        t.automated = True; t.prompt = False; t.replace_identical = True
        if exp: t.exporter = exp
        ok = unreal.Exporter.run_asset_export_task(t)
        unreal.log("EXPORT %s %s %s" % (path, ext, ok))
