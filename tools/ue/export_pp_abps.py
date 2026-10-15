import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""T3D of the post-process / follower anim blueprints used by the sandbox characters."""
import unreal, os
OUT = REPO + r"/tools/ue/out/characters/t3d"
os.makedirs(OUT, exist_ok=True)
paths = ["/Game/Characters/UEFN_Mannequin/Rigs/ABP_UEFN_Mannequin_PostProcess", "/Game/Characters/Echo/Rigs/ABP_Echo_PostProcess",
         "/Game/Characters/Echo/Rigs/ABP_Echo_Hair", "/Game/Characters/Paragon/Heroes/TwinBlast/Rigs/ABP_TwinBlast_PostProcess"]
log = open(OUT + "/../pp_abps.log", "w")
for p in paths:
    obj = unreal.load_asset(p)
    if not obj:
        log.write("missing %s\n" % p); continue
    t = unreal.AssetExportTask()
    t.object = obj; t.filename = OUT + "/" + p.split("/")[-1] + ".t3d"
    t.automated = True; t.prompt = False; t.replace_identical = True
    t.exporter = unreal.ObjectExporterT3D()
    log.write("%s %s\n" % (p, unreal.Exporter.run_asset_export_task(t)))
log.close()
