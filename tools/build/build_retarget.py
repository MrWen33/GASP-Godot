import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""IK Retargeter + IK Rig T3D -> godot/data/retarget/<RTG>.json (runtime data for native/src/retarget.cpp).

Defaults follow UE 5.5 IKRetargetSettings.h / IKRig_FBIKSolver.h / PBIKSolver.h; only overridden values appear in T3D."""
import os, re, json

T3D = REPO + r"/tools/ue/out/characters/t3d/u"
OUT = REPO + r"/godot/data/retarget"


def parse_value(s, i=0):
    """Parse a UE T3D property value starting at s[i]. Returns (value, next_index)."""
    if i < len(s) and s[i] == '(':
        i += 1
        items = []
        named = {}
        while i < len(s) and s[i] != ')':
            # key=value or bare value
            m = re.match(r'([A-Za-z_][A-Za-z0-9_ ]*)=', s[i:])
            if m and not s[i:].startswith('"'):
                key = m.group(1)
                v, i = parse_value(s, i + m.end())
                named[key] = v
            else:
                v, i = parse_value(s, i)
                items.append(v)
            if i < len(s) and s[i] == ',':
                i += 1
            while i < len(s) and s[i] == ' ':
                i += 1
        i += 1  # ')'
        if named and not items:
            return named, i
        if items and not named:
            return items, i
        named["_items"] = items
        return named, i
    if i < len(s) and s[i] == '"':
        j = s.index('"', i + 1)
        return s[i + 1:j], j + 1
    j = i
    while j < len(s) and s[j] not in ',)':
        j += 1
    tok = s[i:j].strip()
    try:
        return float(tok), j
    except ValueError:
        return tok, j


def read_objects(path):
    """Return {object name: {prop: raw string}} for every Begin Object block (later blocks merge over earlier)."""
    txt = open(path, encoding="utf-8").read().replace("\r", "")
    objs = {}
    stack = []
    for line in txt.split("\n"):
        st = line.strip()
        if st.startswith("Begin Object"):
            m = re.search(r'Name="([^"]+)"', st)
            cm = re.search(r'Class=([^ ]+)', st)
            name = m.group(1) if m else "?"
            stack.append(name)
            o = objs.setdefault(name, {})
            if cm:
                o["__class"] = cm.group(1)
        elif st.startswith("End Object"):
            stack.pop()
        elif stack and "=" in st:
            k, v = st.split("=", 1)
            objs[stack[-1]][k] = v
    return objs


def val(raw):
    return parse_value(raw)[0] if raw is not None else None


def vec(d, default=(0, 0, 0)):
    if not isinstance(d, dict):
        return list(default)
    return [float(d.get("X", default[0])), float(d.get("Y", default[1])), float(d.get("Z", default[2]))]


def rot(d):
    if not isinstance(d, dict):
        return [0, 0, 0]
    return [float(d.get("Pitch", 0)), float(d.get("Yaw", 0)), float(d.get("Roll", 0))]


def quat(d):
    return [float(d.get("X", 0)), float(d.get("Y", 0)), float(d.get("Z", 0)), float(d.get("W", 1))]


def asset_name(ref):
    return ref.split(".")[-1].strip('"\'')


def load_ik_rig(name):
    objs = read_objects(os.path.join(T3D, name + ".t3d"))
    root = objs[name]
    rd = val(root["RetargetDefinition"])
    chains = []
    for c in rd["BoneChains"]:
        chains.append({"name": c["ChainName"], "start": c["StartBone"]["BoneName"], "end": c["EndBone"]["BoneName"],
                       "goal": c.get("IKGoalName", "None")})
    goals = {}
    solver = None
    effectors = []
    bone_settings = []
    for oname, o in objs.items():
        cls = o.get("__class", "")
        if cls.endswith("IKRigEffectorGoal") or "GoalName" in o and "BoneName" in o and "CurrentTransform" in o:
            goals[val(o["GoalName"])] = {"bone": val(o["BoneName"])}
    for oname, o in objs.items():
        if o.get("__class", "").endswith("IKRigFBIKSolver") or "RootBone" in o and "Iterations" in o:
            if "RootBone" in o:
                solver = {
                    "root": val(o["RootBone"]),
                    "iterations": int(val(o.get("Iterations", "20"))),
                    "sub_iterations": int(val(o.get("SubIterations", "0"))),
                    "mass_multiplier": float(val(o.get("MassMultiplier", "1.0"))),
                    "allow_stretch": o.get("bAllowStretch", "False") == "True",
                    "root_behavior": val(o.get("RootBehavior", "PrePull")),
                    "pull_chain_alpha": float(val(o.get("PullChainAlpha", "1.0"))),
                    "max_angle": float(val(o.get("MaxAngle", "30.0"))),
                    "over_relaxation": float(val(o.get("OverRelaxation", "1.3"))),
                }
    for oname, o in objs.items():
        if "GoalName" in o and "BoneName" in o and "CurrentTransform" not in o:
            effectors.append({"goal": val(o["GoalName"]), "bone": val(o["BoneName"]),
                              "chain_depth": int(val(o.get("ChainDepth", "0"))),
                              "strength": float(val(o.get("StrengthAlpha", "1.0"))),
                              "pull_chain": float(val(o.get("PullChainAlpha", "1.0"))),
                              "pin_rotation": float(val(o.get("PinRotation", "1.0")))})
        if "Bone" in o and ("RotationStiffness" in o or "X" in o or "Y" in o or "Z" in o or "bUsePreferredAngles" in o or "PositionStiffness" in o):
            pa = val(o.get("PreferredAngles", "(X=0,Y=0,Z=0)"))
            bone_settings.append({"bone": val(o["Bone"]),
                                  "rotation_stiffness": float(val(o.get("RotationStiffness", "0"))),
                                  "position_stiffness": float(val(o.get("PositionStiffness", "0"))),
                                  "x": val(o.get("X", "Free")), "min_x": float(val(o.get("MinX", "0"))), "max_x": float(val(o.get("MaxX", "0"))),
                                  "y": val(o.get("Y", "Free")), "min_y": float(val(o.get("MinY", "0"))), "max_y": float(val(o.get("MaxY", "0"))),
                                  "z": val(o.get("Z", "Free")), "min_z": float(val(o.get("MinZ", "0"))), "max_z": float(val(o.get("MaxZ", "0"))),
                                  "use_preferred": o.get("bUsePreferredAngles", "False") == "True",
                                  "preferred": vec(pa)})
    excluded = []
    for k, v in root.items():
        if k.startswith("ExcludedBones"):
            excluded.append(val(v))
    return {"root": rd.get("RootBone", "None"), "chains": chains, "goals": goals, "solver": solver,
            "effectors": effectors, "bone_settings": bone_settings, "excluded": excluded}


def fk_settings(d):
    d = d if isinstance(d, dict) else {}
    return {"enable": d.get("EnableFK", "True") != "False", "rotation_mode": d.get("RotationMode", "Interpolated"),
            "rotation_alpha": float(d.get("RotationAlpha", 1.0)), "translation_mode": d.get("TranslationMode", "None"),
            "translation_alpha": float(d.get("TranslationAlpha", 1.0)), "pole_vector_matching": float(d.get("PoleVectorMatching", 0.0)),
            "pole_vector_maintain_offset": d.get("PoleVectorMaintainOffset", "False") == "True",
            "pole_vector_offset": float(d.get("PoleVectorOffset", 0.0))}


def ik_settings(d):
    d = d if isinstance(d, dict) else {}
    return {"enable": d.get("EnableIK", "True") != "False", "blend_to_source": float(d.get("BlendToSource", 0.0)),
            "blend_to_source_weights": vec(d.get("BlendToSourceWeights"), (1, 1, 1)), "static_offset": vec(d.get("StaticOffset")),
            "static_local_offset": vec(d.get("StaticLocalOffset")), "static_rotation_offset": rot(d.get("StaticRotationOffset")),
            "scale_vertical": float(d.get("ScaleVertical", 1.0)), "extension": float(d.get("Extension", 1.0))}


def build(rtg):
    objs = read_objects(os.path.join(T3D, rtg + ".t3d"))
    root = objs[rtg]
    src_rig = load_ik_rig(asset_name(root["SourceIKRigAsset"]))
    tgt_rig = load_ik_rig(asset_name(root["TargetIKRigAsset"]))
    rs = val(objs.get("RootSettings", {}).get("Settings", "()")) or {}
    root_settings = {"rotation_alpha": float(rs.get("RotationAlpha", 1.0)), "translation_alpha": float(rs.get("TranslationAlpha", 1.0)),
                     "blend_to_source": float(rs.get("BlendToSource", 0.0)), "blend_to_source_weights": vec(rs.get("BlendToSourceWeights"), (1, 1, 1)),
                     "scale_horizontal": float(rs.get("ScaleHorizontal", 1.0)), "scale_vertical": float(rs.get("ScaleVertical", 1.0)),
                     "translation_offset": vec(rs.get("TranslationOffset")), "rotation_offset": rot(rs.get("RotationOffset")),
                     "affect_ik_horizontal": float(rs.get("AffectIKHorizontal", 1.0)), "affect_ik_vertical": float(rs.get("AffectIKVertical", 0.0))}
    gs = val(objs.get("GlobalSettings", {}).get("Settings", "()")) or {}
    global_settings = {"enable_root": gs.get("bEnableRoot", "True") != "False", "enable_fk": gs.get("bEnableFK", "True") != "False",
                       "enable_ik": gs.get("bEnableIK", "True") != "False", "warping": gs.get("bWarping", "False") == "True"}
    # chain settings in asset order (ChainSettings(i) -> object)
    order = []
    for k, v in root.items():
        m = re.match(r'ChainSettings\((\d+)\)', k)
        if m:
            order.append((int(m.group(1)), re.search(r"'([^']+)'", v).group(1)))
    order.sort()
    chains = []
    for _, oname in order:
        o = objs[oname]
        st = val(o.get("Settings", "()")) or {}
        chains.append({"source": val(o.get("SourceChain", '"None"')), "target": val(o.get("TargetChain", '"None"')),
                       "fk": fk_settings(st.get("FK")), "ik": ik_settings(st.get("IK"))})

    def pose(key):
        if key not in root:
            return {"root_offset": [0, 0, 0], "rotations": {}}
        p = val(root[key])
        # (("Default Pose", (RootTranslationOffset=..., BoneRotationOffsets=((bone, quat), ...))))
        entry = p[0] if isinstance(p, list) else p
        body = entry[1] if isinstance(entry, list) and len(entry) > 1 else {}
        rots = {}
        for pair in body.get("BoneRotationOffsets", []) if isinstance(body, dict) else []:
            rots[pair[0]] = quat(pair[1])
        return {"root_offset": vec(body.get("RootTranslationOffset")) if isinstance(body, dict) else [0, 0, 0], "rotations": rots}

    return {"name": rtg, "source_rig": src_rig, "target_rig": tgt_rig, "root_settings": root_settings,
            "global_settings": global_settings, "chains": chains,
            "source_pose": pose("SourceRetargetPoses"), "target_pose": pose("TargetRetargetPoses")}


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in sorted(os.listdir(T3D)):
        if f.startswith("RTG_"):
            name = f[:-4]
            d = build(name)
            json.dump(d, open(os.path.join(OUT, name + ".json"), "w"), indent=1)
            print(name, len(d["chains"]), "chains,", len(d["target_pose"]["rotations"]), "pose offsets, root offset",
                  d["target_pose"]["root_offset"], "| target solver", d["target_rig"]["solver"]["root"] if d["target_rig"]["solver"] else None,
                  len(d["target_rig"]["effectors"]), "effectors", len(d["target_rig"]["bone_settings"]), "bone settings")
