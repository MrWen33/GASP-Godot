"""Compiles a Control Rig blueprint T3D (RigVM model) into a flat op program for GaspPostProcess (PPRigProgram).

All values live in one register file of doubles. Every pin gets a slot range sized by its type (FVector 3, FRotator 3 as
Pitch/Yaw/Roll, FQuat 4 as X/Y/Z/W, FTransform 10 as rotation/translation/scale, element keys 1 = bone index resolved at
setup, FName curve names 1 = curve index, enums 1). The forward-solve event is walked in execution order: collapse /
aggregate nodes and library function references are inlined, array iterators over compile-time arrays are unrolled,
pure (data) nodes are emitted once before their first use inside the current block (as the RigVM compiler does), and
SwitchInt32 becomes conditional jumps. Ops: {"t": unit, "a": [slot offsets of the unit's pins in UNITS order]}.

Usage: rigvm_compile.py <rig.t3d> [--print]"""
import json, re, sys
from collections import defaultdict

# unit -> ([input pins], [output pins]); pins are read in this order by the C++ interpreter
UNITS = {
    "FRigUnit_GetTransform": (["Item", "Space", "bInitial"], ["Transform"]),
    "FRigUnit_SetTransform": (["Item", "Space", "bInitial", "Value", "Weight", "bPropagateToChildren"], []),
    "FRigUnit_SetRotation": (["Item", "Space", "bInitial", "Value", "Weight", "bPropagateToChildren"], []),
    "FRigUnit_SetTranslation": (["Item", "Space", "bInitial", "Value", "Weight", "bPropagateToChildren"], []),
    "FRigUnit_SetRelativeTransformForItem": (["Child", "Parent", "bParentInitial", "Value", "Weight", "bPropagateToChildren"], []),
    "FRigUnit_ProjectTransformToNewParent": (["Child", "bChildInitial", "OldParent", "bOldParentInitial", "NewParent", "bNewParentInitial"], ["Transform"]),
    "FRigUnit_HierarchyGetParent": (["Child", "bDefaultParent"], ["Parent"]),
    "FRigUnit_GetCurveValue": (["Curve"], ["Valid", "Value"]),
    "FRigUnit_SetCurveValue": (["Curve", "Value"], []),
    "FRigVMFunction_MathFloatMul": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathDoubleMul": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathFloatAdd": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathDoubleAdd": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathFloatSub": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathDoubleSub": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathFloatAbs": (["Value"], ["Result"]),
    "FRigVMFunction_MathDoubleAbs": (["Value"], ["Result"]),
    "FRigVMFunction_MathFloatToInt": (["Value"], ["Result"]),
    "FRigVMFunction_MathFloatRemap": (["Value", "SourceMinimum", "SourceMaximum", "TargetMinimum", "TargetMaximum", "bClamp"], ["Result"]),
    "FRigVMFunction_MathDoubleRemap": (["Value", "SourceMinimum", "SourceMaximum", "TargetMinimum", "TargetMaximum", "bClamp"], ["Result"]),
    "FRigVMFunction_MathVectorAdd": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathVectorSub": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathVectorMul": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathQuaternionSwingTwist": (["Input", "TwistAxis"], ["Swing", "Twist"]),
    "FRigVMFunction_MathQuaternionToRotator": (["Value"], ["Result"]),
    "FRigVMFunction_MathQuaternionFromRotator": (["Rotator"], ["Result"]),
    "FRigVMFunction_MathQuaternionFromRotatorV2": (["Value"], ["Result"]),
    "FRigVMFunction_MathQuaternionInverse": (["Value"], ["Result"]),
    "FRigVMFunction_MathQuaternionMul": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathQuaternionSlerp": (["A", "B", "T"], ["Result"]),
    "FRigVMFunction_MathTransformMakeRelative": (["Global", "Parent"], ["Local"]),
    "FRigVMFunction_MathTransformMakeAbsolute": (["Local", "Parent"], ["Global"]),
    "FRigVMFunction_MathTransformMul": (["A", "B"], ["Result"]),
    "FRigVMFunction_MathTransformLerp": (["A", "B", "T"], ["Result"]),
    "FRigVMFunction_AlphaInterp": (["Value", "Scale", "Bias", "bMapRange", "InRange", "OutRange", "bClampResult", "ClampMin",
                                    "ClampMax", "bInterpResult", "InterpSpeedIncreasing", "InterpSpeedDecreasing"], ["Result"]),
    "FRigUnit_SpringInterpVectorV2": (["bUseCurrentInput", "Current", "Target", "Strength", "CriticalDamping", "Force",
                                       "TargetVelocityAmount", "bInitializeFromTarget"], ["Result", "Velocity"]),
}
# pass-through units (output pin aliases the input)
PASSTHROUGH = {"FRigVMFunction_VisualDebugQuatNoSpace": ("Value", "Value"), "FRigVMFunction_VisualDebugQuat": ("Value", "Value"),
               "FRigVMFunction_VisualDebugVectorNoSpace": ("Value", "Value"), "FRigVMFunction_VisualDebugTransformNoSpace": ("Value", "Value")}
