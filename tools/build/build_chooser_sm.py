# Converts a Chooser table T3D with nested tables, OutputStructColumn and RandomizeColumn (CHT_AnimationsForStateMachine)
# to JSON for scripts/chooser_multi.gd. Tables are keyed by object path; S_ChooserOutputs fields are written without GUID suffixes.
import re, json, sys

src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
t = d.decode('utf-16') if d[:2] in (b'\xff\xfe', b'\xfe\xff') else d.decode('utf-8')

OUTPUT_DEFAULTS = {"StartTime": 0.0, "UseMM": False, "MMCostLimit": 0.0, "BlendTime": 0.3, "BlendProfile": "", "Tags": []}


def groups(s):
    """top level '(...)' groups of s (s starts inside the outer parentheses)"""
    out, depth, cur, q = [], 0, '', False
    for ch in s:
        if ch == '"':
            q = not q
        if not q and ch == '(':
            depth += 1
            if depth == 1:
                cur = ''
                continue
        if not q and ch == ')':
            depth -= 1
            if depth == 0:
                out.append(cur)
                continue
        if depth >= 1:
            cur += ch
    return out


def split_top(s):
    """split on top level commas"""
    out, depth, cur, q = [], 0, '', False
    for ch in s:
        if ch == '"':
            q = not q
        if not q and ch == '(':
            depth += 1
        elif not q and ch == ')':
            depth -= 1
        if ch == ',' and depth == 0 and not q:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    if cur:
        out.append(cur)
    return out


def kv(s):
    r = {}
    for part in split_top(s):
        k, _, v = part.partition('=')
        r[k.strip()] = v.strip()
    return r


def after(body, key):
    """value text of 'key=(...)' (balanced) in body"""
    i = body.find(key + '=(')
    if i < 0:
        return None
    i += len(key) + 2
    depth, j = 1, i
    q = False
    while j < len(body) and depth:
        ch = body[j]
        if ch == '"':
            q = not q
        elif not q and ch == '(':
            depth += 1
        elif not q and ch == ')':
            depth -= 1
        j += 1
    return body[i:j - 1]


def output_struct(text):
    """'/Game/.../S_ChooserOutputs.S_ChooserOutputs(Field_N_GUID=v,...)' -> dict"""
    o = dict(OUTPUT_DEFAULTS)
    o["Tags"] = []
    i = text.find('(')
    if i < 0:
        return o
    for k, v in kv(text[i + 1:text.rfind(')')]).items():
        name = re.sub(r'_\d+_[0-9A-F]{32}$', '', k)
        if name in ("StartTime", "MMCostLimit", "BlendTime"):
            o[name] = float(v)
        elif name == "UseMM":
            o[name] = v == "True"
        elif name == "BlendProfile":
            o[name] = v.strip('"') if v.strip('"') != "None" else ""
        elif name == "Tags":
            o[name] = [x.strip().strip('"') for x in v.strip('()').split(',') if x.strip()]
    return o


