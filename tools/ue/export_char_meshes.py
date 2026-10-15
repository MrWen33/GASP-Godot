import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Full-editor pass (UnrealEditor.exe -ExecutePythonScript): FBX for every retargeted-character skeletal mesh, material
slot info (parent material, texture/vector/scalar parameters) and PNG/EXR textures."""
import unreal, os, json

ASSETS = REPO + r"/godot/assets"
OUTJ = REPO + r"/tools/ue/out/characters/meshes.json"
LOG = REPO + r"/tools/ue/out/characters/export_meshes.log"
log = open(LOG, "w")

def L(s):
    log.write(str(s) + "\n"); log.flush()

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
    except Exception:
        pass
    return o

def export(obj, path, ext, exporter=None, options=None):
    fn = ASSETS + path.replace("/Game", "") + "." + ext
    if os.path.exists(fn):
        return True
    os.makedirs(os.path.dirname(fn), exist_ok=True)
    t = unreal.AssetExportTask()
    t.object = obj; t.filename = fn; t.automated = True; t.prompt = False; t.replace_identical = True
    if exporter: t.exporter = exporter
    if options: t.options = options
    return unreal.Exporter.run_asset_export_task(t)

MESHES = ["/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin",
          "/Game/Characters/Echo/Meshes/SKM_Echo", "/Game/Characters/Echo/Meshes/SKM_Echo_Hair",
          "/Game/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero",
          "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple", "/Game/Characters/UE5_Mannequins/Meshes/SKM_Quinn_Simple",
          "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"]

res = {"meshes": {}}

textures = set()

def mat_info(mi):
    d = {"path": mi.get_path_name(), "class": mi.get_class().get_name()}
    base = mi
    while isinstance(base, unreal.MaterialInstance) and base.get_editor_property("parent"):
        base = base.get_editor_property("parent")
    d["base"] = base.get_path_name()
    if isinstance(mi, unreal.MaterialInterface):
        try:
            d["blend"] = str(base.get_editor_property("blend_mode"))
            d["two_sided"] = bool(base.get_editor_property("two_sided"))
        except Exception:
            pass
    tex = {}; vec = {}; sca = {}
    mel = unreal.MaterialEditingLibrary
    try:
        for n in mel.get_texture_parameter_names(mi):
            t = mel.get_material_instance_texture_parameter_value(mi, n) if isinstance(mi, unreal.MaterialInstance) else None
            if t is None and isinstance(base, unreal.Material):
                t = mel.get_material_default_texture_parameter_value(base, n)
            if t:
                tex[str(n)] = t.get_path_name(); textures.add(t.get_path_name().split(".")[0])
        for n in mel.get_vector_parameter_names(mi):
            v = mel.get_material_instance_vector_parameter_value(mi, n) if isinstance(mi, unreal.MaterialInstance) else mel.get_material_default_vector_parameter_value(base, n)
            vec[str(n)] = [v.r, v.g, v.b, v.a]
        for n in mel.get_scalar_parameter_names(mi):
            s = mel.get_material_instance_scalar_parameter_value(mi, n) if isinstance(mi, unreal.MaterialInstance) else mel.get_material_default_scalar_parameter_value(base, n)
            sca[str(n)] = s
    except Exception as e:
        d["err"] = str(e)
    # textures referenced by the base material graph (non-parameter samplers)
    try:
        for t in mel.get_used_textures(base):
            textures.add(t.get_path_name().split(".")[0])
            d.setdefault("used", []).append(t.get_path_name())
    except Exception:
        pass
    d["tex"] = tex; d["vec"] = vec; d["scalar"] = sca
    return d

for p in MESHES:
    m = unreal.load_asset(p)
    if m is None:
        L("missing " + p); continue
    ok = export(m, p, "fbx", unreal.SkeletalMeshExporterFBX(), opts())
    info = {"fbx": bool(ok), "materials": []}
    for sm in m.get_editor_property("materials"):
        mi = sm.get_editor_property("material_interface")
        info["materials"].append({"slot": str(sm.get_editor_property("material_slot_name")), "mat": mat_info(mi) if mi else None})
    try:
        info["skeleton"] = m.get_editor_property("skeleton").get_path_name()
        pp = m.get_editor_property("post_process_anim_blueprint")
        info["post_process_abp"] = pp.get_path_name() if pp else None
    except Exception:
        pass
    res["meshes"][p] = info
    L("mesh %s %s" % (p, ok))

open(OUTJ, "w").write(json.dumps(res, indent=1))
n = 0
HDR = ("HDR", "HALF_FLOAT", "SINGLE_FLOAT", "HDR_COMPRESSED", "HDR_F32")
for t in sorted(textures):
    obj = unreal.load_asset(t)
    if obj and isinstance(obj, unreal.Texture2D):
        cs = str(obj.get_editor_property("compression_settings"))
        hdr = any(h in cs for h in HDR)
        L("tex %s %s" % (t, cs))
        if export(obj, t, "exr" if hdr else "png", unreal.TextureExporterEXR() if hdr else unreal.TextureExporterPNG()):
            n += 1
        else:
            L("tex fail " + t)
L("textures %d / %d" % (n, len(textures)))
L("ALL DONE")
log.close()
unreal.SystemLibrary.quit_editor()
