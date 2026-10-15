import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Export GASP audio: all SoundWaves under /Game/Audio to WAV, Foley MetaSounds to T3D (for their input defaults),
plus the level ambient sound setup."""
import unreal, os, json

OUT_WAV = REPO + r"/godot/assets/Audio"
OUT_T3D = REPO + r"/tools/ue/out/t3d_audio"
os.makedirs(OUT_T3D, exist_ok=True)
reg = unreal.AssetRegistryHelpers.get_asset_registry()
log = {"wav": [], "t3d": [], "err": []}

def export(obj, path, exporter=None):
    t = unreal.AssetExportTask()
    t.object = obj
    t.filename = path
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    if exporter:
        t.exporter = exporter
    ok = unreal.Exporter.run_asset_export_task(t)
    return ok

for ad in reg.get_assets_by_path("/Game/Audio", recursive=True):
    cls = str(ad.asset_class_path.asset_name)
    pkg = str(ad.package_name)
    rel = pkg[len("/Game/Audio/"):]
    try:
        obj = ad.get_asset()
        if cls == "SoundWave":
            p = os.path.join(OUT_WAV, rel + ".wav").replace("\\", "/")
            os.makedirs(os.path.dirname(p), exist_ok=True)
            ok = export(obj, p)
            log["wav"].append([pkg, ok, obj.get_editor_property("duration") if ok else 0])
        else:
            p = os.path.join(OUT_T3D, rel + ".t3d").replace("\\", "/")
            os.makedirs(os.path.dirname(p), exist_ok=True)
            ok = export(obj, p)
            log["t3d"].append([pkg, cls, ok])
    except Exception as e:
        log["err"].append([pkg, str(e)])

open(os.path.join(OUT_T3D, "export_log.json"), "w").write(json.dumps(log, indent=1))
print("audio export done", len(log["wav"]), len(log["t3d"]), len(log["err"]))
