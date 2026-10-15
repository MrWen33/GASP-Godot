import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, os
OUT = REPO + r"/tools/ue/out"
L = []
def p(*a): L.append(" ".join(str(x) for x in a))
MM = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData"
db = unreal.load_asset(MM + "/Databases/Dense/PSD_Dense_Stand_Run_Loops")
p("DB dir:", [d for d in dir(db) if not d.startswith("_")])
for k in ["schema","normalization_set","tags","exclude_from_database_parameters","continuing_pose_cost_bias","base_cost_bias","looping_cost_bias","pose_search_mode","animation_assets","database_animation_assets"]:
    try: p("DB", k, "=", db.get_editor_property(k))
    except Exception as e: p("DB", k, "ERR", e)
sc = unreal.load_asset(MM + "/Schemas/PSS_Default")
p("SCHEMA dir:", [d for d in dir(sc) if not d.startswith("_")])
for k in ["sample_rate","channels","skeletons","data_preprocessor","mirror_mismatch_cost_bias","number_of_permutations","permutations_sample_rate","permutations_time_offset","add_data_padding","inject_additional_debug_channels"]:
    try:
        v = sc.get_editor_property(k); p("SCHEMA", k, "=", v)
        if k=="channels":
            for c in v:
                p("  CH", type(c).__name__, [d for d in dir(c) if not d.startswith("_")])
    except Exception as e: p("SCHEMA", k, "ERR", e)
ch = unreal.load_asset(MM + "/CHT_PoseSearchDatabases")
p("CHOOSER dir:", [d for d in dir(ch) if not d.startswith("_")])
with open(OUT + "/probe.txt","w") as f: f.write("\n".join(L))
