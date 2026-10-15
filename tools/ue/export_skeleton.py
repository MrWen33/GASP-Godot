import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
import unreal, json
OUT = REPO + r"/tools/ue/out/skeleton_extra.json"
skel = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin")
res = {"blend_profiles": {}, "err": []}
try:
    profiles = skel.get_editor_property("blend_profiles")
except Exception as e:
    profiles = []; res["err"].append(str(e))
for p in profiles:
    d = {"mode": str(p.get_editor_property("mode")), "entries": []}
    for e in p.get_editor_property("profile_entries"):
        d["entries"].append([str(e.get_editor_property("bone_reference").get_editor_property("bone_name")), e.get_editor_property("blend_scale")])
    res["blend_profiles"][p.get_name()] = d
socks = []
try:
    for s in skel.get_editor_property("sockets"):
        socks.append({"name": str(s.get_editor_property("socket_name")), "bone": str(s.get_editor_property("bone_name")),
                      "loc": str(s.get_editor_property("relative_location")), "rot": str(s.get_editor_property("relative_rotation"))})
except Exception as e:
    res["err"].append(str(e))
res["sockets"] = socks
open(OUT, "w").write(json.dumps(res, indent=1))
