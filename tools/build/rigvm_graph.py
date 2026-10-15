"""Prints a Control Rig blueprint T3D (RigVM model) as pseudo code: nodes in execution order, each input pin shown as
its link source or default value. Usage: rigvm_graph.py <t3d> [graph_name_filter]"""
import re, sys
from collections import defaultdict

txt = open(sys.argv[1], encoding="utf-8", errors="replace").read()
flt = sys.argv[2] if len(sys.argv) > 2 else None

cls = {}      # path -> class
props = defaultdict(dict)  # path -> {key: value}
stack = []
for ln in txt.splitlines():
    s = ln.strip()
    m = re.match(r'Begin Object (?:Class=(\S+) )?Name="([^"]+)"', s)
    if m:
        stack.append(m.group(2))
        p = ".".join(stack)
        if m.group(1):
            cls[p] = m.group(1).split(".")[-1]
        continue
    if s == "End Object":
        stack.pop()
        continue
    if stack and "=" in s:
        k, _, v = s.partition("=")
        props[".".join(stack)].setdefault(k, v)

graphs = [p for p, c in cls.items() if c in ("RigVMGraph", "RigVMFunctionLibrary") or (c == "RigVMCollapseNode")]
for g in graphs:
    if flt and flt not in g:
        continue
    depth = g.count(".") + 1
    nodes = [p for p, c in cls.items() if p.startswith(g + ".") and p.count(".") == depth and c.endswith("Node") and c != "RigVMCommentNode"]
    links = [p for p, c in cls.items() if p.startswith(g + ".") and p.count(".") == depth and c == "RigVMLink"]
    if not nodes:
        continue
    src_of = {}
    exec_next = defaultdict(list)
    for l in links:
        sp = props[l].get("SourcePinPath", "").strip('"'); tp = props[l].get("TargetPinPath", "").strip('"')
        src_of[tp] = sp
        if sp.endswith("ExecuteContext") or sp.split(".")[-1] in ("Completed", "ExecutePin", "Execute") or ".Branches" in sp:
            exec_next[sp.split(".")[0]].append((sp, tp))
    print("=" * 30, g.split(":")[-1] if ":" in g else g, len(nodes), "nodes")

    def short(n):
        return n[len(g) + 1:]

    def pin_val(pinpath, rel):
        # rel: pin path relative to node "Node.Pin.Sub"
        if rel in src_of:
            return "<" + src_of[rel] + ">"
        pr = props.get(pinpath, {})
        subs = [p for p in cls if p.startswith(pinpath + ".") and p.count(".") == pinpath.count(".") + 1]
        if subs:
            return "(" + ", ".join(p.split(".")[-1] + "=" + pin_val(p, rel + "." + p.split(".")[-1]) for p in subs) + ")"
        if "DefaultValue" in pr:
            return pr["DefaultValue"]
        return "?"

    def describe(n):
        pr = props[n]
        fn = pr.get("ResolvedFunctionName", pr.get("TemplateNotation", cls[n])).strip('"')
        if cls[n] == "RigVMVariableNode":
            fn = "Var[" + props.get(n + ".Variable", {}).get("DefaultValue", "?").strip('"') + "]"
        out = []
        pins = [p for p in cls if p.startswith(n + ".") and p.count(".") == n.count(".") + 1 and cls[p] == "RigVMPin"]
        for p in pins:
            pp = props[p]
            d = pp.get("Direction", "")
            nm = p.split(".")[-1]
            if nm in ("ExecuteContext", "CachedIndex", "WorkData", "CachedItem", "CachedSpace") or d in ("Hidden",):
                continue
            if d == "Output":
                continue
            out.append(f"{nm}={pin_val(p, short(p))}")
        outs = [p.split(".")[-1] for p in pins if props[p].get("Direction") == "Output"]
        return f"{short(n)}: {fn}({', '.join(out)}) -> {outs}"

    seen = set()
    roots = [n for n in nodes if "BeginExecution" in short(n) or "Event" in props[n].get("ResolvedFunctionName", "")
             or short(n).startswith("Entry") or "PrepareForExecution" in props[n].get("ResolvedFunctionName", "")]

    def walk(n, ind=0):
        if n in seen:
            return
        seen.add(n)
        print("  " * ind + describe(n))
        for sp, tp in exec_next.get(short(n), []):
            tn = g + "." + tp.split(".")[0]
            if tn in cls:
                lbl = sp.split(".", 1)[1]
                if lbl not in ("ExecuteContext",):
                    print("  " * (ind + 1) + f"[{lbl}]")
                walk(tn, ind + (1 if lbl != "ExecuteContext" else 0))

    for r in roots:
        walk(r)
    rest = [n for n in nodes if n not in seen]
    if rest:
        print("  -- data nodes --")
        for n in rest:
            print("  " + describe(n))
