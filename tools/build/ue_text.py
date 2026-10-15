"""Parser for UE exported property text: (Key=Value,Key2=(Nested=1),Arr=((A=1),(A=2)),Str="x",Obj=/Script/X.Y'/Game/..')"""


def parse_value(s, i=0):
    """Returns (value, next_index). Structs -> dict, arrays (tuples without keys) -> list, scalars -> str."""
    n = len(s)
    if i < n and s[i] == "(":
        i += 1
        items = []
        is_dict = None
        d = {}
        while i < n and s[i] != ")":
            # key?
            j = i
            depth = 0
            key = None
            # detect "Key=" (identifier possibly with (idx)) before a comma/paren at depth 0
            k = i
            while k < n and (s[k].isalnum() or s[k] in "_"):
                k += 1
            if k < n and s[k] == "(" and k > i:
                # Key(0)= array element syntax
                e = s.find(")", k)
                if e + 1 < n and s[e + 1] == "=":
                    key = s[i:e + 1]
                    i = e + 2
            elif k < n and s[k] == "=" and k > i:
                key = s[i:k]
                i = k + 1
            v, i = parse_value(s, i)
            if key is not None:
                is_dict = True if is_dict is None else is_dict
                d[key] = v
            else:
                is_dict = False if is_dict is None else is_dict
                items.append(v)
            if i < n and s[i] == ",":
                i += 1
        i += 1  # ')'
        if is_dict:
            return d, i
        return items, i
    if i < n and s[i] == '"':
        j = i + 1
        out = []
        while j < n and s[j] != '"':
            if s[j] == "\\" and j + 1 < n:
                out.append(s[j + 1]); j += 2; continue
            out.append(s[j]); j += 1
        return "".join(out), j + 1
    # bare token until , or ) at depth 0 (object paths may contain quotes ')
    j = i
    quote = False
    while j < n:
        c = s[j]
        if c == "'":
            quote = not quote
        elif not quote and c in ",)":
            break
        j += 1
    return s[i:j], j


def parse(s):
    s = s.strip()
    v, _ = parse_value(s, 0)
    return v


def f(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def vec(d, default=(0.0, 0.0, 0.0)):
    if not isinstance(d, dict):
        return list(default)
    return [f(d.get("X"), default[0]), f(d.get("Y"), default[1]), f(d.get("Z"), default[2])]


def rot(d):
    """FRotator dict -> [pitch, yaw, roll] degrees"""
    if not isinstance(d, dict):
        return [0.0, 0.0, 0.0]
    return [f(d.get("Pitch")), f(d.get("Yaw")), f(d.get("Roll"))]


def quat(d):
    if not isinstance(d, dict):
        return [0.0, 0.0, 0.0, 1.0]
    return [f(d.get("X")), f(d.get("Y")), f(d.get("Z")), f(d.get("W"), 1.0)]


def asset_name(p):
    """/Script/Engine.PhysicsAsset'/Game/a/b/PA_X.PA_X' -> /Game/a/b/PA_X"""
    if not p:
        return None
    p = str(p)
    if "'" in p:
        p = p.split("'")[1]
    return p.split(".")[0]