tables, stack, root = {}, [], None
for ln in t.splitlines():
    s = ln.strip()
    m = re.match(r'Begin Object (?:Class=\S+ )?Name="([^"]+)"(?: ExportPath="([^"]+)")?', s)
    if m:
        path = re.sub(r"^[^']*'(.*)'$", r"\1", m.group(2)) if m.group(2) else m.group(1)
        if root is None:
            root = path
        new = path not in tables
        tbl = tables.setdefault(path, {'name': m.group(1), 'results': [], 'columns': [], 'disabled': []})
        # a nested object appears twice (declaration + definition); only one block carries the properties
        stack.append(tbl)
        continue
    if s == 'End Object':
        stack.pop()
        continue
    if not stack:
        continue
    cur = stack[-1]
    m = re.match(r'ResultsStructs\((\d+)\)=/Script/Chooser\.(\w+)\((.*)\)$', s)
    if m:
        idx, kind, body = int(m.group(1)), m.group(2), m.group(3)
        if idx == 0:
            cur['results'] = []
        # elements equal to an empty instanced struct are not exported: keep row indices aligned
        while len(cur['results']) < idx:
            cur['results'].append({'asset': ''})
        if kind == 'AssetChooser':
            a = re.search(r"'([^']+)'", body)
            cur['results'].append({'asset': a.group(1).split('.')[-1] if a else ''})  # empty AssetChooser yields nothing
        elif kind == 'NestedChooser':
            a = re.search(r"ChooserTable'([^']+)'", body)
            cur['results'].append({'nested': a.group(1)})
        else:
            cur['results'].append({'unknown': kind})
        continue
    m = re.match(r'DisabledRows\((\d+)\)=(\w+)', s)
    if m:
        if int(m.group(1)) == 0:
            cur['disabled'] = []
        while len(cur['disabled']) < int(m.group(1)):
            cur['disabled'].append(False)
        cur['disabled'].append(m.group(2) == 'True')
        continue
    m = re.match(r'ColumnsStructs\((\d+)\)=/Script/Chooser\.(\w+)\((.*)\)$', s)
    if m:
        idx, kind, body = int(m.group(1)), m.group(2), m.group(3)
        if idx == 0:
            cur['columns'] = []
        chain = re.search(r'PropertyBindingChain=\(([^)]*)\)', body)
        col = {'type': kind, 'binding': chain.group(1).replace('"', '').split(',') if chain else []}
        rv = after(body, 'RowValuesWithAny') if kind == 'BoolColumn' else after(body, 'RowValues')
        if kind == 'BoolColumn':
            col['rows'] = rv.split(',') if rv else []
        elif kind == 'FloatRangeColumn':
            col['rows'] = []
            for g in groups(rv or ''):
                k = kv(g)
                col['rows'].append({'min': float(k.get('Min', 0)), 'max': float(k.get('Max', 0)),
                                    'no_min': k.get('bNoMin') == 'True', 'no_max': k.get('bNoMax') == 'True'})
        elif kind == 'EnumColumn':
            col['rows'] = [{'value': int(kv(g).get('Value', 0)), 'comparison': kv(g).get('Comparison', 'MatchEqual')} for g in groups(rv or '')]
        elif kind == 'MultiEnumColumn':
            col['rows'] = [int(kv(g).get('Value', 0)) for g in groups(rv or '')]
        elif kind == 'OutputStructColumn':
            col['rows'] = [output_struct(x) for x in split_top(rv or '')]
            fb = re.search(r'FallbackValue=(/Game/[^,]*?\((?:[^()]|\([^()]*\))*\))', body)
            col['fallback'] = output_struct(fb.group(1)) if fb else dict(OUTPUT_DEFAULTS)
        elif kind == 'RandomizeColumn':
            col['rows'] = [float(x) for x in (rv or '').split(',') if x]
            col['repeat_probability_multiplier'] = float(re.search(r'RepeatProbabilityMultiplier=([-0-9.]+)', body).group(1))
            col['equal_cost_threshold'] = float(re.search(r'EqualCostThreshold=([-0-9.]+)', body).group(1))
        else:
            raise ValueError(kind)
        cur['columns'].append(col)

# nested choosers reference sub-objects by name (unique within this asset)
by_name = {tb['name']: p for p, tb in tables.items()}
assert len(by_name) == len(tables)
for tb in tables.values():
    for r in tb['results']:
        if 'nested' in r and r['nested'] not in tables:
            r['nested'] = by_name[r['nested'].split(':')[-1].split('.')[-1]]
for p, tb in tables.items():
    for c in tb['columns']:
        assert len(c['rows']) <= len(tb['results']) or not tb['results'], (p, c['type'], len(c['rows']), len(tb['results']))
    for r in tb['results']:
        if 'nested' in r:
            assert r['nested'] in tables, r['nested']
        assert 'unknown' not in r, r
json.dump({'root': root, 'tables': tables}, open(dst, 'w'), indent=1)
print(root, len(tables), 'tables', sum(len(v['results']) for v in tables.values()), 'results')
