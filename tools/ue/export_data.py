import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Pass A: text dump of every non-binary asset + per-animation metadata JSON.
import unreal, os, json
ROOT = REPO + r"/tools/ue/out"
T3D = ROOT + "/t3d"
SKIP = {"Texture2D","SoundWave","MaterialFunction","MaterialInstanceConstant","Material","StaticMesh","SkeletalMesh",
        "AnimSequence","PoseAsset","SubsurfaceProfile","GroomAsset","GroomBindingAsset","TextureCube","PhysicsAsset",
        "SkeletalMeshLODSettings","MetaSoundSource","SoundSubmix","ControlRigBlueprint","ControlRigShapeLibrary",
        "EditorUtilityWidgetBlueprint","EditorUtilityBlueprint","MaterialParameterCollection","IKRigDefinition","IKRetargeter","Skeleton"}
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)

def t3d(obj, path):
    fn = T3D + path.replace("/Game", "") + ".t3d"
    os.makedirs(os.path.dirname(fn), exist_ok=True)
    t = unreal.AssetExportTask()
    t.object = obj; t.filename = fn; t.automated = True; t.prompt = False; t.replace_identical = True
    t.exporter = unreal.ObjectExporterT3D()
    return unreal.Exporter.run_asset_export_task(t)

n = 0
for a in assets:
    cls = str(a.asset_class_path.asset_name); path = str(a.package_name)
    if cls in SKIP or path.startswith("/Game/MetaHumans") or path.startswith("/Game/Developers"):
        continue
    obj = unreal.load_asset(path)
    if obj and t3d(obj, path): n += 1
unreal.log("T3D DONE %d" % n)