# units that only exist for controls / editor features: ignored (warned once if executed)
IGNORED = {"FRigUnit_HierarchyAddControlTransform", "FRigUnit_SetDefaultParent", "FRigUnit_PBIK", "FRigUnit_AimConstraintLocalSpaceOffset"}

LEAF_SIZE = {"FQuat": 4, "FVector": 3, "FRotator": 3, "FTransform": 10, "FVector2D": 2, "FLinearColor": 4, "FRigElementKey": 1}
STRUCT_ORDER = {"FQuat": ["X", "Y", "Z", "W"], "FVector": ["X", "Y", "Z"], "FRotator": ["Pitch", "Yaw", "Roll"],
                "FTransform": ["Rotation", "Translation", "Scale3D"], "FVector2D": ["X", "Y"], "FLinearColor": ["R", "G", "B", "A"],
                "FInputRange": ["Min", "Max"]}
ENUMS = {"LocalSpace": 0, "GlobalSpace": 1, "Bone": 1, "Control": 4, "Null": 2, "Curve": 8, "None": 0}


def parse_t3d(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    cls, props, order = {}, defaultdict(dict), defaultdict(list)
    stack = []
    for ln in txt.splitlines():
        s = ln.strip()
        m = re.match(r'Begin Object (?:Class=(\S+) )?Name="([^"]+)"', s)
        if m:
            stack.append(m.group(2))
            p = ".".join(stack)
            if m.group(1):
                cls[p] = m.group(1).split(".")[-1]
                order[".".join(stack[:-1])].append(p)
            continue
        if s == "End Object":
            stack.pop()
            continue
        if stack and "=" in s:
            k, _, v = s.partition("=")
            props[".".join(stack)].setdefault(k, v)
    return cls, props, order


def unq(v):
    v = v.strip()
    return v[1:-1] if len(v) >= 2 and v[0] == '"' and v[-1] == '"' else v


def parse_struct_text(s):
    """'(X=1.0,Y=2.0)' -> {'X': '1.0', 'Y': '2.0'} (one level, nested parentheses kept as text)"""
    s = s.strip()
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    out, depth, cur = {}, 0, ""
    parts = []
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur); cur = ""
        else:
            cur += ch
    if cur:
        parts.append(cur)
    for p in parts:
        k, _, v = p.partition("=")
        out[k.strip()] = unq(v)
    return out


class Val:
    """a value: slot range (off, size) plus optional compile-time constant (python list of floats / strings) or array"""
    def __init__(self, off=None, size=0, const=None, arr=None, cpp=""):
        self.off, self.size, self.const, self.arr, self.cpp = off, size, const, arr, cpp

    def sub(self, off, size, cpp=""):
        c = self.const[off:off + size] if self.const is not None else None
        return Val(self.off + off, size, c, cpp=cpp)


