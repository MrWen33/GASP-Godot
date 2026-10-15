"""Convert UE Blueprint T3D (UTF-8) into readable pseudo-code, one section per EdGraph.

Usage: python bp2text.py <in.t3d> <out.txt>
"""
import re, sys
from collections import OrderedDict

PIN_RE = re.compile(r'CustomProperties Pin \((.*)\)\s*$')

def split_top(s, sep=","):
    """Split by sep at paren/quote depth 0."""
    out, depth, cur, q = [], 0, [], False
    i = 0
    while i < len(s):
        c = s[i]
        if c == '"' and (i == 0 or s[i-1] != '\\'):
            q = not q
        elif not q:
            if c in "([": depth += 1
            elif c in ")]": depth -= 1
            elif c == sep and depth == 0:
                out.append("".join(cur)); cur = []; i += 1; continue
        cur.append(c); i += 1
    if cur: out.append("".join(cur))
    return out

def kv(s):
    d = OrderedDict()
    for part in split_top(s):
        if "=" in part:
            k, v = part.split("=", 1)
            d[k.strip()] = v.strip()
    return d

def unq(v):
    if v is None: return None
    v = v.strip()
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"': v = v[1:-1]
    return v

def short_obj(v):
    v = unq(v) or ""
    m = re.search(r"'([^']*)'", v)
    if m: v = m.group(1)
    return v.split(".")[-1].split(":")[-1]

class Pin:
    def __init__(self, d):
        self.id = d.get("PinId")
        self.name = unq(d.get("PinName"))
        self.friendly = d.get("PinFriendlyName")
        self.out = d.get("Direction") == '"EGPD_Output"'
        self.cat = unq(d.get("PinType.PinCategory"))
        self.sub = short_obj(d.get("PinType.PinSubCategoryObject")) if d.get("PinType.PinSubCategoryObject") not in (None, "None") else unq(d.get("PinType.PinSubCategory"))
        self.default = unq(d.get("DefaultValue"))
        self.default_obj = d.get("DefaultObject")
        self.hidden = d.get("bHidden") == "True"
        links = d.get("LinkedTo")
        self.links = []
        if links:
            for l in split_top(links.strip("()")):
                l = l.strip()
                if l:
                    n, pid = l.split()
                    self.links.append((n, pid))

class Node:
    def __init__(self, cls, name):
        self.cls = cls.split(".")[-1]
        self.name = name
        self.props = OrderedDict()
        self.pins = []
    def pin(self, name, out=None):
        for p in self.pins:
            if p.name == name and (out is None or p.out == out):
                return p
    @property
    def exec_outs(self):
        return [p for p in self.pins if p.out and p.cat == "exec" and p.links]
    def label(self):
        P = self.props
        def member(key):
            v = P.get(key)
            if not v: return None
            d = kv(v.strip("()"))
            name = unq(d.get("MemberName"))
            parent = d.get("MemberParent")
            return (short_obj(parent) + "." if parent and "self" not in key.lower() else "") + (name or "?")
        c = self.cls
        if c in ("K2Node_CallFunction", "K2Node_PromotableOperator", "K2Node_CommutativeAssociativeBinaryOperator", "K2Node_CallArrayFunction", "K2Node_Message", "K2Node_CallParentFunction"):
            return member("FunctionReference")
        if c in ("K2Node_VariableGet", "K2Node_VariableSet"):
            d = kv(P.get("VariableReference", "").strip("()"))
            return unq(d.get("MemberName"))
        if c in ("K2Node_Event",):
            return "EVENT " + (member("EventReference") or unq(P.get("CustomFunctionName")) or "?")
        if c == "K2Node_CustomEvent":
            return "CUSTOM EVENT " + str(unq(P.get("CustomFunctionName")))
        if c in ("K2Node_FunctionEntry",):
            return "FUNCTION " + str(member("FunctionReference") or unq(P.get("CustomGeneratedFunctionName")))
        if c in ("K2Node_BreakStruct", "K2Node_MakeStruct", "K2Node_SetFieldsInStruct"):
            return c.replace("K2Node_", "") + "<" + short_obj(P.get("StructType")) + ">"
        if c == "K2Node_DynamicCast":
            return "Cast<" + short_obj(P.get("TargetType")) + ">"
        if c in ("K2Node_EnhancedInputAction", "K2Node_EnhancedInputActionEvent", "K2Node_GetInputActionValue"):
            return c.replace("K2Node_", "") + "(" + short_obj(P.get("InputAction")) + ")"
        if c in ("K2Node_SwitchEnum", "K2Node_EnumEquality", "K2Node_GetEnumeratorNameAsString", "K2Node_CastByteToEnum"):
            return c.replace("K2Node_", "") + "<" + short_obj(P.get("Enum")) + ">"
        if c == "K2Node_EvaluateChooser2":
            return "EvaluateChooser(" + short_obj(P.get("Chooser")) + ")"
        if c == "K2Node_InputKey":
            return "InputKey(" + str(P.get("InputKey")) + ")"
        return c.replace("K2Node_", "").replace("AnimGraphNode_", "Anim:")

