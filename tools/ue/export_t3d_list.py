"""T3D export of every asset listed (one /Game path per line) in the file named by env GASP_T3D_LIST into GASP_T3D_OUT."""
import os, unreal
lst = os.environ["GASP_T3D_LIST"]; out = os.environ["GASP_T3D_OUT"]
os.makedirs(out, exist_ok=True)
for a in open(lst).read().split():
    obj = unreal.load_asset(a)
    if obj is None:
        print("MISSING", a); continue
    t = unreal.AssetExportTask(); t.object = obj
    t.filename = os.path.join(out, a.split("/")[-1] + ".t3d")
    t.automated = True; t.prompt = False; t.replace_identical = True; t.exporter = unreal.ObjectExporterT3D()
    print("EXPORT", a, unreal.Exporter.run_asset_export_task(t))
