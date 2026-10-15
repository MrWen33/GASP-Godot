import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""T3D of Echo's control rigs, physics assets and pose assets (post-process ABP inputs)."""
import unreal
base = "/Game/Characters/Echo/Rigs/"
names = ["CR_Echo_Helpers", "CR_Echo_Twist", "ABP_Echo_Hair"] + ["Physics/" + n for n in ["PA_Echo", "PA_Echo_Canister", "PA_Echo_Cloth_Scarf", "PA_Echo_Ponytail", "PA_Echo_Pouch", "PA_Echo_ShoulderPad", "PA_Echo_Skirt"]] + \
    ["PoseAssets/%s_PoseAsset" % b for b in ["calf_l", "calf_r", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "neck_02", "upperarm_l", "upperarm_r"]]
for n in names:
    obj = unreal.load_asset(base + n)
    t = unreal.AssetExportTask(); t.object = obj
    t.filename = REPO + r"/tools/ue/out/echo_rigs/" + n.split("/")[-1] + ".t3d"
    t.automated = True; t.prompt = False; t.replace_identical = True; t.exporter = unreal.ObjectExporterT3D()
    print(n, unreal.Exporter.run_asset_export_task(t))
