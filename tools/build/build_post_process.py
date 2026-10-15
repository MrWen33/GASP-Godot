"""Builds godot/data/post_process/<ABP>.json for the character post-process anim blueprints: walks the AnimGraph from Root
back to the input pose (LinkedInputPose / CopyPoseFromMesh) and emits the evaluated node chain (input first):
  pose_driver (FAnimNode_PoseDriver + pose asset data), constraint (FAnimNode_Constraint), rigid_body (FAnimNode_RigidBody
  + physics asset), control_rig (Control Rig T3D compiled by rigvm_compile.py; rigs.py hand ports kept as fallback), modify_curve, copy_pose.
BlendListByBool bound to ShouldDisablePhysics takes the False branch (LOD 0)."""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
import ue_text
from ue_text import f, vec, rot, quat, asset_name

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
CHAR = os.path.join(ROOT, "tools", "ue", "out", "characters")
T3D_DIRS = [os.path.join(ROOT, "tools", "ue", "out", d) for d in ("echo_rigs",)]
OUT = os.path.join(ROOT, "godot", "data", "post_process")


def read_t3d(name):
    for d in T3D_DIRS:
        p = os.path.join(d, name + ".t3d")
        if os.path.exists(p):
            raw = open(p, "rb").read()
            if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
                return raw.decode("utf-16")
            return raw.decode("utf-8", errors="replace")
    raise FileNotFoundError(name)


