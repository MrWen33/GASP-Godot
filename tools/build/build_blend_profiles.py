"""SK_UEFN_Mannequin skeleton T3D (tools/ue/export_skel_t3d.py) -> godot/data/blend_profiles.json.
Entries without a Mode line use the BlendProfile constructor default (WeightFactor); entries with BlendScale=0 are
omitted by the T3D exporter, so they are absent here too."""
import json, os, re

REPO = os.environ.get("GASP_REPO", ".")
SRC = REPO + "/tools/ue/out/SK_UEFN_Mannequin.t3d"
DST = REPO + "/godot/data/blend_profiles.json"

profiles, cur = {}, None
for ln in open(SRC, encoding="utf-8").read().splitlines():
    m = re.match(r'\s*Begin Object Name="([^"]+)" ExportPath="/Script/Engine\.BlendProfile\'', ln)
    if m:
        cur = profiles.setdefault(m.group(1), {"mode": "WeightFactor", "entries": {}})
        continue
    if cur is None:
        continue
    if ln.strip() == "End Object":
        cur = None
        continue
    m = re.match(r'\s*ProfileEntries\(\d+\)=\(BoneReference=\(BoneName="([^"]+)"\),BlendScale=([-\d.eE+]+)\)', ln)
    if m:
        cur["entries"][m.group(1)] = float(m.group(2))
        continue
    m = re.match(r"\s*Mode=(\w+)", ln)
    if m:
        cur["mode"] = m.group(1)

json.dump(profiles, open(DST, "w"), indent=1)
print("blend profiles:", {k: len(v["entries"]) for k, v in profiles.items()})
