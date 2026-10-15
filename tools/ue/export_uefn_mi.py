import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Parameter overrides of MI_UEFN_Mannequin_Blue and the default parameters of M_UEFN_Mannequin."""
import unreal, json
OUT = REPO + r"/tools/ue/out/uefn_materials.json"
M = unreal.MaterialEditingLibrary
res = {}
mi = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Materials/MI_UEFN_Mannequin_Blue")
res["mi_vectors"] = {str(p.parameter_info.name): [p.parameter_value.r, p.parameter_value.g, p.parameter_value.b, p.parameter_value.a] for p in mi.get_editor_property("vector_parameter_values")}
res["mi_scalars"] = {str(p.parameter_info.name): p.parameter_value for p in mi.get_editor_property("scalar_parameter_values")}
res["mi_textures"] = {str(p.parameter_info.name): (p.parameter_value.get_path_name() if p.parameter_value else None) for p in mi.get_editor_property("texture_parameter_values")}
m = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Materials/M_UEFN_Mannequin")
res["m_vectors"] = {}
for n in M.get_vector_parameter_names(m):
    c = M.get_material_default_vector_parameter_value(m, n)
    res["m_vectors"][str(n)] = [c.r, c.g, c.b, c.a]
res["m_scalars"] = {str(n): M.get_material_default_scalar_parameter_value(m, n) for n in M.get_scalar_parameter_names(m)}
res["m_textures"] = {}
for n in M.get_texture_parameter_names(m):
    t = M.get_material_default_texture_parameter_value(m, n)
    res["m_textures"][str(n)] = t.get_path_name() if t else None
json.dump(res, open(OUT, "w"), indent=1)