def parse_abp(name):
    txt = open(os.path.join(CHAR, name + ".txt"), encoding="utf-8").read()
    nodes = {}
    cur = None
    for ln in txt.splitlines():
        m = re.match(r"  NODE (\S+) : (\S+)", ln)
        if m:
            cur = m.group(1)
            first = cur not in nodes
            n = nodes.setdefault(cur, {"class": m.group(2), "node": None, "in": {}, "bind": {}, "cache": None, "vals": {}})
            n["_first"] = first
            continue
        if cur is None:
            continue
        n = nodes[cur]
        m = re.match(r"\s+Node = (.*)", ln)
        if m and n["node"] is None:
            n["node"] = ue_text.parse(m.group(1))
            continue
        m = re.match(r"\s+in  (\S+) <= (\S+)", ln)
        if m and m.group(2) != "-" and n["_first"]:
            n["in"][m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s+in  (\S+) = (.*)", ln)
        if m and n["_first"]:
            n["vals"][m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s+BIND (\S+) <- (\S+)", ln)
        if m:
            n["bind"][m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s+SaveCachedPoseNode = .*:[^.]*\.(AnimGraphNode_SaveCachedPose_\d+)'", ln)
        if m:
            n["cache"] = m.group(1)
    return nodes


def parse_cdo(name):
    """class default object T3D (tools/ue/out/cdo): top level properties -> raw text"""
    path = os.path.join(ROOT, "tools", "ue", "out", "cdo", name + ".t3d")
    out = {}
    if not os.path.exists(path):
        return out
    for ln in open(path, encoding="utf-8", errors="replace").read().splitlines():
        m = re.match(r"^   ([^=]+?)=(.*)$", ln)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def chain(nodes, cdo=None):
    """Root -> input pose, returned input-first."""
    out = []
    cur = nodes["AnimGraphNode_Root_0"]["in"].get("Result")
    seen = set()
    while cur and cur not in seen:
        seen.add(cur)
        n = nodes[cur]
        c = n["class"]
        out.append((cur, n))
        if c == "AnimGraphNode_UseCachedPose":
            cur = n["cache"]
            continue
        if c == "AnimGraphNode_BlendListByBool":
            if n["bind"].get("bActiveValue") == "ShouldDisablePhysics":
                cur = n["in"].get("BlendPose_1")
                continue
            var = n["vals"].get("bActiveValue", "")
            if cdo is not None and var in cdo:
                cur = n["in"].get("BlendPose_0" if cdo[var] == "True" else "BlendPose_1")
                continue
            if "GetCurveValue(CurveName='HeadControlSwitch')" in var:
                # HeadMovementIK only runs while a HeadControlSwitch curve (face mocap) is above 0.5; GASP has none
                cur = n["in"].get("BlendPose_1")
                continue
            raise RuntimeError("unhandled BlendListByBool " + cur)
        src = None
        for k in ("SourcePose", "ComponentPose", "LocalPose", "Pose", "Source", "InPose", "BasePose", "AnimSequence"):
            if k in n["in"]:
                src = n["in"][k]
                break
        cur = src
    out.reverse()
    return out


def ref_bone(d):
    return d.get("BoneName") if isinstance(d, dict) else None


# ---------------------------------------------------------------------------------------------------------------- pose asset
def parse_pose_asset(path):
    name = path.split("/")[-1]
    txt = read_t3d(name)
    m = re.search(r"^   PoseContainer=(.*)$", txt, re.M)
    pc = ue_text.parse(m.group(1))
    additive = "bAdditivePose=True" in txt
    bpi = re.search(r"BasePoseIndex=(-?\d+)", txt)
    tracks = pc["Tracks"]
    poses = []
    for p in pc["Poses"]:
        lsp = p.get("LocalSpacePose", [])
        poses.append([xf(t) for t in lsp])
    infl = []
    for t in pc.get("TrackPoseInfluenceIndices", []):
        lst = t.get("Influences", []) if isinstance(t, dict) else []
        infl.append([[int(i["PoseIndex"]), int(i.get("BoneTransformIndex", 0))] for i in lst])
    return {"name": name, "additive": additive, "base_pose_index": int(bpi.group(1)) if bpi else (0 if additive else -1),
            "pose_names": pc["PoseFNames"], "tracks": tracks, "poses": poses, "influences": infl}


def xf(t):
    if not isinstance(t, dict):
        return [0, 0, 0, 1, 0, 0, 0, 1, 1, 1]
    q = quat(t.get("Rotation"))
    p = vec(t.get("Translation"))
    s = vec(t.get("Scale3D"), (1.0, 1.0, 1.0))
    return q + p + s


# ------------------------------------------------------------------------------------------------------------- physics asset
def parse_physics_asset(path):
    name = path.split("/")[-1]
    txt = read_t3d(name)
    objs = {}
    cur = None
    depth = 0
    for ln in txt.splitlines():
        s = ln.strip()
        m = re.match(r'Begin Object (?:Class=(\S+) )?Name="([^"]+)"', s)
        if m:
            depth += 1
            if depth == 2:
                cur = objs.setdefault(m.group(2), {"class": None, "props": {}})
                if m.group(1):
                    cur["class"] = m.group(1).split(".")[-1]
            continue
        if s == "End Object":
            depth -= 1
            if depth < 2:
                cur = None
            continue
        if cur is not None and depth == 2 and "=" in s:
            k, _, v = s.partition("=")
            cur["props"][k] = v
    order = re.findall(r"SkeletalBodySetups\(\d+\)=\"[^']*'(\w+)'", txt)
    corder = re.findall(r"ConstraintSetup\(\d+\)=\"[^']*'(\w+)'", txt)
    bodies = []
    for bname in order:
        p = objs[bname]["props"]
        geo = ue_text.parse(p.get("AggGeom", "()")) or {}
        inst = ue_text.parse(p.get("DefaultInstance", "()")) or {}
        if isinstance(inst, list):
            inst = {}
        shapes = []
        for e in [x for x in (geo.get("SphylElems", []) if isinstance(geo, dict) else []) if isinstance(x, dict)]:
            shapes.append({"type": "capsule", "center": vec(e.get("Center")), "rot": rot(e.get("Rotation")),
                           "radius": f(e.get("Radius"), 1.0), "length": f(e.get("Length"), 1.0)})
        for e in [x for x in (geo.get("SphereElems", []) if isinstance(geo, dict) else []) if isinstance(x, dict)]:
            shapes.append({"type": "sphere", "center": vec(e.get("Center")), "rot": [0, 0, 0], "radius": f(e.get("Radius"), 1.0), "length": 0.0})
        for e in [x for x in (geo.get("BoxElems", []) if isinstance(geo, dict) else []) if isinstance(x, dict)]:
            shapes.append({"type": "box", "center": vec(e.get("Center")), "rot": rot(e.get("Rotation")),
                           "x": f(e.get("X"), 1.0), "y": f(e.get("Y"), 1.0), "z": f(e.get("Z"), 1.0)})
        for e in [x for x in (geo.get("TaperedCapsuleElems", []) if isinstance(geo, dict) else []) if isinstance(x, dict)]:
            shapes.append({"type": "capsule", "center": vec(e.get("Center")), "rot": rot(e.get("Rotation")),
                           "radius": 0.5 * (f(e.get("Radius0"), 1.0) + f(e.get("Radius1"), 1.0)), "length": f(e.get("Length"), 1.0)})
        bodies.append({
            "bone": p.get("BoneName", "").strip('"'),
            "simulated": p.get("PhysicsType", "PhysType_Default") != "PhysType_Kinematic",
            "physics_type": p.get("PhysicsType", "PhysType_Default"),
            "collision": p.get("CollisionResponse", "BodyCollision_Enabled") != "BodyCollision_Disabled",
            "shapes": shapes,
            "override_mass": inst.get("bOverrideMass") == "True",
            "mass": f(inst.get("MassInKgOverride"), 100.0),
            "mass_scale": f(inst.get("MassScale"), 1.0),
            "linear_damping": f(inst.get("LinearDamping"), 0.01),
            "angular_damping": f(inst.get("AngularDamping"), 0.0),
            "inertia_scale": vec(inst.get("InertiaTensorScale"), (1.0, 1.0, 1.0)),
            "com_nudge": vec(inst.get("COMNudge")),
            "enable_gravity": inst.get("bEnableGravity", "True") != "False",
        })
    joints = []
    for cname in corder:
        p = objs[cname]["props"]
        d = ue_text.parse(p.get("DefaultInstance", "()")) or {}
        prof = d.get("ProfileInstance", {}) if isinstance(d, dict) else {}
        if not isinstance(prof, dict):
            prof = {}
        lin = prof.get("LinearLimit", {}) or {}
        cone = prof.get("ConeLimit", {}) or {}
        tw = prof.get("TwistLimit", {}) or {}
        drv = prof.get("AngularDrive", {}) or {}
        if not isinstance(lin, dict): lin = {}
        if not isinstance(cone, dict): cone = {}
        if not isinstance(tw, dict): tw = {}
        if not isinstance(drv, dict): drv = {}

        def drive(k):
            x = drv.get(k, {}) or {}
            if not isinstance(x, dict):
                x = {}
            return {"stiffness": f(x.get("Stiffness"), 50.0), "damping": f(x.get("Damping"), 1.0), "max_force": f(x.get("MaxForce"), 0.0),
                    "position": x.get("bEnablePositionDrive") == "True", "velocity": x.get("bEnableVelocityDrive") == "True"}
        joints.append({
            "bone1": d.get("ConstraintBone1", ""), "bone2": d.get("ConstraintBone2", ""),
            "pos1": vec(d.get("Pos1")), "pri1": vec(d.get("PriAxis1"), (1.0, 0.0, 0.0)), "sec1": vec(d.get("SecAxis1"), (0.0, 1.0, 0.0)),
            "pos2": vec(d.get("Pos2")), "pri2": vec(d.get("PriAxis2"), (1.0, 0.0, 0.0)), "sec2": vec(d.get("SecAxis2"), (0.0, 1.0, 0.0)),
            "linear": {"x": lin.get("XMotion", "LCM_Locked"), "y": lin.get("YMotion", "LCM_Locked"), "z": lin.get("ZMotion", "LCM_Locked"),
                       "limit": f(lin.get("Limit"), 0.0), "soft": lin.get("bSoftConstraint") == "True",
                       "stiffness": f(lin.get("Stiffness"), 0.0), "damping": f(lin.get("Damping"), 0.0)},
            "swing1": cone.get("Swing1Motion", "ACM_Free"), "swing2": cone.get("Swing2Motion", "ACM_Free"),
            "swing1_limit": f(cone.get("Swing1LimitDegrees"), 45.0), "swing2_limit": f(cone.get("Swing2LimitDegrees"), 45.0),
            "cone_soft": cone.get("bSoftConstraint", "True") != "False", "cone_stiffness": f(cone.get("Stiffness"), 50.0), "cone_damping": f(cone.get("Damping"), 5.0),
            "twist": tw.get("TwistMotion", "ACM_Free"), "twist_limit": f(tw.get("TwistLimitDegrees"), 45.0),
            "twist_soft": tw.get("bSoftConstraint", "True") != "False", "twist_stiffness": f(tw.get("Stiffness"), 50.0), "twist_damping": f(tw.get("Damping"), 5.0),
            "drive_mode": drv.get("AngularDriveMode", "SLERP"), "slerp": drive("SlerpDrive"), "swing_drive": drive("SwingDrive"), "twist_drive": drive("TwistDrive"),
            "orientation_target": rot(drv.get("OrientationTarget")),
            "disable_collision": prof.get("bDisableCollision") == "True",
        })
    solver = ue_text.parse(re.search(r"^   SolverSettings=(.*)$", txt, re.M).group(1)) if re.search(r"^   SolverSettings=", txt, re.M) else {}
    return {"name": name, "bodies": bodies, "joints": joints, "solver": solver if isinstance(solver, dict) else {}}


# ---------------------------------------------------------------------------------------------------------------- node build
def build_node(nid, n, rigs, cdo=None):
    c = n["class"].replace("AnimGraphNode_", "")
    d = n["node"] or {}
    if cdo is not None:
        # compiled anim node values (the graph text omits pins left at their defaults, e.g. PoseDriver DriveOutput)
        keys = [k for k in cdo if re.fullmatch(n["class"] + r"(_\d+)?", k)]
        if c == "ControlRig":
            want = str(d.get("ControlRigClass", ""))
            keys = [k for k in keys if want and want.split(".")[-1].rstrip("'") in cdo[k]]
        if len(keys) == 1:
            d = ue_text.parse(cdo[keys[0]])
    if c in ("LinkedInputPose",):
        return {"type": "input"}
    if c == "CopyPoseFromMesh":
        return {"type": "copy_pose", "copy_curves": d.get("bCopyCurves") == "True"}
    if c in ("LocalToComponentSpace", "ComponentToLocalSpace", "SaveCachedPose", "UseCachedPose", "BlendListByBool", "Root"):
        return None
    if c == "ModifyCurve":
        names = d.get("CurveNames", []) or []
        vals = [f(v) for v in (d.get("CurveValues", []) or [])]
        return {"type": "modify_curve", "names": names, "values": vals[:len(names)], "mode": d.get("ApplyMode", "Blend"),
                "alpha": f(n["vals"].get("Alpha"), 1.0)}
    if c == "PoseDriver":
        rbf = d.get("RBFParams", {}) or {}
        node = {
            "type": "pose_driver", "name": nid,
            "source_bones": [ref_bone(b) for b in d.get("SourceBones", [])],
            "only_drive_bones": [ref_bone(b) for b in d.get("OnlyDriveBones", [])],
            "eval_space_bone": ref_bone(d.get("EvalSpaceBone")),
            "eval_from_ref_pose": d.get("bEvalFromRefPose") == "True",
            "drive_source": d.get("DriveSource", "Rotation"),
            "drive_output": d.get("DriveOutput", "DrivePoses"),
            "solver": rbf.get("SolverType", "Additive"), "radius": f(rbf.get("Radius"), 1.0),
            "automatic_radius": rbf.get("bAutomaticRadius") == "True",
            "function": rbf.get("Function", "Gaussian"), "distance": rbf.get("DistanceMethod", "SwingAngle"),
            "twist_axis": rbf.get("TwistAxis", "BA_X"), "weight_threshold": f(rbf.get("WeightThreshold"), 1e-4),
            "normalize": rbf.get("NormalizeMethod", "OnlyNormalizeAboveOne"),
            "median_reference": vec(rbf.get("MedianReference")), "median_min": f(rbf.get("MedianMin"), 45.0), "median_max": f(rbf.get("MedianMax"), 60.0),
            "lod_threshold": int(f(d.get("LODThreshold"), -1)),
            "targets": [],
        }
        for t in d.get("PoseTargets", []) or []:
            bts = t.get("BoneTransforms", []) or []
            node["targets"].append({
                "rot": [rot(b.get("TargetRotation")) for b in bts], "trans": [vec(b.get("TargetTranslation")) for b in bts],
                "name": t.get("DrivenName", "None"), "scale": f(t.get("TargetScale"), 1.0),
                "distance": t.get("DistanceMethod", "DefaultMethod"), "function": t.get("FunctionType", "DefaultFunction"),
                "custom_curve": t.get("bApplyCustomCurve") == "True",
            })
        pa = asset_name(d.get("PoseAsset"))
        if pa and node["drive_output"] == "DrivePoses":
            node["pose_asset"] = parse_pose_asset(pa)
        return node
    if c == "Constraint":
        return {"type": "constraint", "bone": ref_bone(d.get("BoneToModify")),
                "targets": [{"bone": ref_bone(s.get("TargetBone")), "transform_type": s.get("TransformType", "Parent"),
                             "offset": s.get("OffsetOption", "Offset_RefPose")} for s in d.get("ConstraintSetup", [])],
                "weights": [f(w) for w in d.get("ConstraintWeights", [])]}
    if c == "ModifyBone":
        return {"type": "modify_bone", "bone": ref_bone(d.get("BoneToModify")), "raw": d, "bind": n["bind"]}
    if c == "RigidBody":
        pa = asset_name(d.get("OverridePhysicsAsset"))
        return {"type": "rigid_body", "name": nid, "physics_asset": parse_physics_asset(pa),
                "gravity": vec(d.get("OverrideWorldGravity"), (0.0, 0.0, -980.0)) if d.get("bOverrideWorldGravity") == "True" else [0.0, 0.0, -980.0],
                "acc_scale": vec(d.get("ComponentLinearAccScale")), "vel_scale": vec(d.get("ComponentLinearVelScale")),
                "acc_clamp": vec(d.get("ComponentAppliedLinearAccClamp"), (10000.0, 10000.0, 10000.0)),
                "base_bone": ref_bone(d.get("BaseBoneRef")) or "root", "space": d.get("SimulationSpace", "ComponentSpace"),
                "clamp_linear_to_ref": d.get("bClampLinearTranslationLimitToRefPose") == "True",
                "lod_threshold": int(f(d.get("LODThreshold"), -1))}
    if c == "ControlRig":
        cls = str(d.get("ControlRigClass", ""))
        rig = cls.split(".")[-1].rstrip("'").removesuffix("_C")
        node = {"type": "control_rig", "rig": rig, "ops": rigs.get(rig), "lod_threshold": int(f(d.get("LODThreshold"), -1))}
        path = next((os.path.join(dd, rig + ".t3d") for dd in T3D_DIRS if os.path.exists(os.path.join(dd, rig + ".t3d"))), None)
        if path:
            import rigvm_compile
            node["program"] = rigvm_compile.compile_rig(path)
            # exposed rig variables set on the anim node pins
            vv = {}
            for k, v in n["vals"].items():
                if k in node["program"]["vars"]:
                    try:
                        vv[k] = [float(v)]
                    except ValueError:
                        pass
            node["var_values"] = vv
        return node
    if c in ("LayeredBoneBlend", "LiveLinkPose", "PoseBlendNode", "Slot", "TwoBoneIK"):
        return {"type": c.lower(), "raw": d}
    raise RuntimeError("unhandled node " + nid + " " + c)


def build(abp, out_name=None, cdo_name=None):
    import rigs
    nodes = parse_abp(abp)
    cdo = parse_cdo(cdo_name) if cdo_name else None
    prog = []
    for nid, n in chain(nodes, cdo):
        b = build_node(nid, n, rigs.RIGS, cdo)
        if b is not None:
            prog.append(b)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, (out_name or abp) + ".json")
    json.dump({"abp": abp, "nodes": prog}, open(path, "w"), separators=(",", ":"))
    print(abp, "->", [p["type"] + (":" + p.get("rig", "") if p["type"] == "control_rig" else "") for p in prog], os.path.getsize(path) // 1024, "KB")


if __name__ == "__main__":
    for a in (sys.argv[1:] or ["ABP_Echo_PostProcess", "ABP_Echo_Hair"]):
        build(a)
