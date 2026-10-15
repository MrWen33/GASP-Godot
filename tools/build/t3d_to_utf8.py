"""Re-encodes the UE T3D exports (UTF-16 or ASCII) as UTF-8 copies that the build scripts read:
  tools/ue/out/t3d             -> t3d_utf8
  tools/ue/out/t3d_widgets     -> t3d_widgets/u
  tools/ue/out/characters/t3d  -> characters/t3d/u
"""
import os

REPO = os.environ.get("GASP_REPO", ".")
OUT = REPO + "/tools/ue/out"


def decode(b):
    return b.decode("utf-16") if b[:2] in (b"\xff\xfe", b"\xfe\xff") else b.decode("utf-8")


for src, dst in (("t3d", "t3d_utf8"), ("t3d_widgets", "t3d_widgets/u"), ("characters/t3d", "characters/t3d/u")):
    n = 0
    for root, dirs, files in os.walk(os.path.join(OUT, src)):
        dirs[:] = [d for d in dirs if d != "u"]
        for f in files:
            if not f.endswith(".t3d"):
                continue
            p = os.path.join(root, f)
            q = os.path.join(OUT, dst, os.path.relpath(p, os.path.join(OUT, src)))
            os.makedirs(os.path.dirname(q), exist_ok=True)
            with open(p, "rb") as fi, open(q, "wb") as fo:
                fo.write(decode(fi.read()).encode("utf-8"))
            n += 1
    print(src, "->", dst, n, "files")
