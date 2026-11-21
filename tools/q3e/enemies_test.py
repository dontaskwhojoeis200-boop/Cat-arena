#!/usr/bin/env python3
"""N10-Q3E stage D part 4: scripted test of the 20 enemy types on Reactor Bastion (client console
script) and the evaluation of its log.

  enemies_test.py write <lineup.cfg> <waves.cfg>   the two scripts
  enemies_test.py check <lineup log> <waves log>   measured values and PASS / FAIL per check

Run the client twice:
  +set ca_waves 0 +set ca_log 1 +devmap ca_test +wait 150 +exec lineup.cfg
  +set ca_waves 0 +set ca_log 1 +set ca_loadout "11 0" +devmap ca_reactor +wait 150 +exec waves.cfg

lineup.cfg: all 20 types lined up in front of the player in the test arena (ca_lineup) with
pictures of them standing, in their attack wind-up and strike. waves.cfg: waves on Reactor Bastion,
god mode, the player standing on the low level firing at the nearest visible enemy; the IRON
CHAINGUN is put back in hand every 5 shots and the player back on the spot every 100, so frag
rerolls into explosives cannot throw the player off the spot (see TEST_MATRIX: blasts can throw a
player outside the N10 map footprint); then god mode off for a while and on again. ca_log prints
every enemy spawn, every hit on the player (melee or projectile kind) and every kill.
"""
import os
import re
import sys

TYPES = ["grunt", "ogre", "fiend", "shambler", "marauder", "warden", "skitter", "stalker", "brute", "pyromancer",
         "sentinel", "reaver", "banshee", "crusher", "sapper", "harrier", "cultist", "skyprobe", "skyraider", "skyjuggernaut"]
FLYERS = TYPES[17:]
START = "ca_place -150 -400 -80 90"
SPOTS = [START, "ca_place 0 -700 208 90"]   # Reactor Bastion: low level, high bridge


def plan_lineup():
    return ["ca_place -700 -300 40 0", "wait 40", "god", "ca_lineup", "wait 70", "screenshotJPEG enemies_lineup_idle",
            "wait 98", "screenshotJPEG enemies_lineup_windup", "wait 30", "screenshotJPEG enemies_lineup_strike",
            "ca_place -755 -300 40 0 4", "cg_fov 110", "wait 40", "screenshotJPEG enemies_lineup_wide", "cg_fov 90", "wait 10", "echo CATEST end", "quit"]


def roof_height(world, x, y, z):
    """Lowest N10 world-mesh surface above (x, y, z) over a 0.6 m ring (99: open sky)."""
    import numpy as np
    A, B, C = world
    e1, e2 = B - A, C - A
    best = 99.0
    for ox, oy in ((0, 0), (.6, 0), (-.6, 0), (0, .6), (0, -.6)):
        s = np.array([x + ox, y + oy, z + .3]) - A
        p = np.stack([-e2[:, 1], e2[:, 0], np.zeros(len(e2))], 1)   # cross((0, 0, 1), e2)
        det = (e1 * p).sum(1)
        ok = np.abs(det) > 1e-9
        inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
        u = (s * p).sum(1) * inv
        q = np.cross(s, e1)
        w = q[:, 2] * inv
        t = (e2 * q).sum(1) * inv
        hit = ok & (u >= 0) & (w >= 0) & (u + w <= 1) & (t > 1e-4)
        if hit.any():
            best = min(best, float(t[hit].min()))
    return best


def map_spots(root, map_id):
    """Two N10 spawn spots of a map, far apart (Quake III coordinates), among the spots with no
    roof within 9 m: N10's flyers steer greedily toward a point 2.45 m above the player and do
    not path-find, so under a roof they hover above it for good and the wave never ends (the
    same in N10; a player there moves on)."""
    import json
    import math
    import numpy as np
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import n10materials
    src = os.path.join(root, "assets", "n1", "maps", map_id)
    d = json.load(open(os.path.join(src, "map.json")))
    c = d["compiled"]
    cx, cy = c["meta"]["center"]
    stride, v, idx = n10materials.read_cmesh(os.path.join(src, "world.cmesh"))
    V = np.array(v, dtype=np.float64).reshape(-1, stride)[:, :3]
    T = np.array(idx, dtype=np.int64).reshape(-1, 3)
    world = (V[T[:, 0]], V[T[:, 1]], V[T[:, 2]])
    spawns = [s for s in c["spawns"] if roof_height(world, s["x"], s["y"], s["z"]) >= 9] or c["spawns"]
    pts = [((s["x"] - cx) * 32, (s["y"] - cy) * 32, s["z"] * 32 + 24 + 8, math.degrees(s["a"])) for s in spawns]
    a, b = max(((p, q) for p in pts for q in pts), key=lambda pq: math.hypot(pq[0][0] - pq[1][0], pq[0][1] - pq[1][1]))
    return ["ca_place %.0f %.0f %.0f %.0f" % a, "ca_place %.0f %.0f %.0f %.0f" % b]


