import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Animation sampling replicating UE's FAnimationAssetSampler / FAssetIndexer (UE space, cm)."""
import json
import numpy as np
from uemath import Xf, nlerp

OUT = REPO + "/tools/ue/out"
SMALL_NUMBER = 1e-8
KINDA_SMALL = 1e-4
EXTRAPOLATION_SAMPLE_TIME = 1.0 / 30.0


class Skeleton:
    def __init__(self, index, godot_skel):
        self.names = index["bones"]
        self.idx = {n: i for i, n in enumerate(self.names)}
        gb = godot_skel["bones"]
        assert [b["name"] for b in gb] == self.names, "bone order mismatch between UE bake and Godot import"
        self.parents = [b["parent"] for b in gb]
        rl = np.array(index["ref_local"], dtype=np.float64)
        self.ref_t = rl[:, 0:3]
        self.ref_q = rl[:, 3:7]

    def chain(self, bone):
        out = []
        i = self.idx[bone]
        while i >= 0:
            out.append(i)
            i = self.parents[i]
        return out[::-1]


class Anim:
    """One baked AnimSequence (raw keys) plus optional montage clipping."""

    def __init__(self, entry, raw, skel):
        self.entry = entry
        self.name = entry["name"]
        self.path = entry["path"]
        self.skel = skel
        self.fps = entry["fps"]
        self.nkeys = entry["num_keys"]
        self.seq_length = entry["length"]
        self.length = entry["length"]
        self.loop = bool(entry["loop"])
        self.root_motion = bool(entry["root_motion"])
        self.force_root_lock = bool(entry["force_root_lock"])
        nb = len(skel.names)
        n = self.nkeys * nb * 10
        k = np.frombuffer(raw, dtype=np.float32, count=n, offset=entry["offset"]).astype(np.float64)
        k = k.reshape(self.nkeys, nb, 10)
        self.kt = k[:, :, 0:3]
        self.kq = k[:, :, 3:7]
        nc = len(entry["curves"])
        self.curve_names = entry["curves"]
        if nc:
            self.curves = np.frombuffer(raw, dtype=np.float32, count=self.nkeys * nc, offset=entry["curve_offset"]).reshape(self.nkeys, nc)
        else:
            self.curves = np.zeros((self.nkeys, 0), dtype=np.float32)
        self.lock_root = self.root_motion or self.force_root_lock

    def as_montage(self, montage_length):
        m = Anim.__new__(Anim)
        m.__dict__.update(self.__dict__)
        m.length = montage_length
        m.loop = False
        return m

    # ---- raw key sampling ----
    def _keys(self, t):
        x = np.clip(np.asarray(t, dtype=np.float64), 0.0, self.seq_length) * self.fps
        i0 = np.clip(np.floor(x).astype(int), 0, self.nkeys - 1)
        i1 = np.minimum(i0 + 1, self.nkeys - 1)
        return i0, i1, x - i0

    def local(self, bone, t):
        i0, i1, f = self._keys(t)
        tr = self.kt[i0, bone] * (1 - f)[:, None] + self.kt[i1, bone] * f[:, None]
        q = nlerp(self.kq[i0, bone], self.kq[i1, bone], f)
        return Xf(q, tr)

    def root_track(self, t):
        return self.local(0, t)

    def rm_range(self, a, b):
        """ExtractRootMotionFromRange(a, b) = Root(b).GetRelativeTransform(Root(a))."""
        a = np.broadcast_to(np.asarray(a, dtype=np.float64), np.shape(b))
        if not self.root_motion:
            return Xf.identity(np.shape(b))
        return self.root_track(b).relative_to(self.root_track(a))

    @staticmethod
    def _extrapolate(sample, ext_time):
        """UE::PoseSearch::ExtrapolateRootMotion with SampleDelta = EXTRAPOLATION_SAMPLE_TIME."""
        abs_t = np.abs(ext_time)
        s = Xf.where(ext_time >= 0.0, sample, sample.inverse())
        mult = abs_t / EXTRAPOLATION_SAMPLE_TIME
        nint = np.floor(mult).astype(int)
        frac = mult - nint
        out = Xf.identity(np.shape(ext_time))
        for i in range(int(nint.max()) if nint.size else 0):
            step = s * out
            out = Xf.where(i < nint, step, out)
        rem = s.blend_from_identity(frac)
        return rem * out

    def extract_root_transform(self, t):
        t = np.atleast_1d(np.asarray(t, dtype=np.float64))
        L = self.length
        if self.loop:
            return self.rm_range(np.zeros_like(t), np.clip(t, 0, L))
        clamped = np.clip(t, 0.0, L)
        ext = t - clamped
        res = self.rm_range(np.zeros_like(t), clamped)
        neg = ext < -SMALL_NUMBER
        pos = ext > SMALL_NUMBER
        if neg.any():
            s = self.rm_range(np.zeros_like(t), np.full_like(t, EXTRAPOLATION_SAMPLE_TIME))
            e = self._extrapolate(s, ext)
            res = Xf.where(neg, e, res)
        if pos.any():
            s = self.rm_range(np.full_like(t, L - EXTRAPOLATION_SAMPLE_TIME), np.full_like(t, L))
            e = self._extrapolate(s, ext) * res
            res = Xf.where(pos, e, res)
        return res

    def total_root_transform(self):
        return self.extract_root_transform(np.array([self.length])).relative_to(self.extract_root_transform(np.array([0.0])))

    def sample_info(self, t):
        """FAssetIndexer::GetSampleInfo -> (root transform, clip time, clamped flag)."""
        t = np.atleast_1d(np.asarray(t, dtype=np.float64))
        L = self.length
        can_wrap = self.loop and L > KINDA_SMALL
        main = t + np.where((t < 0) & can_wrap, L, 0.0)
        wrapped = main.copy()
        cycles = np.zeros(t.shape, dtype=int)
        if can_wrap:
            negm = wrapped < 0
            k = np.where(negm, np.ceil(-wrapped / L), 0).astype(int)
            wrapped = wrapped + k * L
            cycles += k
            posm = wrapped > L
            k2 = np.where(posm, np.ceil((wrapped - L) / L), 0).astype(int)
            wrapped = wrapped - k2 * L
            cycles += k2
        pclamped = np.clip(wrapped, 0.0, L)
        extrap = wrapped - pclamped
        wrapped = pclamped
        clamped = np.abs(extrap) > SMALL_NUMBER
        clip_time = np.where(clamped, wrapped + extrap, wrapped)
        root_clamped = self.extract_root_transform(clip_time)
        remainder = self.extract_root_transform(wrapped)
        neg_t = t < 0
        if (cycles > 0).any() or neg_t.any():
            last = self.total_root_transform()
            last_b = Xf(np.broadcast_to(last.q, t.shape + (4,)), np.broadcast_to(last.t, t.shape + (3,)))
            per_cycle = Xf.where(neg_t, last_b.inverse(), last_b)
            remainder = Xf.where(neg_t, remainder.relative_to(last_b), remainder)
            acc = Xf.identity(t.shape)
            for i in range(int(cycles.max())):
                acc = Xf.where(i < cycles, per_cycle * acc, acc)
            root_unclamped = remainder * acc
        else:
            root_unclamped = remainder
        root = Xf.where(clamped, root_clamped, root_unclamped)
        return root, clip_time, clamped

    def component_space(self, bone_name, clip_time):
        ct = np.clip(clip_time, 0.0, self.length) if not self.loop else clip_time
        acc = None
        for b in self.skel.chain(bone_name):
            if b == 0 and self.lock_root:
                loc = Xf(np.broadcast_to(self.skel.ref_q[0], ct.shape + (4,)), np.broadcast_to(self.skel.ref_t[0], ct.shape + (3,)))
            else:
                loc = self.local(b, ct)
            acc = loc if acc is None else loc * acc
        return acc

    def anim_space(self, bone_name, sample_time):
        """FAssetIndexer::GetTransform: CS(bone) * RootTransform, plus clamped flag."""
        root, clip, clamped = self.sample_info(sample_time)
        cs = self.component_space(bone_name, clip if self.loop else np.clip(clip, 0.0, self.length))
        return cs * root, clamped

    def curve(self, name, t):
        if name not in self.curve_names:
            return np.zeros(np.shape(t))
        c = self.curves[:, self.curve_names.index(name)].astype(np.float64)
        i0, i1, f = self._keys(t)
        return c[i0] * (1 - f) + c[i1] * f


def load_anims():
    index = json.load(open(OUT + "/anims/anims.json"))
    gsk = json.load(open(REPO + "/godot/data/godot_skeleton.json"))
    skel = Skeleton(index, gsk)
    raw = open(OUT + "/anims/anims.bin", "rb").read()
    anims = {e["path"]: Anim(e, raw, skel) for e in index["anims"]}
    return skel, anims, index, raw
