#!/usr/bin/env python3
"""N10-Q3E stage D: checks a converted map against the N10 data it came from.

  map_test.py write <repo root> <map id> <name> <file.cfg>   console script (needs devmap: setviewpos, god, ca_state)
  map_test.py check <repo root> <map id> <name> <console log>

Checks (Quake III units, 32 u = 1 m; player origin = floor + 24):
  floors  dropped 0.25 m above every N10 spawn and pickup spot, the player ends on N10's floor
          height there (+-8 u): no missing floors, no invisible obstacles at those spots
  ramps   placed at the low end of each N10 ramp, walking uphill reaches its top (the height is
          sampled six times during about 2 s of walking)
  pads    standing on each N10 jump pad launches the player at least 2 m above the pad
"""
import json
import math
import os
import re
import sys

U = 32.0


def load(root, map_id):
    doc = json.load(open(os.path.join(root, "assets", "n1", "maps", map_id, "map.json")))
    return doc["compiled"]


def inside(poly, x, y):
    n, sign = len(poly), 0
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        cr = (bx - ax) * (y - ay) - (by - ay) * (x - ax)
        if abs(cr) < 1e-9:
            continue
        s = 1 if cr > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def slab_z(s, x, y):
    pl = s.get("plane")
    if not pl:
        return s["z"]
    u = ((x - pl["ox"]) * pl["dx"] + (y - pl["oy"]) * pl["dy"]) / pl["len"]
    u = min(1.0, max(0.0, u))
    return pl["z0"] + (pl["z1"] - pl["z0"]) * u


def floor_height(c, x, y, feet):   # levelFloorHeight: highest slab top at x, y not above feet + 0.52
    best = None
    for s in c["slabs"]:
        if inside(s["poly"], x, y):
            z = slab_z(s, x, y)
            if z <= feet + 0.52 and (best is None or z > best):
                best = z
    return best


def checks(c):
    cx, cy = c["meta"]["center"]
    q = lambda x, y, z: ((x - cx) * U, (y - cy) * U, z * U)
    out = []
    for kind, items in (("spawn", c["spawns"]), ("pickup", c["pickups"])):
        for i, it in enumerate(items):
            fz = floor_height(c, it["x"], it["y"], it["z"])
            if fz is None:
                continue
            out.append(("floor", "%s %d" % (kind, i), q(it["x"], it["y"], fz), fz))
    for s in c["slabs"]:
        pl = s.get("plane")
        if not pl or abs(pl["z1"] - pl["z0"]) < 0.3:
            continue
        # low end, 0.6 m inside, centre line; face uphill
        sgn = 1 if pl["z1"] > pl["z0"] else -1
        u0 = 0.0 if sgn > 0 else 1.0
        dx, dy = pl["dx"] * sgn, pl["dy"] * sgn
        bx = pl["ox"] + pl["dx"] * pl["len"] * u0 + dx * 0.6
        by = pl["oy"] + pl["dy"] * pl["len"] * u0 + dy * 0.6
        # centre across the ramp: average the poly's extent along the perpendicular
        px, py = -dy, dx
        proj = [(p[0] - bx) * px + (p[1] - by) * py for p in s["poly"]]
        mid = (max(proj) + min(proj)) / 2
        bx, by = bx + px * mid, by + py * mid
        low, high = min(pl["z0"], pl["z1"]), max(pl["z0"], pl["z1"])
        yaw = math.degrees(math.atan2(dy, dx))
        out.append(("ramp", s["id"], q(bx, by, low + (high - low) * 0.6 / pl["len"]), (low, high, yaw)))
    for i, pd in enumerate(c["pads"]):
        out.append(("pad", pd["id"], q(pd["x"], pd["y"], pd["z"]), pd["z"]))
    return out


RAMP_WAITS = (8, 10, 15, 20, 30, 42)


def write(root, map_id, name, path):
    lines = ["god", "wait 10"]
    for kind, label, (x, y, z), extra in checks(load(root, map_id)):
        lines.append("echo MAPTEST %s %s" % (kind, label))
        if kind == "floor":
            lines += ["ca_place %.1f %.1f %.1f 0" % (x, y, z + 24 + 8), "wait 90", "ca_state"]   # drop 0.25 m, settle
        elif kind == "ramp":
            # walk uphill; the height is sampled six times (short ramps, e.g. the pieces of a spiral,
            # end long before the walk does)
            lines += ["ca_place %.1f %.1f %.1f %.1f" % (x, y, z + 24 + 8, extra[2]), "wait 20", "ca_state", "+forward"]
            for wt in RAMP_WAITS:
                lines += ["wait %d" % wt, "ca_state"]
            lines += ["-forward"]
        else:
            lines += ["ca_place %.1f %.1f %.1f 0" % (x, y, z + 24 + 4), "wait 30", "ca_state", "wait 40", "ca_state", "wait 40", "ca_state", "wait 120", "ca_state"]
    lines += ["echo MAPTEST end", "wait 100", "quit"]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def check(root, map_id, name, log):
    states = [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", open(log, errors="replace").read())]
    pos = lambda st: tuple(float(v) for v in st["pos"].split(","))
    i, fails, total = 0, [], {"floor": [0, 0], "ramp": [0, 0], "pad": [0, 0]}
    for kind, label, (x, y, z), extra in checks(load(root, map_id)):
        need = {"floor": 1, "ramp": 1 + len(RAMP_WAITS), "pad": 4}[kind]
        st = states[i:i + need]
        i += need
        if len(st) < need:
            fails.append("%s %s: no state" % (kind, label))
            total[kind][1] += 1
            continue
        if kind == "floor":
            pz = pos(st[0])[2]
            ok = abs(pz - (z + 24)) <= 8
            if not ok:
                fails.append("floor %s at (%.0f, %.0f): z %.0f, expected %.0f" % (label, x, y, pz, z + 24))
        elif kind == "ramp":
            low, high, _ = extra
            z0, z1 = pos(st[0])[2], max(pos(q)[2] for q in st[1:])
            ok = abs(z0 - (z + 24)) <= 12 and z1 >= high * U + 24 - 8   # starts at the bottom, reaches the top
            if not ok:
                fails.append("ramp %s: z %.0f -> %.0f (bottom %.0f, top %.0f)" % (label, z0, z1, z + 24, high * U + 24))
        else:
            zs = [pos(s)[2] for s in st]
            ok = max(zs) - (z + 24) >= 2 * U
            if not ok:
                fails.append("pad %s: z %s, pad at %.0f" % (label, [round(v) for v in zs], z + 24))
        total[kind][0 if ok else 1] += 1
    for k, (p, f) in total.items():
        print("%s %s: %d passed, %d failed" % ("PASS" if f == 0 and p > 0 else "FAIL", k, p, f))
    for f in fails[:40]:
        print("  " + f)
    good = all(f == 0 for _, f in total.values())
    print("ALL PASS" if good else "FAILURES")
    return good


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(*sys.argv[2:6])
    else:
        sys.exit(0 if check(*sys.argv[2:6]) else 1)