def plan_waves(spots=None, god_off=(700,)):
    global START, SPOTS
    if spots:
        START, SPOTS = spots[0], spots
    lines = [START, "wait 40", "god", "set ca_waves 1", "wait 60", "ca_state"]
    for i in range(1100):
        lines += ["ca_aimnearest", "+attack", "wait 6", "-attack", "wait 1"]
        if i % 5 == 4:
            lines.append("ca_give 11")
        if i % 100 == 99:   # alternate between two spots, so no enemy keeps cover from the player for good
            lines.append(SPOTS[(i // 100 + 1) % 2])
        if i % 50 == 49:
            lines += ["ca_state", "ca_enemies"]
        if i in (60, 300, 600, 900):
            lines.append("screenshotJPEG enemies_fight_%d" % i)
        if i in god_off:
            lines += ["god", "echo CATEST god off"]
        if i - 60 in god_off:
            lines += ["ca_state", "god", "echo CATEST god on"]
    lines += ["ca_state", "wait 30", "echo CATEST end", "quit"]
    return lines


def write(lineup_path, waves_path, root=None, map_id=None, god_off="700"):
    spots = map_spots(root, map_id) if map_id else None
    for path, lines in ((lineup_path, plan_lineup()), (waves_path, plan_waves(spots, tuple(int(g) for g in str(god_off).split(","))))):
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


def check(lineup_path, waves_path):
    lineup = open(lineup_path, errors="replace").read() if lineup_path != "-" else None   # "-": waves only
    text = open(waves_path, errors="replace").read()
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    waves = text
    ok(re.search(r"Cat Arena: 20 enemy meshes loaded", text) is not None and (lineup is None or "Cat Arena: 20 enemy meshes loaded" in lineup), "20 N10 creature meshes loaded by cgame")
    if lineup is not None:
        lined = set(re.findall(r"Cat Arena: spawn (\w+)", lineup))
        ok(lined == set(TYPES), "lineup: all 20 types spawned (%d)" % len(lined))
    else:
        lineup = ""
    spawned = re.findall(r"Cat Arena: spawn (\w+)", waves)
    kinds = set(spawned)
    ok(len(kinds) >= 16 and set(FLYERS) <= kinds, "waves: %d spawns of %d types, the three flyers among them" % (len(spawned), len(kinds)))
    killed = re.findall(r"Cat Arena: killed (\w+)", waves)
    ok(len(set(killed)) >= 14 and set(FLYERS) & set(killed), "kills: %d of %d types (%s)" % (len(killed), len(set(killed)), ", ".join(sorted(set(killed)))))
    hits = re.findall(r"Cat Arena: hit by (\w+) (\w+) (\d+)", waves)
    by_kind = {}
    for who, kind, dmg in hits:
        by_kind.setdefault(kind, set()).add(who)
    ok(all(k in by_kind for k in ("shot", "spike", "fireball", "voidorb", "melee")),
       "player hit by every attack kind: " + "; ".join("%s by %s" % (k, ", ".join(sorted(v))) for k, v in sorted(by_kind.items())))
    flyer_hits = [h for h in hits if h[0] in FLYERS]
    ok(len(flyer_hits) > 0, "flyers attacked the player %d times" % len(flyer_hits))
    god_off = ""
    for rest in text.split("CATEST god off")[1:]:   # every window, up to the state asked for just before god mode went on
        head, _, tail = rest.partition("CATEST god on")   # again (server prints arrive after the local echo lines)
        m = re.search(r"CA_STATE .*", tail)
        god_off += head + (m.group(0) if m else "") + "\n"
    off_states = [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", god_off)]
    took = len(re.findall(r"Cat Arena: hit by", god_off))
    # damage shows as a health below 100 in the window, or a death (respawn at 100); a medkit can heal it back
    lowest = min([int(st["health"]) for st in off_states] or [100])
    died = "died" in god_off
    ok(took > 0 and (lowest < 100 or died or len(off_states) == 0), "without god mode: %d hits, lowest health %d%s" % (took, lowest, ", died" if died else ""))
    reached = [int(w) for w in re.findall(r"Cat Arena: wave (\d+),", waves)]
    ok(max(reached or [0]) >= 3, "waves reached: %s" % (max(reached or [0])))
    ok(all("ERROR" not in t and "Signal caught" not in t for t in (lineup, text)), "no errors")
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":   # write <lineup.cfg> <waves.cfg> [<repo root> <N10 map id> [<god off at shot>]] (default: Reactor Bastion spots, 700)
        write(sys.argv[2], sys.argv[3], *sys.argv[4:7])
    else:
        sys.exit(0 if check(sys.argv[2], sys.argv[3]) else 1)
