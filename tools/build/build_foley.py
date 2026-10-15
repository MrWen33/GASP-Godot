"""Build godot/data/foley.json from the exported Foley MetaSound presets (T3D) and DefaultFoleyEventAudioBank.

Each MSS_FoleySound_* preset of MSS_FoleySound: random wave (Array Random Get, no repeats) with
gain * dB->linear(random(Volume.Min, Volume.Max)) and pitch shift = semitones(random(Pitch.Min, Pitch.Max)).
"""
import re, json, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
T3D = os.path.join(ROOT, "tools", "ue", "out", "t3d_audio", "Foley")
OUT = os.path.join(ROOT, "godot", "data", "foley.json")

bank = open(os.path.join(T3D, "DefaultFoleyEventAudioBank.t3d"), encoding="utf-8", errors="ignore").read()
events = re.findall(r'\(\(TagName="([^"]+)"\), "[^\']*\'(/Game/Audio/Foley/MetaSounds/[^.]+)\.', bank)
res = {}
for tag, pkg in events:
    name = pkg.rsplit("/", 1)[1]
    s = open(os.path.join(T3D, "MetaSounds", name + ".t3d"), encoding="utf-8", errors="ignore").read()
    waves = []
    for w in re.findall(r"SoundWave'/Game/Audio/(Foley/SoundWaves/[^.']+)\.", s):
        p = "res://assets/Audio/" + w + ".wav"
        if p not in waves:
            waves.append(p)
    # top-level member defaults (Gain, Volume.Min, Volume.Max, Pitch.Min, Pitch.Max in input sort order)
    floats = {}
    for idx, val in re.findall(r'\n   Begin Object Name="MetasoundEditorGraphMemberDefaultFloat_(\d)"[^\n]*\n\s*Default=([-0-9.]+)', s):
        floats[int(idx)] = float(val)
    keys = ["gain", "volume_min", "volume_max", "pitch_min", "pitch_max"]
    defaults = [1.0, -1.0, 1.0, 0.95, 1.05]
    d = {k: floats.get(i, defaults[i]) for i, k in enumerate(keys)}
    d["waves"] = waves
    d["metasound"] = name
    res[tag.split(".")[-1]] = d
json.dump(res, open(OUT, "w"), indent=1)
for k, v in res.items():
    print(k, len(v["waves"]), v["gain"], v["volume_min"], v["volume_max"], v["pitch_min"], v["pitch_max"])
