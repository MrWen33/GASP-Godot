# Extracts the GASP blend spaces (samples, triangulation/segments, smoothing) to godot/data/blendspaces.json
import re, json, sys, os
root = sys.argv[1]; dst = sys.argv[2]
files = {
 'BS_Neutral_AO_Stand': 'Characters/UEFN_Mannequin/Animations/AimOffset/BS_Neutral_AO_Stand.t3d',
 'BS1D_Additive_Lean_Run': 'Characters/UEFN_Mannequin/Animations/Run/BS1D_Additive_Lean_Run.t3d',
}
out = {}
for name, rel in files.items():
    d = open(os.path.join(root, rel), 'rb').read()
    t = d.decode('utf-16') if d[:2] in (b'\xff\xfe', b'\xfe\xff') else d.decode('utf-8')
    bs = {'samples': [], 'params': [], 'speed': 0.0, 'per_bone': [], 'triangles': [], 'segments': []}
    for l in t.splitlines():
        l = l.strip()
        m = re.match(r'SampleData\((\d+)\)=\(Animation="[^\']*\'[^.]*\.([^\']+)\'"(?:,SampleValue=\(X=([-\d.]+),Y=([-\d.]+),Z=([-\d.]+)\))?', l)
        if m:
            bs['samples'].append({'anim': m.group(2), 'value': [float(m.group(3) or 0), float(m.group(4) or 0)]})
        m = re.match(r'BlendParameters\((\d+)\)=\(DisplayName="([^"]*)",Min=([-\d.]+),Max=([-\d.]+)', l)
        if m:
            bs['params'].append({'name': m.group(2), 'min': float(m.group(3)), 'max': float(m.group(4))})
        m = re.match(r'TargetWeightInterpolationSpeedPerSec=([-\d.]+)', l)
        if m: bs['speed'] = float(m.group(1))
        m = re.match(r'ManualPerBoneOverrides\(\d+\)=\(BoneReference=\(BoneName="([^"]+)"\),InterpolationSpeedPerSec=([-\d.]+)\)', l)
        if m: bs['per_bone'].append({'bone': m.group(1), 'speed': float(m.group(2))})
        if l.startswith('BlendSpaceData='):
            for tri in re.finditer(r'\(SampleIndices\[0\]=(\d+),SampleIndices\[1\]=(\d+)(?:,SampleIndices\[2\]=(\d+))?', l):
                if tri.group(3) is not None:
                    bs['triangles'].append([int(tri.group(1)), int(tri.group(2)), int(tri.group(3))])
                else:
                    bs['segments'].append([int(tri.group(1)), int(tri.group(2))])
            # SampleIndices[0] defaults to 0 and is omitted by T3D
            for tri in re.finditer(r'\((?!SampleIndices\[0\])SampleIndices\[1\]=(\d+)(?:,SampleIndices\[2\]=(\d+))?', l):
                if tri.group(2) is not None:
                    bs['triangles'].append([0, int(tri.group(1)), int(tri.group(2))])
                else:
                    bs['segments'].append([0, int(tri.group(1))])
    out[name] = bs
    print(name, len(bs['samples']), 'tris', len(bs['triangles']), 'segs', len(bs['segments']), bs['speed'], bs['per_bone'])
# AO blend curve
d = open(os.path.join(root, 'Characters/UEFN_Mannequin/Animations/AimOffset/AO_Blend_Curve.t3d'), 'rb').read()
t = d.decode('utf-16') if d[:2] in (b'\xff\xfe', b'\xfe\xff') else d.decode('utf-8')
m = re.search(r'FloatCurve=\(Keys=\((.*?)\)\)', t)
print('AO curve', m.group(1)[:400] if m else None)
out['_raw_ao_curve'] = m.group(1) if m else ''
json.dump(out, open(dst, 'w'), indent=1)
