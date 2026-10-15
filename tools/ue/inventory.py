import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, json, os
OUT = REPO + r"/tools/ue/out"
os.makedirs(OUT, exist_ok=True)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)
rows = []
for a in assets:
    rows.append({"path": str(a.package_name), "name": str(a.asset_name), "class": str(a.asset_class_path.asset_name)})
with open(OUT + "/inventory.json", "w") as f:
    json.dump(rows, f, indent=1)
from collections import Counter
c = Counter(r["class"] for r in rows)
with open(OUT + "/inventory_summary.txt", "w") as f:
    for k, v in c.most_common():
        f.write(f"{v}\t{k}\n")
unreal.log("INVENTORY DONE %d" % len(rows))
