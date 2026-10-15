"""Compare a UE PIE log (tools/ue/pie_wall_probe.py) with a Godot log (godot/tools/test_wall.gd) of the same PIE_CASES.

  python compare_pie_trav.py ue.txt godot.txt            per case: montage chosen, mean bone distance before / during /
                                                          after the montage (aligned on the jump press), and the
                                                          montage-time aligned pose distance (independent of run-up)
  python compare_pie_trav.py ue.txt godot.txt <case>     plus 0.1 s rows (montage, position, vz, database)

Bones are component space in both logs; Godot's skeleton space mirrors UE in Y (GaspAnimator::apply_to_skeleton).
"""
import re
import sys
import numpy as np

NAMES = ["pelvis", "spine_03", "head", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "foot_l", "foot_r", "calf_l", "calf_r"]


def load(path, mirror_y=False):
    cases, cur = [], None
    for l in open(path, encoding="utf-8", errors="replace"):
        if l.startswith("CASE"):
            cur = {"rows": [], "jump": None}
            cases.append(cur)
        elif l.startswith("JUMP") and cur is not None:
            cur["jump"] = float(re.search(r"t=([\d.]+)", l).group(1))
        elif l.startswith("F ") and cur is not None:
            bones = np.array([[float(v) for v in b.split(",")] for b in re.search(r"bones=(\S+)", l).group(1).split(";")])
            if mirror_y:
                bones[:, 1] *= -1.0
            m = re.search(r"mont=(\S*)", l)
            mont, mpos = "", -1.0
            if m and "@" in m.group(1):
                mont, mpos = m.group(1).split("@")
                mpos = float(mpos)
            cur["rows"].append({
                "t": float(re.search(r" t=([\d.]+)", l).group(1)),
                "loc": np.array([float(v) for v in re.search(r"loc=([-\d.,]+)", l).group(1).split(",")]),
                "vel": np.array([float(v) for v in re.search(r"vel=([-\d.,]+)", l).group(1).split(",")]),
                "bones": bones, "mont": mont, "mpos": mpos, "db": re.search(r"db=(\S*)", l).group(1)})
    return cases


def short(m):
    return m.replace("AM_M_Neutral_Traversal_", "")


def main():
    ue, gd = load(sys.argv[1]), load(sys.argv[2], mirror_y=True)
    only = int(sys.argv[3]) if len(sys.argv) > 3 else None
    for ci, (u, g) in enumerate(zip(ue, gd)):
        if only is not None and ci != only:
            continue
        if u["jump"] is None or g["jump"] is None:
            print("case %d: jump missing ue=%s gd=%s" % (ci, u["jump"], g["jump"]))
            continue
        um = sorted(set(short(r["mont"]) for r in u["rows"] if r["mont"]))
        gm = sorted(set(short(r["mont"]) for r in g["rows"] if r["mont"]))
        print("case %d  montage UE=%s  Godot=%s" % (ci, um, gm))
        ut = np.array([r["t"] - u["jump"] for r in u["rows"]])
        phases = {"before": [], "montage": [], "after": []}
        rows = []
        for r in g["rows"]:
            tg = r["t"] - g["jump"]
            if tg < -0.5:
                continue
            i = int(np.argmin(np.abs(ut - tg)))
            if abs(ut[i] - tg) > 0.02:
                continue
            q = u["rows"][i]
            d = np.linalg.norm(q["bones"] - r["bones"], axis=1)
            phases["montage" if (q["mont"] or r["mont"]) else ("before" if tg < 0 else "after")].append(d)
            rows.append((tg, q, r, d))
        for ph, ds in phases.items():
            if ds:
                a = np.array(ds)
                w = np.argsort(-a.mean(axis=0))[:3]
                print("   %-8s frames %3d  mean bone dist %5.1f cm  p95 %5.1f  worst %s" % (ph, len(a), a.mean(), np.percentile(a.mean(axis=1), 95),
                      ", ".join("%s %.1f" % (NAMES[k], a[:, k].mean()) for k in w)))
        # montage-time aligned (same montage only)
        for mont in sorted(set(r["mont"] for r in g["rows"] if r["mont"])):
            urows = [r for r in u["rows"] if r["mont"] == mont]
            if not urows:
                continue
            up = np.array([r["mpos"] for r in urows])
            ds = []
            for r in g["rows"]:
                if r["mont"] != mont:
                    continue
                i = int(np.argmin(np.abs(up - r["mpos"])))
                if abs(up[i] - r["mpos"]) <= 0.02:
                    ds.append(np.linalg.norm(urows[i]["bones"] - r["bones"], axis=1).mean())
            if ds:
                print("   same montage time (%s): %d frames, mean bone dist %.1f cm, max frame %.1f" % (short(mont), len(ds), np.mean(ds), np.max(ds)))
        if only is None:
            continue
        print("   %6s | %-26s %6s %16s %6s | %-26s %6s %16s %6s | dloc bones" % ("t", "UE mont", "pos", "loc", "vz", "Godot mont", "pos", "loc", "vz"))
        last = -9.0
        for tg, q, r, d in rows:
            if tg - last < 0.1 - 1e-6:
                continue
            last = tg
            print("   %6.2f | %-26s %6.2f %16s %6.0f | %-26s %6.2f %16s %6.0f | %5.1f %5.1f %s %s" % (
                tg, short(q["mont"])[:26], q["mpos"], "%.0f,%.0f,%.0f" % tuple(q["loc"]), q["vel"][2],
                short(r["mont"])[:26], r["mpos"], "%.0f,%.0f,%.0f" % tuple(r["loc"]), r["vel"][2],
                np.linalg.norm(q["loc"] - r["loc"]), d.mean(), q["db"].replace("PSD_Dense_", ""), r["db"].replace("PSD_Dense_", "")))


if __name__ == "__main__":
    main()
