"""Per-notify FAnimNotifyEvent::TriggerWeightThreshold from the GASP .uasset files -> godot/data/notify_thresholds.json.

The notify export (tools/ue/export_notifies.py) did not keep TriggerWeightThreshold. Many GASP foley notifies use 0.2:
FAnimNotifyQueue::AddAnimNotifiesToDest only queues a notify when TriggerWeightThreshold <= the player's blend weight
(default ZERO_ANIMWEIGHT_THRESH), so footsteps of motion matching blend stack players that are blending out stay silent.

Reads the packages read-only (no editor): the name map is located from the summary, the "Notifies" ArrayProperty tag is
found and its struct elements are walked as UE 5.4+ tagged properties (Name, FPropertyTypeName tree, Size, Flags...),
collecting each element's LinkValue (trigger time) and TriggerWeightThreshold. Elements are in the same order as
UAnimationLibrary::GetAnimationNotifyEvents (notifies.json); the output lists one threshold per notifies.json event.

usage: build_notify_thresholds.py <GASP Content dir> <godot/data/notifies.json> <out.json>
"""
import glob
import json
import os
import struct
import sys

DEFAULT = 0.00001  # ZERO_ANIMWEIGHT_THRESH


def parse_names(b):
    """Name map: scan the summary for (NameCount, NameOffset) that parses as a consistent FNameEntrySerialized list;
    the longest one wins (a stray pair can parse a tail of the real map)."""
    best = None
    for pos in range(0, min(len(b), 65536) - 8):
        cnt, off = struct.unpack_from("<ii", b, pos)
        if not (10 < cnt < 100000 and 64 < off < len(b)):
            continue
        names, p, ok = [], off, True
        for _ in range(cnt):
            if p + 4 > len(b):
                ok = False
                break
            n = struct.unpack_from("<i", b, p)[0]
            p += 4
            if 0 < n < 1024 and p + n <= len(b) and b[p + n - 1] == 0:
                names.append(b[p:p + n - 1].decode("latin1"))
                p += n
            elif -1024 < n < 0 and p - n * 2 <= len(b):
                try:
                    names.append(b[p:p - n * 2 - 2].decode("utf-16-le"))
                except UnicodeDecodeError:
                    ok = False
                    break
                p += -n * 2
            else:
                ok = False
                break
            p += 4  # case-preserving hash
        if ok and names and (best is None or len(names) > len(best)):
            best = names
    return best


class Reader:
    def __init__(self, b, names):
        self.b, self.names = b, names

    def name(self, p):
        i, n = struct.unpack_from("<ii", self.b, p)
        if not (0 <= i < len(self.names)) or n < 0 or n > 1000:
            raise ValueError("bad name at %d" % p)
        return self.names[i], p + 8

    def type_name(self, p):
        """FPropertyTypeName: Name, InnerCount, then InnerCount nested type names."""
        n, p = self.name(p)
        cnt = struct.unpack_from("<i", self.b, p)[0]
        p += 4
        if not (0 <= cnt <= 4):
            raise ValueError("bad type param count")
        inner = []
        for _ in range(cnt):
            t, p = self.type_name(p)
            inner.append(t)
        return (n, inner), p

    def tag(self, p):
        """-> (name, type, size, value_pos, end) or (None, ...) for the 'None' terminator."""
        n, p = self.name(p)
        if n == "None":
            return None, None, 0, p, p
        t, p = self.type_name(p)
        size = struct.unpack_from("<i", self.b, p)[0]
        p += 4
        flags = self.b[p]
        p += 1
        if flags & 0x01:  # HasArrayIndex
            p += 4
        if flags & 0x02:  # HasPropertyGuid
            p += 16
        if flags & 0x04:  # HasPropertyExtensions
            ext = self.b[p]
            p += 1
            if ext & 0x02:  # OverridableInformation
                p += 1
        if size < 0 or p + size > len(self.b):
            raise ValueError("bad size")
        return n, t, size, p, p + size

    def struct_props(self, p):
        """Tagged properties of one struct value until 'None'; returns ({name: (type, value_pos, size)}, end)."""
        props = {}
        for _ in range(200):
            n, t, size, vp, end = self.tag(p)
            if n is None:
                return props, end
            props[n] = (t, vp, size)
            p = end
        raise ValueError("unterminated struct")


def notify_elements(b, names):
    r = Reader(b, names)
    if "Notifies" not in names:
        return None
    pat = struct.pack("<ii", names.index("Notifies"), 0)
    p = b.find(pat)
    while p >= 0:
        try:
            n, t, size, vp, end = r.tag(p)
            if t and t[0] == "ArrayProperty" and t[1] and t[1][0][0] == "StructProperty":
                cnt = struct.unpack_from("<i", b, vp)[0]
                q = vp + 4
                elems = []
                for _ in range(cnt):
                    props, q = r.struct_props(q)
                    lv = props.get("LinkValue")
                    tw = props.get("TriggerWeightThreshold")
                    elems.append((struct.unpack_from("<f", b, lv[1])[0] if lv else 0.0,
                                  struct.unpack_from("<f", b, tw[1])[0] if tw else DEFAULT))
                if q == end:
                    return elems
        except (ValueError, struct.error, IndexError):
            pass
        p = b.find(pat, p + 1)
    return None


def main():
    root, notifies_json, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    notifies = json.load(open(notifies_json, encoding="utf-8"))
    out, stats = {}, {"assets": 0, "parsed": 0, "count_mismatch": 0, "time_mismatch": 0, "non_default": 0}
    for f in glob.glob(os.path.join(root, "**", "*.uasset"), recursive=True):
        key = "/Game/" + os.path.relpath(f, root).replace("\\", "/")[:-len(".uasset")]
        if key not in notifies:
            continue
        stats["assets"] += 1
        b = open(f, "rb").read()
        names = parse_names(b)
        elems = notify_elements(b, names) if names else None
        if elems is None:
            continue
        evs = notifies[key]
        if len(elems) != len(evs):
            stats["count_mismatch"] += 1
            continue
        if any(abs(e.get("time", 0.0) - t) > 1e-3 for e, (t, _) in zip(evs, elems)):
            stats["time_mismatch"] += 1
            continue
        stats["parsed"] += 1
        vals = [round(w, 6) for _, w in elems]
        stats["non_default"] += sum(1 for w in vals if abs(w - DEFAULT) > 1e-9)
        out[key] = vals
    json.dump(out, open(out_path, "w"), indent=0)
    print(stats)


if __name__ == "__main__":
    main()