class Compiler:
    def __init__(self, path):
        self.cls, self.props, self.order = parse_t3d(path)
        self.root = next(p for p, c in self.cls.items() if c == "ControlRigBlueprint")
        self.slots = 0
        self.init = []        # [slot, value]
        self.bone_init = []   # [slot, bone name]
        self.curves = []
        self.ops = []
        self.vars = {}
        self.var_defaults = self._variables()
        self.warn = set()
        self.memo_stack = [{}]
        self.uid = 0

    # ------------------------------------------------------------------ helpers
    def _variables(self):
        out = {}
        for k, v in self.props[self.root].items():
            if k.startswith("NewVariables("):
                d = parse_struct_text(v)
                vt = d.get("VarType", "")
                cpp = "double"
                if "Rotator" in vt:
                    cpp = "FRotator"
                elif "Vector" in vt:
                    cpp = "FVector"
                elif "Transform" in vt:
                    cpp = "FTransform"
                elif "Quat" in vt:
                    cpp = "FQuat"
                elif "bool" in vt:
                    cpp = "bool"
                elif "int" in vt:
                    cpp = "int32"
                out[d.get("VarName")] = (cpp, d.get("DefaultValue", ""))
        return out

    def alloc(self, n):
        o = self.slots
        self.slots += n
        return o

    def curve(self, name):
        if name not in self.curves:
            self.curves.append(name)
        return self.curves.index(name)

    def pins(self, node):
        return [p for p in self.order.get(node, []) if self.cls.get(p) == "RigVMPin"]

    def subpins(self, pin):
        return [p for p in self.order.get(pin, []) if self.cls.get(p) == "RigVMPin"]

    def cpp(self, pin):
        return unq(self.props[pin].get("CPPType", ""))

    def size_of(self, pin):
        c = self.cpp(pin)
        if c.startswith("TArray"):
            return 0
        if c in LEAF_SIZE:
            return LEAF_SIZE[c]
        subs = self.subpins(pin)
        if subs:
            return sum(self.size_of(s) for s in subs)
        return 1

    def sub_offset(self, pin, names):
        """offset/size of a nested sub pin (names list) inside pin"""
        off = 0
        c = self.cpp(pin)
        for i, nm in enumerate(names):
            subs = self.subpins(pin)
            if subs:
                for s in subs:
                    if s.split(".")[-1] == nm:
                        pin = s
                        break
                    off += self.size_of(s)
            else:
                # leaf struct (FQuat / FVector shown unexpanded)
                order = STRUCT_ORDER.get(c, [])
                k = order.index(nm)
                if c == "FTransform":
                    sizes = [4, 3, 3]
                    off += sum(sizes[:k])
                    return off, sizes[k], ["FQuat", "FVector", "FVector"][k]
                return off + k, 1, "double"
            c = self.cpp(pin)
        return off, self.size_of(pin), self.cpp(pin)

    def leaf_values(self, cpp, text):
        """default text of a leaf / unexpanded pin -> list of numbers or [name]"""
        text = unq(text) if text is not None else ""
        if cpp in ("double", "float", "int32", "uint8", "uint16", "int"):
            try:
                return [float(text)] if text else [0.0]
            except ValueError:
                return [0.0]
        if cpp == "bool":
            return [1.0 if text.lower() == "true" else 0.0]
        if cpp == "FName" or cpp == "FString":
            return [text]
        if cpp in STRUCT_ORDER:
            d = parse_struct_text(text) if text.startswith("(") else {}
            if cpp == "FTransform":
                r = parse_struct_text(d.get("Rotation", "")) if d.get("Rotation") else {}
                t = parse_struct_text(d.get("Translation", "")) if d.get("Translation") else {}
                s = parse_struct_text(d.get("Scale3D", "")) if d.get("Scale3D") else {}
                return ([float(r.get(k, 1.0 if k == "W" else 0.0)) for k in "XYZW"] + [float(t.get(k, 0.0)) for k in "XYZ"]
                        + [float(s.get(k, 1.0)) for k in "XYZ"])
            dflt = {"W": 1.0} if cpp == "FQuat" else {}
            return [float(d.get(k, dflt.get(k, 0.0)) or 0.0) for k in STRUCT_ORDER[cpp]]
        if cpp.startswith("E"):   # enum
            return [float(ENUMS.get(text, 0))]
        if cpp == "FRigElementKey":
            d = parse_struct_text(text) if text.startswith("(") else {}
            return [("key", d.get("Type", "None"), d.get("Name", "None"))]
        return [0.0] * LEAF_SIZE.get(cpp, 1)

    def const_of_pin(self, pin):
        """compile-time default of an unlinked pin (recursive over sub pins)"""
        c = self.cpp(pin)
        subs = self.subpins(pin)
        if c == "FRigElementKey":
            if subs:
                t = n = "None"
                for s in subs:
                    nm = s.split(".")[-1]
                    if nm == "Type":
                        t = unq(self.props[s].get("DefaultValue", '"None"'))
                    elif nm == "Name":
                        n = unq(self.props[s].get("DefaultValue", '"None"'))
                return [("key", t, n)]
            return self.leaf_values(c, self.props[pin].get("DefaultValue"))
        if subs and not c.startswith("TArray"):
            out = []
            for s in subs:
                out += self.const_of_pin(s)
            return out
        return self.leaf_values(c, self.props[pin].get("DefaultValue"))

    def materialize(self, const):
        off = self.alloc(len(const))
        for i, v in enumerate(const):
            if isinstance(v, tuple) and v[0] == "key":
                if v[1] == "Bone":
                    self.bone_init.append([off + i, v[2]])
                else:
                    self.init.append([off + i, -2.0])
            elif isinstance(v, str):
                self.init.append([off + i, float(self.curve(v))])
            else:
                if v != 0.0:
                    self.init.append([off + i, v])
        return off

    def const_val(self, const, cpp=""):
        return Val(self.materialize(const), len(const), const, cpp=cpp)

    def emit(self, t, args, **extra):
        op = {"t": t, "a": args}
        op.update(extra)
        self.ops.append(op)
        return op

    def copy(self, dst, src, n):
        if dst != src and n:
            self.emit("copy", [dst, src, n])

    # ------------------------------------------------------------------ graph context
    class Ctx:
        def __init__(self, graph, parent=None, call_node=None, args=None):
            self.graph, self.parent, self.call_node, self.args = graph, parent, call_node, args or {}
            self.links_to, self.links_from = {}, defaultdict(list)
            self.outs = {}  # node -> {pin: Val}
            self.nodes = {}  # short name -> full path (including nodes injected into pins)

    def make_ctx(self, graph, parent=None, call_node=None):
        ctx = Compiler.Ctx(graph, parent, call_node)
        for p in self.order.get(graph, []):
            if self.cls.get(p, "").endswith("Node"):
                ctx.nodes[p.split(".")[-1]] = p
        pre = graph + "."
        for p, c in self.cls.items():
            if c.endswith("Node") and p.startswith(pre) and ".RigVMInjectionInfo_" in p:
                rest = p[len(pre):].split(".")
                if len(rest) == 4 and rest[2].startswith("RigVMInjectionInfo_"):
                    ctx.nodes[rest[3]] = p
        for p in self.order.get(graph, []):
            if self.cls.get(p) == "RigVMLink":
                sp = unq(self.props[p].get("SourcePinPath", ""))
                tp = unq(self.props[p].get("TargetPinPath", ""))
                ctx.links_to[tp] = sp
                ctx.links_from[sp].append(tp)
        return ctx

    def node_path(self, ctx, name):
        return ctx.nodes.get(name, ctx.graph + "." + name)

    def kind(self, node):
        c = self.cls.get(node, "")
        pr = self.props[node]
        fn = unq(pr.get("ResolvedFunctionName", ""))
        return c, fn.split("::")[0] if fn else "", unq(pr.get("TemplateNotation", ""))

    def is_exec_pin(self, pin):
        return self.cpp(pin) in ("FRigVMExecuteContext", "FControlRigExecuteContext", "FRigUnit_SequenceExecution") or \
            "ExecuteContext" in self.cpp(pin)

    # ------------------------------------------------------------------ values
    def pin_value(self, ctx, rel):
        """value of input pin rel ('Node.Pin[.Sub]') in ctx"""
        node_name, _, pin_rel = rel.partition(".")
        node = self.node_path(ctx, node_name)
        pin = node + "." + pin_rel
        if rel in ctx.links_to:
            return self.source_value(ctx, ctx.links_to[rel], pin)
        # linked sub pins?
        subs = self.subpins(pin)
        c = self.cpp(pin)
        if subs and not c.startswith("TArray") and any((rel + "." + s.split(".")[-1]) in ctx.links_to or self._sub_linked(ctx, rel + "." + s.split(".")[-1]) for s in subs):
            if c == "FRigElementKey":
                raise NotImplementedError("partially linked element key " + rel)
            size = self.size_of(pin)
            off = self.alloc(size)
            o = 0
            const_parts = []
            for s in subs:
                v = self.pin_value(ctx, rel + "." + s.split(".")[-1])
                self.copy(off + o, v.off, v.size) if v.const is None else const_parts.append((off + o, v))
                o += v.size
            for dst, v in const_parts:
                self.copy(dst, v.off, v.size)
            return Val(off, size, cpp=c)
        if c.startswith("TArray"):
            return self.array_const(pin)
        return self.const_val(self.const_of_pin(pin), c)

    def _sub_linked(self, ctx, rel):
        if rel in ctx.links_to:
            return True
        return any(k.startswith(rel + ".") for k in ctx.links_to)

    def array_const(self, pin):
        subs = self.subpins(pin)
        if subs:
            return Val(arr=[self.const_val(self.const_of_pin(s), self.cpp(s)) for s in subs])
        dv = unq(self.props[pin].get("DefaultValue", "()"))
        if dv in ("()", ""):
            return Val(arr=[])
        raise NotImplementedError("array default " + dv)

    def source_value(self, ctx, src, target_pin=None):
        """value produced at output pin path src ('Node.Pin[.Sub...]') in ctx"""
        parts = src.split(".")
        node_name, pin_name, subs = parts[0], parts[1], parts[2:]
        node = self.node_path(ctx, node_name)
        whole = self.output_value(ctx, node, pin_name)
        if not subs:
            return whole
        if whole.arr is not None:
            raise NotImplementedError("array sub pin " + src)
        off, size, cpp = self.sub_offset(node + "." + pin_name, subs)
        return whole.sub(off, size, cpp)

    def output_value(self, ctx, node, pin_name):
        c, fn, tmpl = self.kind(node)
        short = node.split(".")[-1]
        outs = ctx.outs.setdefault(node, {})
        memo = self.memo_stack[-1]
        key = (id(ctx), node, pin_name)
        if key in memo:
            return memo[key]
        if c == "RigVMRerouteNode":
            v = self.pin_value(ctx, short + ".Value")
        elif c == "RigVMVariableNode":
            v = self.var_value(ctx, node)
        elif c == "RigVMFunctionEntryNode":
            v = ctx.args[pin_name]
        elif c in ("RigVMCollapseNode", "RigVMAggregateNode", "RigVMFunctionReferenceNode"):
            # pure function call: evaluate inner graph data flow
            inner = self.call_ctx(ctx, node)
            ret = next(p for p in self.order.get(inner.graph, []) if self.cls.get(p) == "RigVMFunctionReturnNode")
            v = self.pin_value(inner, ret.split(".")[-1] + "." + pin_name)
        elif c == "RigVMDispatchNode" or c == "RigVMUnitNode" or c == "RigVMTemplateNode":
            v = self.eval_pure(ctx, node, pin_name)
        else:
            raise NotImplementedError("output of %s %s" % (c, node))
        memo[key] = v
        return v

    def var_value(self, ctx, node):
        name = unq(self.props.get(node + ".Variable", {}).get("DefaultValue", '""'))
        if name in self.vars:
            return self.vars[name]
        # function-local variable / argument (inside library functions VariableNodes read the arguments)
        c = ctx
        while c is not None:
            if name in c.args:
                return c.args[name]
            c = c.parent
        cpp, dflt = self.var_defaults.get(name, (self.cpp(node + ".Value"), ""))
        size = self.size_of(node + ".Value")
        const = self.leaf_values(cpp, dflt) if dflt else [0.0] * size
        if cpp == "FQuat" and not dflt:
            const = [0, 0, 0, 1]
        if cpp == "FTransform" and not dflt:
            const = [0, 0, 0, 1, 0, 0, 0, 1, 1, 1]
        v = Val(self.materialize(const), size, None, cpp=cpp)
        self.vars[name] = v
        return v

    def call_ctx(self, ctx, node):
        c = self.cls[node]
        if c == "RigVMFunctionReferenceNode":
            hdr = self.props[node].get("ReferencedFunctionHeader", "")
            m = re.search(r'LibraryNodePath="[^:]*:(RigVMFunctionLibrary\.[^"]+)"', hdr)
            graph = self.root + "." + m.group(1) + ".ContainedGraph"
        else:
            graph = node + ".ContainedGraph"
        inner = self.make_ctx(graph, ctx, node)
        short = node.split(".")[-1]
        for p in self.pins(node):
            nm = p.split(".")[-1]
            if self.is_exec_pin(p) or self.props[p].get("Direction") == "Output":
                continue
            inner.args[nm] = self.pin_value(ctx, short + "." + nm)
        return inner

    def eval_pure(self, ctx, node, pin_name):
        c, fn, tmpl = self.kind(node)
        short = node.split(".")[-1]
        name = fn or tmpl
        if name.startswith("DISPATCH_RigVMDispatch_ArrayMake"):
            subs = self.subpins(node + ".Values")
            return Val(arr=[self.pin_value(ctx, short + ".Values." + s.split(".")[-1]) for s in subs])
        if name.startswith("DISPATCH_RigVMDispatch_ArrayGetAtIndex"):
            arr = self.pin_value(ctx, short + ".Array")
            idx = self.pin_value(ctx, short + ".Index")
            if idx.const is None:
                raise NotImplementedError("dynamic array index")
            return arr.arr[int(idx.const[0])]
        if name.startswith("DISPATCH_RigVMDispatch_If"):
            cond = self.pin_value(ctx, short + ".Condition")
            a = self.pin_value(ctx, short + ".True")
            b = self.pin_value(ctx, short + ".False")
            if cond.const is not None:
                return a if cond.const[0] else b
            out = self.alloc(a.size)
            self.emit("select", [cond.off, a.off, b.off, out, a.size])
            return Val(out, a.size)
        if name in PASSTHROUGH:
            return self.pin_value(ctx, short + "." + PASSTHROUGH[name][0])
        if name in UNITS:
            self.emit_unit(ctx, node, name)
            return ctx.outs[node][pin_name]
        raise NotImplementedError("pure unit " + name)

    def emit_unit(self, ctx, node, name):
        short = node.split(".")[-1]
        ins, outs = UNITS[name]
        args = []
        for p in ins:
            v = self.pin_value(ctx, short + "." + p)
            args.append(v.off)
        res = {}
        for p in outs:
            size = self.size_of(node + "." + p)
            off = self.alloc(size)
            res[p] = Val(off, size, cpp=self.cpp(node + "." + p))
            args.append(off)
        ctx.outs.setdefault(node, {}).update(res)
        if name == "FRigUnit_SetRotation" or name == "FRigUnit_SetTranslation" or name == "FRigUnit_SetTransform":
            pass
        self.emit(name.replace("FRigUnit_", "").replace("FRigVMFunction_", ""), args, sz=[self.size_of(node + "." + p) for p in ins + outs])

    # ------------------------------------------------------------------ execution
    def exec_next(self, ctx, node, pin="ExecuteContext"):
        short = node.split(".")[-1]
        return [self.node_path(ctx, t.split(".")[0]) for t in ctx.links_from.get(short + "." + pin, [])]

    def run_chain(self, ctx, start_nodes):
        for n in start_nodes:
            self.run_node(ctx, n)

    def run_node(self, ctx, node):
        c, fn, tmpl = self.kind(node)
        short = node.split(".")[-1]
        name = fn or tmpl
        if c == "RigVMFunctionReturnNode":
            return
        if c == "RigVMRerouteNode":
            self.run_chain(ctx, self.exec_next(ctx, node, "Value"))
            return
        if c in ("RigVMCollapseNode", "RigVMAggregateNode", "RigVMFunctionReferenceNode"):
            inner = self.call_ctx(ctx, node)
            entry = next(p for p in self.order.get(inner.graph, []) if self.cls.get(p) == "RigVMFunctionEntryNode")
            eshort = entry.split(".")[-1]
            # entry exec pin: ExecuteContext or Execute
            for p in self.pins(entry):
                if self.is_exec_pin(p):
                    self.run_chain(inner, [self.node_path(inner, t.split(".")[0]) for t in inner.links_from.get(eshort + "." + p.split(".")[-1], [])])
            # outputs of a mutable collapse node: return values (pure part) are resolved lazily through output_value
            ctx.outs.setdefault(node, {})
            ret = next(p for p in self.order.get(inner.graph, []) if self.cls.get(p) == "RigVMFunctionReturnNode")
            # sequence-like collapse nodes expose several exec outputs (A, B, C, D) wired to inner exec chains: handled by
            # following the return node's exec input links back to inner chains, which run_chain already executed in order.
            self._after_collapse(ctx, node, inner, ret)
            return
        if c == "RigVMVariableNode":
            name_v = unq(self.props.get(node + ".Variable", {}).get("DefaultValue", '""'))
            var = self.var_value(ctx, node)
            v = self.pin_value(ctx, short + ".Value")
            self.copy(var.off, v.off, v.size)
            self.run_chain(ctx, self.exec_next(ctx, node))
            return
        if name.startswith("FRigVMFunction_Sequence") or name.startswith("FRigUnit_SequenceExecution"):
            for p in self.pins(node):
                pn = p.split(".")[-1]
                if self.props[p].get("Direction") == "Output":
                    self.run_chain(ctx, self.exec_next(ctx, node, pn))
            return
        if name.startswith("DISPATCH_RigVMDispatch_ArrayIterator"):
            arr = self.pin_value(ctx, short + ".Array")
            n = len(arr.arr)
            for i, el in enumerate(arr.arr):
                self.memo_stack.append(dict(self.memo_stack[-1]))
                ctx.outs[node] = {"Element": el, "Index": self.const_val([float(i)]), "Count": self.const_val([float(n)]),
                                  "Ratio": self.const_val([float(i) / max(1, n - 1)])}
                memo = self.memo_stack[-1]
                for k, v in ctx.outs[node].items():
                    memo[(id(ctx), node, k)] = v
                self.run_chain(ctx, self.exec_next(ctx, node, "ExecuteContext"))
                self.memo_stack.pop()
            memo = self.memo_stack[-1]
            ctx.outs[node] = {"Count": self.const_val([float(n)])}
            memo[(id(ctx), node, "Count")] = ctx.outs[node]["Count"]
            self.run_chain(ctx, self.exec_next(ctx, node, "Completed"))
            return
        if name.startswith("DISPATCH_RigVMDispatch_SwitchInt32"):
            idx = self.pin_value(ctx, short + ".Index")
            cases = self.subpins(node + ".Cases")
            jumps_end = []
            for k, cp in enumerate(cases):
                cn = cp.split(".")[-1]
                j = self.emit("jne", [idx.off], v=float(cn), to=-1)
                self.memo_stack.append(dict(self.memo_stack[-1]))
                self.run_chain(ctx, self.exec_next(ctx, node, "Cases." + cn))
                self.memo_stack.pop()
                je = self.emit("jmp", [], to=-1)
                jumps_end.append(je)
                j["to"] = len(self.ops)
            for je in jumps_end:
                je["to"] = len(self.ops)
            self.run_chain(ctx, self.exec_next(ctx, node, "Completed"))
            return
        if name in ("FRigUnit_BeginExecution", "FRigUnit_PrepareForExecution"):
            self.run_chain(ctx, self.exec_next(ctx, node))
            return
        if name in IGNORED:
            self.emit("ignored", [], unit=name)
            self.run_chain(ctx, self.exec_next(ctx, node))
            return
        if name in UNITS:
            self.emit_unit(ctx, node, name)
            self.run_chain(ctx, self.exec_next(ctx, node))
            return
        raise NotImplementedError("exec unit %s (%s)" % (name, node))

    def _after_collapse(self, ctx, node, inner, ret):
        # continue with the collapse node's own exec outputs, in pin order (single "ExecuteContext"/"Execute" or A/B/C/D)
        rshort = ret.split(".")[-1]
        for p in self.pins(node):
            pn = p.split(".")[-1]
            if not self.is_exec_pin(p):
                continue
            d = self.props[p].get("Direction")
            if d not in ("Output", "IO"):
                continue
            self.run_chain(ctx, self.exec_next(ctx, node, pn))

    def compile(self):
        model = self.root + ".RigVMModel"
        ctx = self.make_ctx(model)
        begin = [p for p in self.order.get(model, []) if "FRigUnit_BeginExecution" in self.props[p].get("ResolvedFunctionName", "")]
        for b in begin:
            self.run_node(ctx, b)
        return {"slots": self.slots, "init": self.init, "bones": self.bone_init, "curves": self.curves, "ops": self.ops,
                "vars": {k: [v.off, v.size] for k, v in self.vars.items()}}


def compile_rig(path):
    return Compiler(path).compile()


if __name__ == "__main__":
    prog = compile_rig(sys.argv[1])
    if "--print" in sys.argv:
        for i, o in enumerate(prog["ops"]):
            print(i, o)
        print("slots", prog["slots"], "curves", prog["curves"], "bones", prog["bones"], "vars", prog["vars"])
    else:
        print(json.dumps(prog)[:2000])
