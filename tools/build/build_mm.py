import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Builds GASP motion matching search indices exactly the way UE's PoseSearch does (see PoseSearchDerivedData.cpp).

Outputs (into godot/data/mm):
  mm.json  - schemas (flattened channels), databases (assets, params, tags), anim name table
  <db>.bin - float32 blocks: values[N*D], weights_sqrt[D], pca_mean[D], pca_proj[D*K], pca_values[N*K], cost_addend[N], flags[N]
"""
import json, os, re, sys
import numpy as np
from animdata import load_anims, OUT
from uemath import qaxis

FINITE_DELTA = 1.0 / 60.0
EXTRACTION_INTERVAL = 1.0 / 120.0
DST = REPO + "/godot/data/mm"
T3D = OUT + "/t3d_utf8"

TRAJ_FLAGS = {"Velocity": 1, "Position": 2, "VelocityDirection": 4, "FacingDirection": 8,
              "VelocityXY": 16, "PositionXY": 32, "VelocityDirectionXY": 64, "FacingDirectionXY": 128}


def obj_path(p):
    """'/Game/a/B.B' -> '/Game/a/B'"""
    return p.split(".")[0]


# ------------------------------------------------------------------------------------------------------------------
# schema flattening
def flatten_schema(schema):
    """Returns leaf channels in data order with offsets, cardinality, weight and normalization label."""
    leaves = []

    def add(ch):
        strip = ch.get("component_stripping", "NONE")
        card = 1 if ch.get("type") == "Curve" else {"NONE": 3, "STRIP_Z": 2, "STRIP_XY": 1}[strip]
        ch = dict(ch)
        ch["cardinality"] = card
        leaves.append(ch)

    def walk(c, outer):
        cls = c["class"]
        if cls == "PoseSearchFeatureChannel_Trajectory":
            for s in c["samples"]:
                f = s["flags"]
                base = {"bone": "", "origin_bone": "", "sample_time_offset": s["offset"], "origin_time_offset": 0.0,
                        "weight": s["weight"] * c["weight"], "input_query_pose": "USE_CHARACTER_POSE",
                        "normalization_group": s.get("normalization_group", "None"), "outer": "Traj"}
                if f & (TRAJ_FLAGS["Position"] | TRAJ_FLAGS["PositionXY"]):
                    add(dict(base, type="Position", component_stripping="STRIP_Z" if f & TRAJ_FLAGS["PositionXY"] else "NONE"))
                if f & (TRAJ_FLAGS["Velocity"] | TRAJ_FLAGS["VelocityXY"]):
                    add(dict(base, type="Velocity", use_char_space_vel=False, normalize=False,
                             component_stripping="STRIP_Z" if f & TRAJ_FLAGS["VelocityXY"] else "NONE"))
                if f & (TRAJ_FLAGS["VelocityDirection"] | TRAJ_FLAGS["VelocityDirectionXY"]):
                    add(dict(base, type="Velocity", use_char_space_vel=False, normalize=True,
                             component_stripping="STRIP_Z" if f & TRAJ_FLAGS["VelocityDirectionXY"] else "NONE"))
                if f & (TRAJ_FLAGS["FacingDirection"] | TRAJ_FLAGS["FacingDirectionXY"]):
                    add(dict(base, type="Heading", heading_axis="X",
                             component_stripping="STRIP_Z" if f & TRAJ_FLAGS["FacingDirectionXY"] else "NONE"))
        elif cls == "PoseSearchFeatureChannel_Group":
            for sc in c["sub_channels"]:
                walk(sc, outer + "Group/")
        elif cls == "PoseSearchFeatureChannel_Curve":
            # FPoseSearchFeatureChannel_Curve: curve value at SampleTimeOffset
            add({"type": "Curve", "curve": c["curve_name"], "bone": "", "origin_bone": "", "sample_time_offset": c["sample_time_offset"],
                 "origin_time_offset": 0.0, "weight": c["weight"], "input_query_pose": c["input_query_pose"],
                 "normalization_group": c["normalization_group"], "component_stripping": "NONE", "outer": outer})
        elif cls == "PoseSearchFeatureChannel_Pose":
            for sb in c["sampled_bones"]:
                base = {"bone": sb["bone"], "origin_bone": "", "sample_time_offset": 0.0, "origin_time_offset": 0.0,
                        "weight": sb["weight"] * c["weight"], "input_query_pose": c["input_query_pose"],
                        "normalization_group": "None", "component_stripping": "NONE", "outer": outer + "Pose/"}
                if sb["flags"] & 2:
                    add(dict(base, type="Position"))
                if sb["flags"] & 4:
                    add(dict(base, type="Heading", heading_axis="X"))
                    add(dict(base, type="Heading", heading_axis="Y"))
                if sb["flags"] & 1:
                    add(dict(base, type="Velocity", use_char_space_vel=True, normalize=False))
                if sb["flags"] & 8:
                    raise NotImplementedError("phase channel")
        else:
            t = {"PoseSearchFeatureChannel_Position": "Position", "PSC_Traversal_Pos_C": "Position",
                 "PoseSearchFeatureChannel_Velocity": "Velocity", "PoseSearchFeatureChannel_Heading": "Heading",
                 "PSC_Traversal_Head_C": "Heading"}[cls]
            d = {"type": t, "bone": "" if c["bone"] == "None" else c["bone"],
                 "origin_bone": "" if c.get("origin_bone", "None") == "None" else c["origin_bone"],
                 "sample_time_offset": c["sample_time_offset"], "origin_time_offset": c["origin_time_offset"],
                 "weight": c["weight"], "input_query_pose": c["input_query_pose"],
                 "component_stripping": c["component_stripping"], "normalization_group": c["normalization_group"],
                 "outer": outer, "bp_override": cls.startswith("PSC_")}
            if t == "Velocity":
                d["use_char_space_vel"] = c.get("b_use_character_space_velocities", c.get("use_character_space_velocities", True))
                d["normalize"] = c.get("b_normalize", c.get("normalize", False))
            if t == "Heading":
                d["heading_axis"] = c["heading_axis"]
            add(d)

    for c in schema["channels"]:
        walk(c, "")
    off = 0
    for ch in leaves:
        ch["offset"] = off
        off += ch["cardinality"]
        ch["label"] = "|".join(str(ch.get(k)) for k in ("outer", "type", "curve", "bone", "origin_bone", "sample_time_offset",
                                                       "origin_time_offset", "component_stripping", "heading_axis",
                                                       "normalize", "use_char_space_vel", "input_query_pose"))
    return leaves, off


# ------------------------------------------------------------------------------------------------------------------
# database asset parsing (T3D)
def parse_db_assets(db_path):
    rel = db_path.replace("/Game/", "") + ".t3d"
    txt = open(os.path.join(T3D, rel), encoding="utf-8").read()
    out = []
    for m in re.finditer(r"AnimationAssets\(\d+\)=/Script/PoseSearch\.(\w+)\((.*)\)\s*$", txt, re.M):
        kind, body = m.group(1), m.group(2)
        asset = re.search(r"(?:Sequence|AnimMontage)=\"[^']*'([^']*)'\"", body).group(1)
        sr = re.search(r"SamplingRange=\(([^)]*)\)", body)
        smin = smax = 0.0
        if sr and sr.group(1):
            mm = re.search(r"Min=([-0-9.]+)", sr.group(1)); smin = float(mm.group(1)) if mm else 0.0
            mx = re.search(r"Max=([-0-9.]+)", sr.group(1)); smax = float(mx.group(1)) if mx else 0.0
        out.append({"kind": kind, "asset": obj_path(asset), "sampling_range": [smin, smax],
                    "enabled": "bEnabled=False" not in body,
                    "disable_reselection": "bDisableReselection=True" in body,
                    "mirror": re.search(r"MirrorOption=(\w+)", body).group(1) if "MirrorOption=" in body else "UnmirroredOnly"})
    return out


def parse_montage(path):
    rel = path.replace("/Game/", "") + ".t3d"
    txt = open(os.path.join(T3D, rel), encoding="utf-8").read()
    segs = re.findall(r"AnimSegments=\(\((.*?)\)\)\)\)", txt)
    seg = re.search(r"AnimReference=\"[^']*'([^']*)'\"", txt).group(1)
    end = re.search(r"AnimEndTime=([-0-9.]+)", txt)
    start = re.search(r"AnimStartTime=([-0-9.]+)", txt)
    length = float(re.search(r"^\s*SequenceLength=([-0-9.]+)", txt, re.M).group(1))
    assert len(re.findall(r"AnimReference=", txt)) == 1, path
    assert start is None or float(start.group(1)) == 0.0, path
    return {"sequence": obj_path(seg), "length": length, "anim_end": float(end.group(1)) if end else None}


# ------------------------------------------------------------------------------------------------------------------
class NotifyWindows:
    def __init__(self):
        self.data = json.load(open(OUT + "/notifies.json"))

    def states(self, asset, cls):
        res = []
        for e in self.data.get(asset, []):
            ns = e.get("notify_state_class")
            if ns and ns["class"] == cls:
                res.append((e["time"], e["time"] + e["duration"], ns))
        return res

    def active(self, asset, t, length):
        """ExtractPoseSearchNotifyStates: notify states overlapping [min(t,L)-EI/2, +EI]."""
        start = min(t, length) - EXTRACTION_INTERVAL * 0.5
        end = start + EXTRACTION_INTERVAL
        res = []
        for e in self.data.get(asset, []):
            ns = e.get("notify_state_class")
            if not ns or not ns["class"].startswith("AnimNotifyState_PoseSearch"):
                continue
            a, b = e["time"], e["time"] + e["duration"]
            if a <= end and b > start:
                res.append(ns)
        return res


def valid_ranges(length, looping, sampling_range, exclude_params, exclusions):
    smin, smax = sampling_range
    if smin == 0.0 and smax == 0.0:
        lo, hi = 0.0, length
    else:
        lo, hi = smin, min(length, smax)
        lo = min(lo, hi)
    hi_excl = False
    if not looping:
        # FFloatRange(Min, Max) is [Min, Max)
        lo = max(lo, exclude_params[0])
        if length + exclude_params[1] <= hi:
            hi = length + exclude_params[1]
            hi_excl = True
    ranges = [(lo, hi)]
    for a, b in exclusions:
        nr = []
        for (x, y) in ranges:
            if b < x or a > y:
                nr.append((x, y)); continue
            if a > x: nr.append((x, a))
            if b < y: nr.append((b, y))
        ranges = nr
    return [r for r in ranges if r[1] >= r[0]]


# ------------------------------------------------------------------------------------------------------------------
class Indexer:
    """FAssetIndexer for one (anim, sampling interval)."""

    def __init__(self, anim, first, last, rate):
        self.anim = anim
        self.first = first
        self.last = last
        self.rate = rate
        # UE computes pose and sample times in float: SampleIdx * (1 / rate) + offset. In float64, 6 / 30 - 0.2 = -2.8e-17,
        # which GetSampleInfo treats as a negative (loop wrapped) time and corrupts the feature with the total root motion.
        self.times = np.arange(first, last + 1, dtype=np.float32) * np.float32(1.0 / rate)
        self._cache = {}

    def xf(self, bone, sample_time):
        key = (bone, float(sample_time[0]), len(sample_time))
        return self.anim.anim_space(bone or "root", sample_time)

    def sample_position(self, bone, origin_bone, ts, to):
        root, _ = self.anim.anim_space("root", to)
        b, clamped = self.anim.anim_space(bone or "root", ts)
        if not origin_bone:
            return root.inv_transform_position(b.t), clamped
        o, oc = self.anim.anim_space(origin_bone, to)
        return root.inv_transform_vector(b.t - o.t), clamped | oc

    def feature(self, ch):
        n = len(self.times)
        t = self.times
        ts = (t + np.float32(ch["sample_time_offset"])).astype(np.float64)
        to = (t + np.float32(ch["origin_time_offset"])).astype(np.float64)
        t32s = t + np.float32(ch["sample_time_offset"])
        t32o = t + np.float32(ch["origin_time_offset"])
        typ = ch["type"]
        if typ == "Curve":
            return np.asarray(self.anim.curve(ch["curve"], ts), dtype=np.float64).reshape(n, 1)
        if typ == "Position":
            v, _ = self.sample_position(ch["bone"], ch["origin_bone"], ts, to)
            return encode(v, ch["component_stripping"], False)
        if typ == "Heading":
            root, _ = self.anim.anim_space("root", to)
            b, _ = self.anim.anim_space(ch["bone"] or "root", ts)
            q = root.inv_transform_rotation(b.q)
            axis = {"X": 0, "Y": 1, "Z": 2}[ch["heading_axis"]]
            return encode(qaxis(q, axis), ch["component_stripping"], True)
        if typ == "Velocity":
            cs = ch["use_char_space_vel"]
            fd = np.float32(FINITE_DELTA)
            f64 = lambda x: x.astype(np.float64)
            past, cpast = self.sample_position(ch["bone"], ch["origin_bone"], f64(t32s - fd), f64(t32o - fd) if cs else to)
            pres, _ = self.sample_position(ch["bone"], ch["origin_bone"], ts, to)
            fut, _ = self.sample_position(ch["bone"], ch["origin_bone"], f64(t32s + fd), f64(t32o + fd) if cs else to)
            vel = np.where(cpast[:, None], (fut - pres) / FINITE_DELTA, (pres - past) / FINITE_DELTA)
            if ch["normalize"]:
                ln = np.linalg.norm(vel, axis=1, keepdims=True)
                vel = np.where(ln > 1.0, vel / np.maximum(ln, 1e-12), vel)
            return encode(vel, ch["component_stripping"], False)
        raise ValueError(typ)


def encode(v, strip, normalize):
    v = np.array(v, dtype=np.float64)
    if strip == "STRIP_Z":
        v = v[:, 0:2]
    elif strip == "STRIP_XY":
        v = v[:, 2:3]
    if normalize:
        ln = np.linalg.norm(v, axis=1, keepdims=True)
        v = np.where(ln > 1e-8, v / np.maximum(ln, 1e-12), 0.0)
    return v


# ------------------------------------------------------------------------------------------------------------------
def build_search_index_base(db_path, db, schema_leaves, card, rate, anims, montages, notifies):
    """InitSearchIndexAssets + IndexDatabase: returns values, metadata, assets."""
    assets_out, values, cost, flags = [], [], [], []
    total = 0
    for src_idx, a in enumerate(parse_db_assets(db_path)):
        if not a["enabled"]:
            continue
        assert a["mirror"] == "UnmirroredOnly", a
        if a["kind"] == "PoseSearchDatabaseAnimMontage":
            mi = montages[a["asset"]]
            anim = anims[mi["sequence"]].as_montage(mi["length"])
            notify_asset = a["asset"]
        else:
            anim = anims[a["asset"]]
            notify_asset = a["asset"]
        L = anim.length
        excl = [(s, e) for (s, e, _) in notifies.states(notify_asset, "AnimNotifyState_PoseSearchExcludeFromDatabase")]
        for (lo, hi) in valid_ranges(L, anim.loop, a["sampling_range"], db["exclude_from_database_parameters"], excl):
            first = int(np.ceil(lo * rate - 1e-6))
            last = int(np.floor(hi * rate + 1e-6))
            if last < first:
                continue
            ix = Indexer(anim, first, last, rate)
            n = last - first + 1
            vec = np.zeros((n, card))
            for ch in schema_leaves:
                vec[:, ch["offset"]:ch["offset"] + ch["cardinality"]] = ix.feature(ch)
            for i, t in enumerate(ix.times):
                st = min(t, L)
                addend = db["base_cost_bias"]
                block = False
                for ns in notifies.active(notify_asset, st, L):
                    if ns["class"] == "AnimNotifyState_PoseSearchBlockTransition":
                        block = True
                    elif ns["class"] == "AnimNotifyState_PoseSearchModifyCost":
                        addend = ns["cost_addend"]
                if anim.loop:
                    addend += db["looping_cost_bias"]
                cost.append(addend); flags.append(1 if block else 0)
            values.append(vec)
            assets_out.append({"source_asset_idx": src_idx, "anim": anim.path, "montage": a["asset"] if a["kind"].endswith("Montage") else None,
                               "play_length": L, "looping": anim.loop, "disable_reselection": a["disable_reselection"],
                               "first_pose": total, "first_sample": first, "last_sample": last, "notify_asset": notify_asset})
            total += n
    vals = np.concatenate(values, axis=0) if values else np.zeros((0, card))
    return vals, np.array(cost), np.array(flags, dtype=np.int32), assets_out


def mean_deviation(groups_data, leaves, card):
    """FMeanDeviationCalculator::Calculate for a common schema."""
    dev = np.ones(card)
    groups = {}
    for ch in leaves:
        key = ("G", ch["normalization_group"]) if ch["normalization_group"] not in ("None", "", None) else ("L", ch["label"])
        groups.setdefault(key, []).append(ch)
    for key, chans in groups.items():
        c0 = chans[0]["cardinality"]
        if any(c["cardinality"] != c0 for c in chans):
            # incompatible cardinality inside a normalization group: UE keeps them in separate entry groups
            sub = {}
            for c in chans:
                sub.setdefault(c["cardinality"], []).append(c)
            parts = list(sub.values())
        else:
            parts = [chans]
        for part in parts:
            rows = np.concatenate([d[:, c["offset"]:c["offset"] + c["cardinality"]] for d in groups_data for c in part], axis=0)
            if rows.shape[0] == 0:
                continue
            centered = rows - rows.mean(axis=0)
            md = np.linalg.norm(centered, axis=1).mean()
            for c in part:
                dev[c["offset"]:c["offset"] + c["cardinality"]] = md if md > 0.1 else 1.0
    return dev


def main():
    os.makedirs(DST, exist_ok=True)
    ps = json.load(open(OUT + "/posesearch.json"))
    skel, anims, index, raw = load_anims()
    notifies = NotifyWindows()
    montages = {}
    for p in [p for p in ps and []]:
        pass
    dbs = {obj_path(k): v for k, v in ps["databases"].items()}
    schemas = {}
    for k, v in ps["schemas"].items():
        leaves, card = flatten_schema(v)
        schemas[obj_path(k)] = {"leaves": leaves, "cardinality": card, "rate": v["sample_rate"], "pre": v["data_preprocessor"]}
    for dbp in dbs:
        for a in parse_db_assets(dbp):
            if a["kind"].endswith("Montage") and a["asset"] not in montages:
                montages[a["asset"]] = parse_montage(a["asset"])
    norm_sets = {obj_path(k): [obj_path(x) for x in v] for k, v in ps["normalization_sets"].items()}

    only = sys.argv[1:]
    base_cache = {}

    def base(dbp, schema_path):
        key = (dbp, schema_path)
        if key not in base_cache:
            s = schemas[schema_path]
            base_cache[key] = build_search_index_base(dbp, dbs[dbp], s["leaves"], s["cardinality"], s["rate"], anims, montages, notifies)
            print("  indexed", dbp.split("/")[-1], "with", schema_path.split("/")[-1], base_cache[key][0].shape, flush=True)
        return base_cache[key]

    out_dbs = {}
    for dbp, db in dbs.items():
        name = dbp.split("/")[-1]
        if only and name not in only:
            continue
        sp = obj_path(db["schema"])
        s = schemas[sp]
        vals, cost, flags, assets = base(dbp, sp)
        # normalization data sets
        datasets = [vals]
        if s["pre"] == "NORMALIZE_WITH_COMMON_SCHEMA" and db["normalization_set"]:
            for other in norm_sets[obj_path(db["normalization_set"])]:
                if other != dbp:
                    datasets.append(base(other, sp)[0])
        dev = mean_deviation(datasets, s["leaves"], s["cardinality"]) if s["pre"] != "NONE" else np.ones(s["cardinality"])
        # weights
        w = np.zeros(s["cardinality"])
        for ch in s["leaves"]:
            w[ch["offset"]:ch["offset"] + ch["cardinality"]] = ch["weight"]
        if s["pre"] in ("NORMALIZE", "NORMALIZE_WITH_COMMON_SCHEMA") and abs(w.sum()) > 1e-8:
            w = w / w.sum()
        wsqrt = np.sqrt(w)
        if s["pre"] != "NONE":
            wsqrt = wsqrt / dev
        # PCA
        K = db["number_of_principal_components"]
        mean = np.zeros(s["cardinality"]); proj = np.zeros((s["cardinality"], K)); pca = np.zeros((len(vals), K))
        if db["pose_search_mode"] == "PCAKD_TREE" and len(vals) > 1:
            wv = vals * wsqrt
            mean = wv.mean(axis=0)
            cen = wv - mean
            cov = cen.T @ cen / float(len(vals) - 1)
            evals, evecs = np.linalg.eigh(cov)
            order = np.argsort(-evals)[:K]
            proj = evecs[:, order]
            pca = cen @ proj
        blob = np.concatenate([vals.ravel(), wsqrt, mean, proj.ravel(), pca.ravel(), cost]).astype(np.float32)
        with open(os.path.join(DST, name + ".bin"), "wb") as f:
            f.write(blob.tobytes())
            f.write(flags.astype(np.int32).tobytes())
        out_dbs[name] = {"path": dbp, "schema": sp.split("/")[-1], "num_poses": int(len(vals)), "dims": s["cardinality"], "pcs": K,
                         "mode": db["pose_search_mode"], "kd_neighbors": db["kd_tree_query_num_neighbors"],
                         "continuing_pose_cost_bias": db["continuing_pose_cost_bias"], "base_cost_bias": db["base_cost_bias"],
                         "looping_cost_bias": db["looping_cost_bias"], "tags": db["tags"], "assets": assets,
                         "min_cost_addend": float(cost.min()) if len(cost) else 0.0}
        print(name, len(vals), "poses", s["cardinality"], "dims", flush=True)

    meta_path = os.path.join(DST, "mm.json")
    prev = json.load(open(meta_path)) if (only and os.path.exists(meta_path)) else {"databases": {}}
    prev["databases"].update(out_dbs)
    prev["schemas"] = {k.split("/")[-1]: {"cardinality": v["cardinality"], "rate": v["rate"], "channels": v["leaves"]} for k, v in schemas.items()}
    prev["montages"] = montages
    json.dump(prev, open(meta_path, "w"), indent=1)


if __name__ == "__main__":
    main()