def parse(path):
    lines = open(path, encoding="utf-8").read().split("\n")
    graphs = OrderedDict()   # graph path -> OrderedDict(name->Node)
    decl_cls = {}           # export path -> class
    stack = []
    for ln in lines:
        s = ln.strip()
        if s.startswith("Begin Object"):
            m_cls = re.search(r"Class=(\S+)", s)
            m_name = re.search(r'Name="([^"]+)"', s)
            m_exp = re.search(r"ExportPath=\"[^']*'([^']*)'", s)
            exp = m_exp.group(1) if m_exp else None
            cls = m_cls.group(1) if m_cls else decl_cls.get(exp)
            if m_cls and exp:
                decl_cls[exp] = cls
            node = None
            parent = stack[-1] if stack else None
            if isinstance(parent, Node) and not m_cls and m_name and "Binding" in m_name.group(1):
                stack.append(("BINDING", parent)); continue
            if exp and cls and ":" in exp and not m_cls:
                # definition block
                tail = exp.split(":", 1)[1]
                if "." in tail:
                    gpath, nname = tail.rsplit(".", 1)
                    if "EdGraph" not in cls.split(".")[-1]:
                        node = Node(cls, nname)
                        graphs.setdefault(gpath, OrderedDict())[nname] = node
            stack.append(node)
            continue
        if s == "End Object":
            if stack: stack.pop()
            continue
        cur = stack[-1] if stack else None
        if cur is None: continue
        if isinstance(cur, tuple):
            if s.startswith("PropertyBindings="):
                for mb in re.finditer(r'PropertyName="([^"]+)",PathAsText="([^"]*)"', s):
                    cur[1].props.setdefault("__bindings", []).append("%s <- %s" % (mb.group(1), mb.group(2)))
            continue
        m = PIN_RE.match(s)
        if m:
            cur.pins.append(Pin(kv(m.group(1))))
        elif "=" in s and not s.startswith("CustomProperties"):
            k, v = s.split("=", 1)
            cur.props[k] = v
    return graphs

SKIP_PROPS = {"NodePosX", "NodePosY", "NodeGuid", "NodeWidth", "NodeHeight", "bCommentBubbleVisible", "bCommentBubblePinned",
              "ErrorType", "ErrorMsg", "bHasCompilerMessage", "EnabledState", "bIsNodeEnabled", "AdvancedPinDisplay",
              "bDefaultsToPureFunc", "FunctionReference", "VariableReference", "EventReference", "StructType", "TargetType",
              "bOverrideFunction", "bInternalEvent", "bIsPureFunc", "bIsConstFunc", "CustomFunctionName", "ExtraFlags",
              "ShowPinForProperties", "bMadeAfterOverridePinRemoval", "OutputPins", "bUseFlattenedName", "bCanRenameNode"}

