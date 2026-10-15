import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Dumps fully-resolved PoseSearch schemas / databases / normalization sets / mirror table to JSON.
import unreal, json, os

OUT = REPO + r"/tools/ue/out"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)

def gp(o, name, default=None):
    try:
        v = o.get_editor_property(name)
    except Exception:
        return default
    return v

def enum_name(v):
    return str(v).split(".")[-1].split(":")[0].strip("<> ") if v is not None else None

def bone(o, name):
    b = gp(o, name)
    return str(b.get_editor_property("bone_name")) if b else ""

def channel(c):
    cls = c.get_class().get_name()
    d = {"class": cls}
    for k in ["weight", "sample_time_offset", "origin_time_offset", "b_use_character_space_velocities", "b_normalize",
              "normalization_group", "sampling_attribute_id", "curve_name", "b_use_blueprint_query_override"]:
        v = gp(c, k)
        if v is not None:
            d[k] = v if isinstance(v, (int, float, bool)) else str(v)
    for k in ["input_query_pose", "component_stripping", "heading_axis", "permutation_time_type"]:
        v = gp(c, k)
        if v is not None:
            d[k] = enum_name(v)
    for k in ["bone", "origin_bone"]:
        if gp(c, k) is not None:
            d[k] = bone(c, k)
    samples = gp(c, "samples")
    if samples is not None:
        d["samples"] = [{"offset": s.get_editor_property("offset"), "flags": s.get_editor_property("flags"),
                         "weight": s.get_editor_property("weight"), "normalization_group": str(s.get_editor_property("normalization_group"))}
                        for s in samples]
    # pose channel
    sampled = gp(c, "sampled_bones")
    if sampled is not None:
        d["sampled_bones"] = []
        for sb in sampled:
            item = {"bone": str(sb.get_editor_property("reference").get_editor_property("bone_name"))}
            for k in ["flags", "weight", "color_preset_index"]:
                try: item[k] = sb.get_editor_property(k)
                except Exception: pass
            d["sampled_bones"].append(item)
        for k in ["sample_time_offset"]:
            d[k] = gp(c, k)
    subs = gp(c, "sub_channels")
    if subs is not None and cls != "PoseSearchFeatureChannel_Trajectory":
        d["sub_channels"] = [channel(s) for s in subs if s]
    return d

schemas = {}
dbs = {}
norms = {}
for a in assets:
    cls = str(a.asset_class_path.asset_name); path = str(a.package_name)
    if cls == "PoseSearchSchema":
        s = unreal.load_asset(path)
        schemas[path] = {
            "sample_rate": gp(s, "sample_rate"),
            "data_preprocessor": enum_name(gp(s, "data_preprocessor")),
            "number_of_permutations": gp(s, "number_of_permutations"),
            "permutations_sample_rate": gp(s, "permutations_sample_rate"),
            "permutations_time_offset": gp(s, "permutations_time_offset"),
            "add_data_padding": gp(s, "add_data_padding"),
            "mirror_mismatch_cost_bias": gp(s, "mirror_mismatch_cost_bias"),
            "channels": [channel(c) for c in gp(s, "channels") if c],
        }
    elif cls == "PoseSearchDatabase":
        d = unreal.load_asset(path)
        rng = gp(d, "exclude_from_database_parameters")
        ext = gp(d, "additional_extrapolation_time")
        dbs[path] = {
            "schema": gp(d, "schema").get_path_name() if gp(d, "schema") else None,
            "normalization_set": gp(d, "normalization_set").get_path_name() if gp(d, "normalization_set") else None,
            "tags": [str(t) for t in gp(d, "tags", [])],
            "continuing_pose_cost_bias": gp(d, "continuing_pose_cost_bias"),
            "base_cost_bias": gp(d, "base_cost_bias"),
            "looping_cost_bias": gp(d, "looping_cost_bias"),
            "exclude_from_database_parameters": [rng.get_editor_property("min"), rng.get_editor_property("max")] if rng else None,
            "additional_extrapolation_time": [ext.get_editor_property("min"), ext.get_editor_property("max")] if ext else None,
            "pose_search_mode": enum_name(gp(d, "pose_search_mode")),
            "number_of_principal_components": gp(d, "number_of_principal_components"),
            "kd_tree_max_leaf_size": gp(d, "kd_tree_max_leaf_size"),
            "kd_tree_query_num_neighbors": gp(d, "kd_tree_query_num_neighbors"),
            "pose_pruning_similarity_threshold": gp(d, "pose_pruning_similarity_threshold"),
            "pca_values_pruning_similarity_threshold": gp(d, "pca_values_pruning_similarity_threshold"),
            "kd_tree_query_num_neighbors_with_duplicates": gp(d, "kd_tree_query_num_neighbors_with_duplicates"),
            "num_animation_assets": d.get_num_animation_assets(),
        }
    elif cls == "PoseSearchNormalizationSet":
        n = unreal.load_asset(path)
        norms[path] = [x.get_path_name() for x in gp(n, "databases", []) if x]

mdt = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Rigs/MDT_UEFN_Mannequin")
mirror = {"rows": [], "mirror_axis": enum_name(gp(mdt, "mirror_axis")), "find_replace": []}
try:
    for fr in gp(mdt, "mirror_find_replace_expressions", []):
        mirror["find_replace"].append({"find": str(fr.get_editor_property("find_expression")), "replace": str(fr.get_editor_property("replace_expression")),
                                       "method": enum_name(fr.get_editor_property("find_replace_method"))})
except Exception as e:
    mirror["find_replace_err"] = str(e)
try:
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(mdt)
    for col in ["Name", "MirroredName", "MirrorEntryType"]:
        vals = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(mdt, col)
        mirror.setdefault("columns", {})[col] = list(vals)
    mirror["row_names"] = [str(n) for n in names]
except Exception as e:
    mirror["rows_err"] = str(e)

with open(OUT + "/posesearch.json", "w") as f:
    json.dump({"schemas": schemas, "databases": dbs, "normalization_sets": norms, "mirror": mirror}, f, indent=1)
unreal.log("POSESEARCH DONE %d %d %d" % (len(schemas), len(dbs), len(norms)))
