"""Copies the small exported JSON files that need no conversion into godot/data (the last data-build step)."""
import os, shutil

REPO = os.environ.get("GASP_REPO", ".")
OUT = REPO + "/tools/ue/out"
DATA = REPO + "/godot/data"

COPIES = [
    (OUT + "/additive.json", DATA + "/additive.json"),
    (OUT + "/sm_notifies.json", DATA + "/sm_notifies.json"),
    (OUT + "/characters/meshes.json", DATA + "/characters/meshes.json"),
    (OUT + "/uefn_materials.json", DATA + "/characters/uefn_materials.json"),
    # hand-extracted from M_UEFN_Mannequin_Masked (parameter name -> mask texture/channel); no exporter
    (REPO + "/tools/static/uefn_masked_regions.json", DATA + "/characters/uefn_masked_regions.json"),
]
for src, dst in COPIES:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    print("copied", os.path.relpath(src, REPO), "->", os.path.relpath(dst, REPO))
