import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Extracts the ABP_SandboxCharacter "State Controller" state machine from the ABP T3D: states, aliases, transitions
(endpoints, priority, crossfade, notify name) and, per transition rule graph, the K2Node_AnimGetter functions and literal
pin defaults that the text dump loses. Writes tools/ue/out/sm_raw.json and prints a summary."""
import json, re, sys

T3D = REPO + r"/tools/ue/out/t3d_utf8/Blueprints/ABP_SandboxCharacter.t3d"
OUT = REPO + r"/tools/ue/out/sm_raw.json"
PREFIX = "AnimGraph.AnimGraphNode_StateMachine_1.State Controller"

lines = open(T3D, encoding="utf-8").read().split("\n")

# parse nested objects: collect (class, name, exportpath, body lines (direct children only, excluding nested Begin/End))
objs = []
stack = []
for ln in lines:
    s = ln.strip()
    m = re.match(r'Begin Object Class=(\S+) Name="([^"]+)"(?: ExportPath="([^"]+)")?', s)
    if m:
        stack.append({"class": m.group(1), "name": m.group(2), "path": m.group(3) or "", "body": []})
        continue
    if s.startswith("Begin Object Name="):
        m = re.match(r'Begin Object Name="([^"]+)"(?: ExportPath="([^"]+)")?', s)
        stack.append({"class": "", "name": m.group(1), "path": m.group(2) or "", "body": []})
        continue
    if s == "End Object":
        o = stack.pop()
        objs.append(o)
        continue
    if stack:
        stack[-1]["body"].append(s)

def props(o):
    d = {}
    for b in o["body"]:
        if b.startswith("CustomProperties"):
            continue
        k, _, v = b.partition("=")
        d.setdefault(k, v)
    return d

def pins(o):
    res = []
    for b in o["body"]:
        if b.startswith("CustomProperties Pin "):
            p = {}
            for key in ["PinId", "PinName", "DefaultValue", "Direction", "LinkedTo"]:
                m = re.search(key + r'=("([^"]*)"|\(([^)]*)\)|([^,)]+))', b)
                if m:
                    p[key] = m.group(2) if m.group(2) is not None else (m.group(3) if m.group(3) is not None else m.group(4))
            res.append(p)
    return res

# merge declaration + definition blocks by export path; class from the export path ("/Script/Mod.Class'/Game/...'")
merged = {}
for o in objs:
    if not o["path"]:
        continue
    m = re.match(r"([^']+)'(.*)'$", o["path"])
    cls, path = (m.group(1), m.group(2)) if m else (o["class"], o["path"])
    e = merged.setdefault(path, {"class": cls, "name": o["name"], "path": path, "body": []})
    e["body"] += o["body"]
sm = [o for o in merged.values() if PREFIX in o["path"] and "ExecuteUbergraph" not in o["path"]]
byname = {}
for o in sm:
    if o["class"]:
        byname[o["name"]] = o  # last definition with class wins
# state nodes
nodes = {o["name"]: o for o in sm if o["path"].endswith("State Controller." + o["name"]) and o["class"]}
pinowner = {}
for n, o in nodes.items():
    for p in pins(o):
        if "PinId" in p:
            pinowner[p["PinId"]] = n
out = {"states": {}, "aliases": {}, "transitions": {}, "entry": None, "rules": {}}
for n, o in nodes.items():
    pr = props(o)
    cls = o["class"].split(".")[-1]
    links = {}
    for p in pins(o):
        tgt = []
        for l in (p.get("LinkedTo") or "").split(","):
            l = l.strip()
            if l:
                tgt.append(l.split(" ")[0])
        links[p.get("PinName", "?")] = tgt
    if cls in ("AnimStateNode", "AnimStateConduitNode"):
        bg = pr.get("BoundGraph", "")
        out["states"][n] = {"class": cls, "graph": re.sub(r".*'([^']*)'\"?$", r"\1", bg), "reset": pr.get("bAlwaysResetOnEntry", "False"), "out": links.get("Out", [])}
    elif cls == "AnimStateAliasNode":
        al = re.findall(r"State Controller\.(AnimStateNode_\d+)", pr.get("AliasedStateNodes", ""))
        out["aliases"][n] = {"name": pr.get("StateAliasName", "").strip('"'), "global": pr.get("bGlobalAlias", "False"), "states": al, "out": links.get("Out", [])}
    elif cls == "AnimStateTransitionNode":
        out["transitions"][n] = {"in": links.get("In", []), "out": links.get("Out", []), "priority": pr.get("PriorityOrder", "1"),
            "crossfade": pr.get("CrossfadeDuration", "0.2"), "blend_mode": pr.get("BlendMode", ""), "logic": pr.get("LogicType", ""),
            "notify": re.sub(r'.*NotifyName="([^"]*)".*', r"\1", pr.get("TransitionStart", "")) if "NotifyName" in pr.get("TransitionStart", "") else "",
            "shared": pr.get("SharedRulesName", "").strip('"'), "bound": pr.get("BoundGraph", ""), "automatic": pr.get("bAutomaticRuleBasedOnSequencePlayerInState", "False"),
            "auto_trigger": pr.get("AutomaticRuleTriggerTime", ""), "bidir": pr.get("Bidirectional", "False"), "custom_blend": pr.get("CustomBlendCurve", ""), "blend_profile": pr.get("BlendProfile", ""),
            "interrupt": pr.get("TransitionInterrupt", ""), "props": {k: v for k, v in pr.items() if k not in ("NodeGuid", "NodePosX", "NodePosY", "BoundGraph", "TransitionStart", "TransitionEnd", "TransitionInterrupt", "SharedColor")}}
    elif cls == "AnimStateEntryNode":
        out["entry"] = links

# rule graphs: objects whose path is inside "<transition>.Transition" graphs
for o in sm:
    m = re.search(r"State Controller\.(AnimState(?:Transition|Conduit)Node_\d+)\.(?:Transition|Conduit)\.(\S+?)'?$", o["path"])
    if not m or not o["class"]:
        continue
    t, node = m.group(1), m.group(2)
    pr = props(o)
    info = {"class": o["class"].split(".")[-1]}
    for k in ["FunctionReference", "GetterClass", "SourceAnimNode", "SourceStateNode", "CachedDisplayName", "GetterContext", "VariableReference", "CachedTitle", "ContextString"]:
        if k in pr:
            info[k] = pr[k]
    info["pins"] = [{k: v for k, v in p.items() if k in ("PinName", "DefaultValue", "LinkedTo")} for p in pins(o) if p.get("DefaultValue") or p.get("LinkedTo")]
    out["rules"].setdefault(t, {})[node] = info

json.dump(out, open(OUT, "w"), indent=1)
alias_names = {k: v["name"] for k, v in out["aliases"].items()}
state_names = {k: v["graph"] for k, v in out["states"].items()}
def nm(x):
    return state_names.get(x) or alias_names.get(x) or x
for t, v in sorted(out["transitions"].items(), key=lambda kv: int(kv[0].split("_")[-1])):
    print(t, ":", [nm(i) for i in v["in"]], "->", [nm(i) for i in v["out"]], "prio", v["priority"], "xfade", v["crossfade"], v["blend_mode"], v["logic"], "|", v["notify"], "|", v["shared"])
print("aliases:", json.dumps(out["aliases"], indent=0)[:3000])
print("entry:", out["entry"])
