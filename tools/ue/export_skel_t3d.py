import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal
obj = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin")
t = unreal.AssetExportTask(); t.object = obj; t.filename = REPO + r"/tools/ue/out/SK_UEFN_Mannequin.t3d"
t.automated = True; t.prompt = False; t.replace_identical = True; t.exporter = unreal.ObjectExporterT3D()
unreal.Exporter.run_asset_export_task(t)
