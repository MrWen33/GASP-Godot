import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Packs the UE-space animation bake for the native runtime (the runtime works in UE space and converts at output).

Output: godot/data/anims/anims.json + anims.bin
  per anim: float32 [nkeys][nbones][7] (UE local: pos cm xyz, quat xyzw) then float32 [nkeys][ncurves]
Godot skeleton local = (x, -y, z) * 0.01 and quat (-x, y, -z, w) -- applied by the runtime.
Also copies notifies into godot/data/notifies.json (cleaned).
"""
import json, os, re
import numpy as np

SRC = REPO + "/tools/ue/out"
DST = REPO + "/godot/data"


def clean_notifies():
    raw = json.load(open(SRC + "/notifies.json"))
    out = {}
    for path, lst in raw.items():
        rows = []
        for e in lst:
            cls = (e.get("notify_state_class") or e.get("notify") or {})
            row = {"name": e["name"], "time": e["time"], "duration": e["duration"], "class": cls.get("class") or e["name"],
                   "state": "notify_state_class" in e}
            if "cost_addend" in cls:
                row["cost_addend"] = cls["cost_addend"]
            m = cls.get("modifier")
            if m:
                mod = {}
                for k, v in m.items():
                    if isinstance(v, str):
                        mm = re.match(r"<\w+\.(\w+): (-?\d+)>", v)
                        v = mm.group(1) if mm else v
                    mod[k] = v
                row["modifier"] = mod
            if "side" in cls:
                mm = re.match(r"<\w+\.(\w+): (-?\d+)>", cls["side"])
                row["side"] = mm.group(1) if mm else cls["side"]
            rows.append(row)
        out[path] = rows
    json.dump(out, open(DST + "/notifies.json", "w"))
    print("notifies", len(out))


def main():
    os.makedirs(DST + "/anims", exist_ok=True)
    idx = json.load(open(SRC + "/anims/anims.json"))
    gsk = json.load(open(DST + "/godot_skeleton.json"))
    raw = open(SRC + "/anims/anims.bin", "rb").read()
    nb = len(idx["bones"])
    out = {"bones": idx["bones"], "parents": [b["parent"] for b in gsk["bones"]], "anims": []}
    rl = np.array(idx["ref_local"], dtype=np.float64)
    out["ref_pos"] = rl[:, 0:3].tolist()
    out["ref_rot"] = rl[:, 3:7].tolist()
    off = 0
    max_scale_err = 0.0
    with open(DST + "/anims/anims.bin", "wb") as f:
        for e in idx["anims"]:
            n = e["num_keys"]
            k = np.frombuffer(raw, dtype=np.float32, count=n * nb * 10, offset=e["offset"]).reshape(n, nb, 10)
            max_scale_err = max(max_scale_err, float(np.abs(k[:, :, 7:10] - 1.0).max()))
            q = k[:, :, 3:7].astype(np.float64)
            q = q / np.linalg.norm(q, axis=2, keepdims=True)
            blob = np.concatenate([k[:, :, 0:3], q], axis=2).astype(np.float32)
            nc = len(e["curves"])
            curves = np.frombuffer(raw, dtype=np.float32, count=n * nc, offset=e["curve_offset"]).reshape(n, nc) if nc else np.zeros((n, 0), np.float32)
            f.write(blob.tobytes())
            f.write(curves.astype(np.float32).tobytes())
            out["anims"].append({"name": e["name"], "path": e["path"], "length": e["length"], "fps": e["fps"], "num_keys": n,
                                 "loop": bool(e["loop"]), "root_motion": bool(e["root_motion"]),
                                 "lock_root": bool(e["root_motion"] or e["force_root_lock"]),
                                 "additive": e.get("additive", "AAT_NONE"),
                                 "curves": e["curves"], "offset": off, "curve_offset": off + blob.nbytes})
            off += blob.nbytes + curves.nbytes
    json.dump(out, open(DST + "/anims/anims.json", "w"))
    print("anims", len(out["anims"]), "bytes", off, "max |scale-1|", max_scale_err)
    clean_notifies()


if __name__ == "__main__":
    main()
