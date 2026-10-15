"""Parse the UMG widget trees of GASP's editor utility widgets (T3D, UTF-8 copies in t3d_widgets/u) into
godot/data/widgets.json: nested {class, name, props, slot, children} with user-widget instances kept as leaves."""
import re, json, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "tools", "ue", "out", "t3d_widgets", "u")
OUT = os.path.join(ROOT, "godot", "data", "widgets.json")
WIDGETS = ["ReadMe", "Controls", "GameAnimationWidget", "WB_SimpleController", "WB_SimpleTextBlock",
           "EUW_SimpleConsoleButton", "EUW_SimpleConsoleSpinBox", "EUW_CharacterSelectButton", "StillCam"]

def parse_tree(text):
    lines = text.splitlines()
    # class declarations (first pass): name -> class
    classes = {}
    for l in lines:
        m = re.match(r'\s*Begin Object Class=([^ ]+) Name="([^"]+)"', l)
        if m:
            classes.setdefault(m.group(2), m.group(1).split(".")[-1])
    # property definitions: second pass blocks "Begin Object Name=..." inside the WidgetTree definition
    start = None
    for i, l in enumerate(lines):
        if re.match(r'   Begin Object Name="WidgetTree"', l):
            start = i
            break
    if start is None:
        return None
    objs = {}
    stack = []
    root_widget = None
    for l in lines[start + 1:]:
        if re.match(r'   End Object', l):
            break
        m = re.match(r'(\s*)Begin Object (?:Class=[^ ]+ )?Name="([^"]+)"', l)
        if m:
            stack.append(m.group(2))
            objs.setdefault(".".join(stack), {})
            continue
        if re.match(r'\s*End Object', l):
            if stack:
                stack.pop()
            continue
        mm = re.match(r'\s*([A-Za-z0-9_][A-Za-z0-9_ ]*?(?:\(\d+\))?)=(.*)$', l)
        if not mm:
            continue
        key, val = mm.group(1), mm.group(2)
        if not stack:
            if key == "RootWidget":
                root_widget = re.search(r"'([^']+)'", val).group(1).split(".")[-1].split(":")[-1]
            continue
        objs[".".join(stack)][key] = val
    def ref(v):
        m = re.search(r"'([^']+)'\"?$", v)
        return m.group(1).split(".")[-1].split(":")[-1] if m else None
    def build(name):
        props = objs.get(name, {})
        node = {"name": name, "class": classes.get(name, "?"), "props": {}, "children": []}
        slots = []
        for k, v in props.items():
            if re.match(r"Slots\(\d+\)", k):
                slots.append((int(k[6:-1]), ref(v)))
            elif k not in ("Slot", "bIsVariable", "DisplayLabel", "bExpandedInDesigner", "WidgetTree", "CategoryName"):
                node["props"][k] = v
        if "Slot" in props:
            pass
        for k in ("HeaderContent", "BodyContent"):
            if k in props and ref(props[k]):
                c = build(ref(props[k]))
                c["slot"] = {"class": k}
                node["children"].append(c)
                node["props"].pop(k, None)
        for _, sname in sorted(slots):
            sp = objs.get(name + "." + sname, {})
            child = sp.get("Content")
            if not child:
                continue
            c = build(ref(child))
            c["slot"] = {k: v for k, v in sp.items() if k not in ("Parent", "Content")}
            c["slot"]["class"] = classes.get(sname, "?")
            node["children"].append(c)
        return node
    return build(root_widget) if root_widget else None

def text_of(v):
    m = re.search(r'NSLOCTEXT\("[^"]*", "[^"]*", "((?:[^"\\]|\\.)*)"\)', v) or re.search(r'INVTEXT\("((?:[^"\\]|\\.)*)"\)', v)
    if m:
        return m.group(1).replace("\\r\\n", "\n").replace("\r\n", "\n").replace('\\"', '"')
    return None

res = {}
for w in WIDGETS:
    p = os.path.join(SRC, w + ".t3d")
    if not os.path.exists(p):
        continue
    t = open(p, encoding="utf-8", errors="ignore").read()
    tree = parse_tree(t)
    # decode texts in place
    def walk(n):
        for k, v in list(n["props"].items()):
            tx = text_of(v)
            if tx is not None:
                n["props"][k] = {"text": tx}
        for c in n["children"]:
            walk(c)
    if tree:
        walk(tree)
    # class defaults of the widget blueprint (generated class CDO) for user widgets
    res[w] = tree
json.dump(res, open(OUT, "w"), indent=1)
for k, v in res.items():
    def count(n):
        return 1 + sum(count(c) for c in n["children"]) if n else 0
    print(k, count(v), v["class"] if v else None)
