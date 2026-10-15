import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""T3D of materials / material functions (expression graphs) for shader ports."""
import unreal
ASSETS = ["/Game/Characters/UEFN_Mannequin/Materials/M_UEFN_Mannequin", "/Game/Characters/UEFN_Mannequin/Materials/MF_CustomColorMask",
          "/Game/Characters/UEFN_Mannequin/Materials/MI_UEFN_Mannequin_Blue", "/Game/Characters/UEFN_Mannequin/Materials/M_UEFN_Mannequin_Masked"]
for a in ASSETS:
    obj = unreal.load_asset(a)
    t = unreal.AssetExportTask(); t.object = obj
    t.filename = REPO + r"/tools/ue/out/materials/" + a.split("/")[-1] + ".t3d"
    t.automated = True; t.prompt = False; t.replace_identical = True; t.exporter = unreal.ObjectExporterT3D()
    print(a, unreal.Exporter.run_asset_export_task(t))
