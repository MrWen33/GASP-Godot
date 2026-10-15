# Converts a Chooser table T3D (UTF-16) to JSON for the GDScript chooser evaluator.
import re, json, sys
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
t = d.decode('utf-16') if d[:2] in (b'\xff\xfe', b'\xfe\xff') else d.decode('utf-8')
lines = t.splitlines()

def parse_rows(s):
    # split top-level parenthesised groups "(a),(b)"
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
            if depth == 1: cur = ''; continue
        if ch == ')':
            depth -= 1
            if depth == 0: out.append(cur); continue
        if depth >= 1: cur += ch
    return out

def kv(s):
    r = {}
    for m in re.finditer(r'(\w+)=("[^"]*"|[^,]*)', s):
        r[m.group(1)] = m.group(2).strip('"')
    return r

tables, cur, root = {}, None, None
for ln in lines:
    s = ln.strip()
    m = re.match(r'Begin Object (?:Class=\S+ )?Name="([^"]+)"', s)
    if m:
        name = m.group(1)
        if root is None: root = name
        cur = tables.setdefault(name, {'results': [], 'columns': [], 'disabled': []})
        continue
    if s == 'End Object':
        cur = tables[root]; continue
    if cur is None: continue
    m = re.match(r'ResultsStructs\(\d+\)=/Script/Chooser\.(\w+)\((.*)\)$', s)
    if m:
        kind, body = m.groups()
        if kind == 'AssetChooser':
            a = re.search(r"\.(\w+)'\"?\)?$", body) or re.search(r"\.([\w]+)'", body)
            cur['results'].append({'asset': a.group(1)})
        elif kind == 'NestedChooser':
            a = re.search(r"ChooserTable'([^']+)'", body)
            cur['results'].append({'nested': a.group(1)})
        else:
            cur['results'].append({'unknown': kind})
        continue
    m = re.match(r'DisabledRows\(\d+\)=(\w+)', s)
    if m: cur['disabled'].append(m.group(1) == 'True'); continue
    m = re.match(r'ColumnsStructs\(\d+\)=/Script/Chooser\.(\w+)\((.*)\)$', s)
    if m:
        kind, body = m.groups()
        chain = re.search(r'PropertyBindingChain=\(([^)]*)\)', body).group(1).replace('"', '').split(',')
        col = {'type': kind, 'binding': chain}
        if kind == 'BoolColumn':
            rv = re.search(r'RowValuesWithAny=\(([^)]*)\)', body).group(1).split(',')
            col['rows'] = rv
        elif kind == 'FloatRangeColumn':
            rv = re.search(r'RowValues=\((.*)\)$', body).group(1)
            rows = []
            for g in parse_rows(rv):
                k = kv(g)
                rows.append({'min': float(k.get('Min', 0)), 'max': float(k.get('Max', 0)),
                             'no_min': k.get('bNoMin') == 'True', 'no_max': k.get('bNoMax') == 'True'})
            col['rows'] = rows
        elif kind == 'EnumColumn':
            rv = re.search(r'RowValues=\((.*)\)\)?$', body).group(1)
            rows = []
            for g in parse_rows(rv):
                k = kv(g)
                rows.append({'value': int(k.get('Value', 0)), 'comparison': k.get('Comparison', 'MatchEqual')})
            col['rows'] = rows
        elif kind == 'MultiEnumColumn':
            rv = re.search(r'RowValues=\((.*)\)$', body).group(1)
            col['rows'] = [int(kv(g).get('Value', 0)) for g in parse_rows(rv)]
        elif kind == 'OutputEnumColumn':
            rv = re.search(r'RowValues=\((.*)\)$', body).group(1)
            col['rows'] = [int(kv(g).get('Value', 0)) for g in parse_rows(rv)]
            fb = re.search(r'FallbackValue=\(([^)]*)\)', body)
            col['fallback'] = int(kv(fb.group(1)).get('Value', 0)) if fb else 0
        col['raw'] = body
        cur['columns'].append(col)
json.dump({'root': root, 'tables': tables}, open(dst, 'w'), indent=1)
print(root, {k: (len(v['results']), len(v['columns'])) for k, v in tables.items()})