def emit(graphs, out):
    W = out.write
    for gpath, nodes in graphs.items():
        pid = {}
        for n in nodes.values():
            for p in n.pins:
                pid[(n.name, p.id)] = (n, p)
        def resolve(pin):
            """Source (node, pin) feeding this input pin, skipping reroute knots."""
            if not pin.links: return None
            nname, p_id = pin.links[0]
            src = pid.get((nname, p_id))
            if not src: return None
            n, p = src
            if n.cls == "K2Node_Knot":
                inp = [q for q in n.pins if not q.out]
                return resolve(inp[0]) if inp else None
            return src
        def lit(p):
            if p.default_obj and p.default_obj != "None": return short_obj(p.default_obj)
            if p.default not in (None, ""): return repr(p.default) if p.cat in ("name", "string", "text") else p.default
            return None
        def expr(pin, depth=0):
            src = resolve(pin)
            if src is None:
                v = lit(pin)
                return v if v is not None else "<" + (pin.name or "?") + ">"
            n, p = src
            impure = any(q.cat == "exec" for q in n.pins)
            if impure or depth > 12:
                if n.cls in ("K2Node_FunctionEntry", "K2Node_Event", "K2Node_CustomEvent", "K2Node_EnhancedInputActionEvent", "K2Node_Tunnel"):
                    return p.name
                return "%s.%s" % (n.name, p.name)
            return call_str(n, depth + 1, outpin=p)
        def args(n, depth):
            a = []
            for q in n.pins:
                if q.out or q.cat == "exec" or q.hidden and not q.links: continue
                if q.name == "self" and not q.links: continue
                a.append("%s=%s" % (q.name, expr(q, depth)))
            return ", ".join(a)
        def call_str(n, depth, outpin=None):
            lab = n.label()
            if n.cls == "K2Node_VariableGet":
                s = lab
                tgt = n.pin("self", out=False)
                if tgt and tgt.links: s = expr(tgt, depth) + "." + lab
                return s
            if n.cls in ("K2Node_BreakStruct",):
                inp = [q for q in n.pins if not q.out]
                return "%s.%s" % (expr(inp[0], depth) if inp else "?", outpin.name if outpin else "?")
            if n.cls == "K2Node_Self": return "self"
            extra = ""
            if n.cls == "K2Node_Select":
                pass
            s = "%s(%s)" % (lab, args(n, depth))
            outs = [q for q in n.pins if q.out and q.cat != "exec"]
            if outpin is not None and len(outs) > 1:
                s += "." + outpin.name
            return s
        visited = set()
        def stmt(n, ind, chain_guard):
            pad = "  " * ind
            if n.name in chain_guard:
                W(pad + "goto %s\n" % n.name); return
            if n.name in visited:
                W(pad + "goto %s  (already emitted)\n" % n.name); return
            visited.add(n.name)
            chain_guard = chain_guard | {n.name}
            c = n.cls
            if c == "K2Node_IfThenElse":
                cond = n.pin("Condition", out=False)
                W(pad + "[%s] if %s:\n" % (n.name, expr(cond)))
                follow(n.pin("then", out=True), ind + 1, chain_guard)
                elsep = n.pin("else", out=True)
                if elsep and elsep.links:
                    W(pad + "else:\n"); follow(elsep, ind + 1, chain_guard)
                return
            if c == "K2Node_ExecutionSequence":
                W(pad + "[%s] sequence:\n" % n.name)
                for p in n.exec_outs:
                    W(pad + "  %s:\n" % p.name); follow(p, ind + 2, chain_guard)
                return
            if c == "K2Node_VariableSet":
                val = [q for q in n.pins if not q.out and q.cat != "exec" and q.name != "self"]
                tgt = n.pin("self", out=False)
                pre = (expr(tgt) + ".") if tgt and tgt.links else ""
                W(pad + "[%s] %s%s = %s\n" % (n.name, pre, n.label(), expr(val[0]) if val else "?"))
            elif c in ("K2Node_AssignmentStatement",):
                W(pad + "[%s] %s = %s\n" % (n.name, expr(n.pin("Variable", out=False)), expr(n.pin("Value", out=False))))
            elif c == "K2Node_FunctionResult":
                W(pad + "[%s] return %s\n" % (n.name, args(n, 0)))
            else:
                outs = [q.name for q in n.pins if q.out and q.cat != "exec" and q.links]
                W(pad + "[%s] %s%s(%s)\n" % (n.name, (", ".join(outs) + " = ") if outs else "", n.label(), args(n, 0)))
                extra = [p for p in n.exec_outs if p.name not in ("then", "execute", "OutputDelegate")]
                main = [p for p in n.exec_outs if p.name in ("then", "execute", "OutputDelegate")]
                if extra:
                    for p in extra:
                        W(pad + "  on %s:\n" % p.name); follow(p, ind + 2, chain_guard)
                    for p in main:
                        W(pad + "  on %s:\n" % p.name); follow(p, ind + 2, chain_guard)
                    return
            for p in n.exec_outs:
                follow(p, ind, chain_guard)
        def follow(p, ind, guard):
            if not p or not p.links: return
            for nname, p_id in p.links:
                src = pid.get((nname, p_id))
                if src:
                    n = src[0]
                    if n.cls == "K2Node_Knot":
                        o = [q for q in n.pins if q.out]
                        follow(o[0] if o else None, ind, guard)
                    else:
                        stmt(n, ind, guard)
        W("\n" + "=" * 100 + "\nGRAPH " + gpath + "\n" + "=" * 100 + "\n")
        entries = [n for n in nodes.values() if n.cls in ("K2Node_FunctionEntry", "K2Node_Event", "K2Node_CustomEvent",
                   "K2Node_EnhancedInputActionEvent", "K2Node_EnhancedInputAction", "K2Node_InputKey", "K2Node_InputKeyEvent",
                   "K2Node_ComponentBoundEvent", "K2Node_Tunnel", "K2Node_InputAction")]
        for e in entries:
            outs = [q.name for q in e.pins if q.out and q.cat != "exec"]
            W("\n--- %s  (%s) params: %s\n" % (e.label(), e.name, ", ".join(outs)))
            for p in e.exec_outs:
                if len(e.exec_outs) > 1: W("  on %s:\n" % p.name); follow(p, 2, frozenset())
                else: follow(p, 1, frozenset())
        # comments
        for n in nodes.values():
            if n.cls == "EdGraphNode_Comment":
                W("  # COMMENT: %s\n" % unq(n.props.get("NodeComment", "")).replace("\\r\\n", " | "))
        # anim graph & other non-K2 nodes: dump props + pin links
        for n in nodes.values():
            if n.cls.startswith("AnimGraphNode") or n.cls.startswith("AnimStateNode") or n.cls.startswith("AnimStateTransition") or n.cls.startswith("AnimStateEntry") or n.cls.startswith("AnimStateConduit"):
                W("\n  NODE %s : %s\n" % (n.name, n.cls))
                for k, v in n.props.items():
                    if k in SKIP_PROPS or k.startswith("ShowPinForProperties") or k == "Binding": continue
                    if k == "__bindings":
                        for b in v: W("     BIND %s\n" % b)
                        continue
                    W("     %s = %s\n" % (k, v[:1500]))
                for q in n.pins:
                    if q.cat == "exec": continue
                    if q.out:
                        if q.links: W("     out %s -> %s\n" % (q.name, ", ".join(l[0] for l in q.links)))
                    else:
                        if q.cat == "struct" and q.sub in ("PoseLink", "ComponentSpacePoseLink"):
                            src = resolve(q)
                            W("     in  %s <= %s\n" % (q.name, src[0].name if src else "-"))
                        else:
                            W("     in  %s = %s\n" % (q.name, expr(q)))
        # unreached impure nodes
        rest = [n for n in nodes.values() if n.name not in visited and any(q.cat == "exec" for q in n.pins) and n not in entries]
        if rest:
            W("\n  -- unreached exec nodes: %s\n" % ", ".join("%s(%s)" % (n.name, n.label()) for n in rest))

if __name__ == "__main__":
    g = parse(sys.argv[1])
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        emit(g, f)
    print(sys.argv[2], len(g), "graphs")
