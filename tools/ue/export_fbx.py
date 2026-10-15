import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Pass B: FBX for skeletal meshes, all UEFN animations, level static meshes; TGA for textures.
import unreal, os
OUT = REPO + r"/godot/assets"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)

def opts():
    o = unreal.FbxExportOption()
    o.ascii = False
    o.force_front_x_axis = False
    o.level_of_detail = False
    o.collision = False
    o.export_morph_targets = True
    o.export_preview_mesh = False
    o.map_skeletal_motion_to_root = False
    o.vertex_color = True
    try:
        o.bake_material_inputs = unreal.FbxMaterialBakeMode.DISABLED
    except Exception as e:
        unreal.log("BAKE OPT " + str(e))
    return o

def export(obj, path, ext, exporter=None, options=None):
    fn = OUT + path.replace("/Game", "") + "." + ext
    if os.path.exists(fn):
        return True
    os.makedirs(os.path.dirname(fn), exist_ok=True)
    t = unreal.AssetExportTask()
    t.object = obj; t.filename = fn; t.automated = True; t.prompt = False; t.replace_identical = True
    if exporter: t.exporter = exporter
    if options: t.options = options
    return unreal.Exporter.run_asset_export_task(t)

MESHES = ["/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin",
          "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple",
          "/Game/Characters/UE5_Mannequins/Meshes/SKM_Quinn_Simple",
          "/Game/LevelPrototyping/Meshes/SM_Cube",
          "/Game/LevelPrototyping/Meshes/SM_Cylinder"]
for p in MESHES:
    obj = unreal.load_asset(p)
    ex = unreal.SkeletalMeshExporterFBX() if isinstance(obj, unreal.SkeletalMesh) else unreal.StaticMeshExporterFBX()
    ok = export(obj, p, "fbx", ex, opts())
    unreal.log("MESH %s %s" % (p, ok))

n = 0; fail = []
# animation FBX are not used at runtime (bake_anims.py samples the sequences directly); opt in with GASP_ANIM_FBX=1
for a in (assets if os.environ.get("GASP_ANIM_FBX") else []):
    cls = str(a.asset_class_path.asset_name); path = str(a.package_name)
    if cls == "AnimSequence" and path.startswith("/Game/Characters/UEFN_Mannequin"):
        if export(unreal.load_asset(path), path, "fbx", unreal.AnimSequenceExporterFBX(), opts()): n += 1
        else: fail.append(path)
unreal.log("ANIM DONE %d fail %d" % (n, len(fail)))
for f in fail: unreal.log("ANIM FAIL " + f)

tn = 0
for a in assets:
    cls = str(a.asset_class_path.asset_name); path = str(a.package_name)
    if cls == "Texture2D" and (path.startswith("/Game/Characters/UEFN_Mannequin") or path.startswith("/Game/LevelPrototyping")
                               or path.startswith("/Game/Characters/UE5_Mannequins")):
        if export(unreal.load_asset(path), path, "tga", unreal.TextureExporterTGA()): tn += 1
unreal.log("TEX DONE %d" % tn)

unreal.log("ALL EXPORT FINISHED")
unreal.SystemLibrary.quit_editor()
