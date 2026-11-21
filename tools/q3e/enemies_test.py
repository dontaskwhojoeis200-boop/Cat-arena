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


def plan_waves():
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
        if i == 700:
            lines += ["god", "echo CATEST god off"]
        if i == 760:
            lines += ["ca_state", "god", "echo CATEST god on"]
    lines += ["ca_state", "wait 30", "echo CATEST end", "quit"]
    return lines


def write(lineup_path, waves_path):
    for path, lines in ((lineup_path, plan_lineup()), (waves_path, plan_waves())):
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


def check(lineup_path, waves_path):
    lineup = open(lineup_path, errors="replace").read()
    text = open(waves_path, errors="replace").read()
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    waves = text
    ok(re.search(r"Cat Arena: 20 enemy meshes loaded", lineup) is not None and re.search(r"Cat Arena: 20 enemy meshes loaded", text) is not None, "20 N10 creature meshes loaded by cgame (both maps)")
    lined = set(re.findall(r"Cat Arena: spawn (\w+)", lineup))
    ok(lined == set(TYPES), "lineup: all 20 types spawned (%d)" % len(lined))
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
    god_off = text.split("CATEST god off")[1].split("CATEST god on")[0] if "CATEST god off" in text else ""
    off_states = [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", god_off)]
    took = len(re.findall(r"Cat Arena: hit by", god_off))
    ok(took > 0 and (len(off_states) == 0 or int(off_states[-1]["health"]) < 125), "without god mode: %d hits, health then %s" % (took, off_states[-1]["health"] if off_states else "?"))
    reached = [int(w) for w in re.findall(r"Cat Arena: wave (\d+),", waves)]
    ok(max(reached or [0]) >= 3, "waves reached: %s" % (max(reached or [0])))
    ok(all("ERROR" not in t and "Signal caught" not in t for t in (lineup, text)), "no errors")
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2], sys.argv[3])
    else:
        sys.exit(0 if check(sys.argv[2], sys.argv[3]) else 1)
