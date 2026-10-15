"""Prints a UE material (or material function) T3D expression graph as nested expressions starting from the material
inputs (BaseColor, ...) or function outputs. Usage: mat_graph.py <t3d> [max_depth]"""
import re, sys
txt = open(sys.argv[1], encoding="utf-8", errors="replace").read()
depth_max = int(sys.argv[2]) if len(sys.argv) > 2 else 12
objs = {}
cur = None
for ln in txt.splitlines():
    s = ln.strip()
    m = re.match(r'Begin Object (?:Class=(\S+) )?Name="([^"]+)"', s)
    if m:
        cur = objs.setdefault(m.group(2), {"class": "", "props": {}})
        if m.group(1):
            cur["class"] = m.group(1).split(".")[-1]
        continue
    if s == "End Object":
        cur = None
        continue
    if cur is not None and "=" in s:
        k, _, v = s.partition("=")
        cur["props"].setdefault(k, v)

def refs(v):
    return re.findall(r"Expression=\"[^']*'[^:']*:?([^']+)'\"", v)

def show(name, d=0, out_idx=None):
    o = objs.get(name)
    if o is None:
        return name
    c = o["class"].replace("MaterialExpression", "")
    p = o["props"]
    if d > depth_max:
        return c + "(...)"
    if c == "Reroute" or c == "NamedRerouteUsage":
        r = refs(p.get("Input", "")) or refs(p.get("Declaration", ""))
        if c == "NamedRerouteUsage":
            decl = re.search(r":([^']+)'", p.get("Declaration", ""))
            return show(decl.group(1), d) if decl else "?"
        return show(r[0], d) if r else "?"
    if c == "NamedRerouteDeclaration":
        r = refs(p.get("Input", ""))
        return show(r[0], d) if r else "?"
    args = []
    for k, v in p.items():
        if k in ("Input", "A", "B", "Alpha", "Coordinates", "UVs", "Texture") or k.startswith("FunctionInputs"):
            for r in refs(v):
                oi = re.search(r"OutputIndex=(\d+)", v)
                args.append(f"{k}={show(r, d + 1)}" + (f"[out{oi.group(1)}]" if oi else ""))
    extra = []
    for k in ("ParameterName", "InputName", "Desc", "R", "G", "B", "A", "ConstA", "ConstB", "DefaultValue", "Texture", "MaterialFunction", "OutputName"):
        if k in p and not refs(p[k]):
            extra.append(f"{k}={p[k][:90]}")
    return f"{c}<{', '.join(extra)}>({', '.join(args)})"

for name, o in objs.items():
    if o["class"] in ("Material", "MaterialEditorOnlyData", "MaterialFunctionEditorOnlyData"):
        for k, v in o["props"].items():
            if refs(v) and k in ("BaseColor", "Metallic", "Specular", "Roughness", "Normal", "EmissiveColor", "OpacityMask", "AmbientOcclusion"):
                print(f"== {k}:", show(refs(v)[0]))
    if o["class"] == "MaterialExpressionFunctionOutput":
        print(f"== OUT {o['props'].get('OutputName', name)}:", show(name))
