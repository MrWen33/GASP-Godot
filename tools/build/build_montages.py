"""Parse traversal AnimMontage T3Ds -> godot/data/montages.json.

Per montage: blend in/out (FAlphaBlend time + option), BlendOutTriggerTime, motion warping windows
(RootMotionModifier_SkewWarp params) and BP_NotifyState_MontageBlendOut windows.
usage: build_montages.py <t3d_utf8 root> <out.json>
"""
import json, os, re, sys

ROOT = sys.argv[1]
OUT = sys.argv[2]

# CDO defaults
BLENDOUT_DEFAULT = {"time": 0.2, "profile": "FastFeet_InstantRoot", "condition": 0}
WARP_DEFAULT = {"warp_translation": True, "ignore_z": True, "warp_rotation": True, "rotation_type": "Default",
                "rot_mult": 1.0, "provider": "None", "bone": "", "max_rot_rate": 0.0, "rotation_method": "Slerp"}


def split_top(s):
    out, depth, cur, q = [], 0, '', False
    for ch in s:
        if ch == '"':
            q = not q
        if not q:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                out.append(cur); cur = ''; continue
        cur += ch
    if cur:
        out.append(cur)
    return out


def kv(s):
    d = {}
    for p in split_top(s):
        if '=' in p:
            k, v = p.split('=', 1)
            d[k.strip()] = v.strip()
    return d


def parse_objects(text):
    """Return {object path: {prop: value}} for 'Begin Object Name=' property blocks (the 2nd, filled pass). Nested objects
    are also keyed by every suffix of their outer path ("AnimNotifyState_MotionWarping_1.RootMotionModifier_SkewWarp"): every warping notify
    owns a modifier with the same name."""
    objs = {}
    stack = []
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r'Begin Object (?:Class=\S+ )?Name="([^"]+)"', s)
        if m:
            stack.append(m.group(1))
            for i in range(len(stack)):
                objs.setdefault(".".join(stack[i:]), {})
            continue
        if s == 'End Object':
            if stack:
                stack.pop()
            continue
        if stack and '=' in s:
            k, v = s.split('=', 1)
            for i in range(len(stack)):
                objs[".".join(stack[i:])][k] = v
    return objs


def blend(s, default_time=0.25):
    d = kv(s[1:-1]) if s else {}
    return {"time": float(d.get("BlendTime", default_time)), "option": d.get("BlendOption", "Linear")}


def main():
    out = {}
    for dp, _, fs in os.walk(ROOT):
        for f in fs:
            if not (f.startswith('AM_') and f.endswith('.t3d')):
                continue
            text = open(os.path.join(dp, f), encoding='utf-8').read()
            m = re.search(r'Class=/Script/Engine.AnimMontage Name="([^"]+)" ExportPath="[^\']*\'([^\']+)\'', text)
            if not m:
                continue
            path = m.group(2).split('.')[0]
            objs = parse_objects(text)
            top = {}
            for line in text.splitlines():
                if line.startswith('   ') and not line.startswith('    ') and '=' in line:
                    k, v = line.strip().split('=', 1)
                    top[k] = v
            entry = {
                "blend_in": blend(top.get("BlendIn")),
                "blend_out": blend(top.get("BlendOut")),
                "blend_out_trigger": float(top.get("BlendOutTriggerTime", -1.0)),
                "auto_blend_out": top.get("bEnableAutoBlendOut", "True") == "True",
                "warps": [], "blendouts": [],
            }
            # UAnimMontage::RawCurveData (montage float curves, e.g. MovingTraversal = 1 on hurdles / mantles / vaults)
            entry["curves"] = []
            for cm in re.finditer(r'FloatCurve=\(Keys=\(((?:\([^()]*\),?)*)\)[^"]*?CurveName="([^"]+)"', top.get("RawCurveData", "")):
                keys = []
                for km in re.finditer(r'\(([^()]*)\)', cm.group(1)):
                    kd = dict(x.split('=', 1) for x in km.group(1).split(',') if '=' in x)
                    keys.append([float(kd.get("Time", 0.0)), float(kd.get("Value", 0.0))])
                if keys:
                    entry["curves"].append({"name": cm.group(2), "keys": keys})
            seg = re.search(r'SegmentLength=([0-9.]+)', top.get("CompositeSections(0)", ""))
            entry["length"] = float(seg.group(1)) if seg else 0.0
            for k, v in top.items():
                if not k.startswith('Notifies('):
                    continue
                d = kv(v[1:-1])
                cls = d.get("NotifyStateClass", "")
                om = re.search(r"'([^']+)'\"?$", cls)
                if not om:
                    continue
                oname = om.group(1)
                start = float(d.get("LinkValue", 0.0))
                if "TriggerTimeOffset" in d:
                    start += 0.0
                dur = float(d.get("Duration", 0.0))
                o = objs.get(oname, {})
                if "AnimNotifyState_MotionWarping" in cls:
                    mod = o.get("RootMotionModifier", '').strip('"')
                    mo = objs.get(oname + "." + mod, objs.get(mod, {}))
                    w = dict(WARP_DEFAULT)
                    w.update({"start": start, "end": start + dur, "target": mo.get("WarpTargetName", '""').strip('"')})
                    if "bWarpTranslation" in mo: w["warp_translation"] = mo["bWarpTranslation"] == "True"
                    if "bIgnoreZAxis" in mo: w["ignore_z"] = mo["bIgnoreZAxis"] == "True"
                    if "bWarpRotation" in mo: w["warp_rotation"] = mo["bWarpRotation"] == "True"
                    if "RotationType" in mo: w["rotation_type"] = mo["RotationType"]
                    if "WarpRotationTimeMultiplier" in mo: w["rot_mult"] = float(mo["WarpRotationTimeMultiplier"])
                    if "WarpPointAnimProvider" in mo: w["provider"] = mo["WarpPointAnimProvider"]
                    if "WarpPointAnimBoneName" in mo: w["bone"] = mo["WarpPointAnimBoneName"].strip('"')
                    if "WarpMaxRotationRate" in mo: w["max_rot_rate"] = float(mo["WarpMaxRotationRate"])
                    if "RotationMethod" in mo: w["rotation_method"] = mo["RotationMethod"]
                    entry["warps"].append(w)
                elif "MontageBlendOut" in cls:
                    b = dict(BLENDOUT_DEFAULT)
                    b.update({"start": start, "end": start + dur})
                    if "BlendOutTime" in o: b["time"] = float(o["BlendOutTime"])
                    if "BlendProfile" in o: b["profile"] = o["BlendProfile"].strip('"')
                    if "BlendOutCondition" in o: b["condition"] = int(o["BlendOutCondition"].split('NewEnumerator')[-1])
                    entry["blendouts"].append(b)
            entry["warps"].sort(key=lambda w: w["start"])
            out[path] = entry
    json.dump(out, open(OUT, 'w'), indent=1)
    print(len(out), 'montages;', sum(len(e['warps']) for e in out.values()), 'warp windows')


main()
