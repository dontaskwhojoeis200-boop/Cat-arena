#!/usr/bin/env python3
"""N10-Q3E stage D part 7: scripted client test of the medkits on Reactor Bastion.

  medkit_test.py write <repo root> <medkit.cfg>
  medkit_test.py check <console log>

Run: SDL_AUDIODRIVER=dummy CatArena +set ca_waves 0 +set ca_log 1 +devmap ca_reactor +wait 150 +exec medkit.cfg

The player spawns (health must be N10's 100), stands on the first N10 medkit at full health (not
taken), backs off and looks at it (picture), drops to 40 health and walks over it (taken: 80, the
medkit beep, picture without it), then waits on it at 80 health: it must come back and heal again
13 s after it was taken (80 -> 100).
"""
import json
import math
import os
import re
import sys


def write(root, path):
    c = json.load(open(os.path.join(root, "assets", "n1", "maps", "reactor-bastion", "map.json")))["compiled"]
    cx, cy = c["meta"]["center"]
    k = c["pickups"][0]
    x, y, z = (k["x"] - cx) * 32, (k["y"] - cy) * 32, k["z"] * 32 + 28
    on = "ca_place %.0f %.0f %.0f 0 20" % (x, y, z)
    lines = ["wait 40", "ca_state", on, "wait 30", "ca_state", "echo CATEST backoff",
             "+back", "wait 7", "-back", "wait 30", "screenshotJPEG medkit_available", "ca_health 40", "ca_state", "echo CATEST walk",
             "+forward", "wait 30", "-forward", "wait 20", "ca_state",
             on, "wait 10", "+back", "wait 7", "-back", "wait 30", "screenshotJPEG medkit_taken", on, "wait 10", "echo CATEST wait"]
    for _ in range(60):
        lines += ["wait 20", "ca_state"]
    lines += ["echo CATEST end", "quit"]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def check(log):
    text = open(log, errors="replace").read()
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    def states(part):
        return [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", part)]

    parts = re.split(r"CATEST (backoff|walk|wait|end)", text)
    first = states(parts[0])
    heals = [(int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))) for m in
             re.finditer(r"Cat Arena: medkit (\d+) healed (\d+) \((\d+) -> (\d+)\) t=(\d+)", text)]
    ok("Cat Arena: 85 sounds registered" in text, "sounds registered (with the medkit beep)")
    ok("Cat Arena: 38 medkits" in text, "38 medkits on Reactor Bastion (N10's medkit spots)")
    ok(first and int(first[0]["health"]) == 100, "spawn health %s (N10 100)" % (first[0]["health"] if first else "?"))
    every = states(text)   # server prints arrive after the local echo lines: whole log, in order
    ok(len(every) >= 2 and int(every[1]["health"]) == 100 and (not heals or heals[0][2] == 40), "standing on a medkit at full health: not taken (health %s)" % (every[1]["health"] if len(every) > 1 else "?"))
    walk = parts[parts.index("walk") + 1] if "walk" in parts else ""
    ws = states(walk)
    ok(len(heals) >= 1 and heals[0][1:4] == (40, 40, 80) and ws and int(ws[-1]["health"]) == 80 and "Cat Arena: sound beep_medkit" in walk,
       "walked over it at 40 health: %s, health then %s, medkit beep" % (heals[0][1:4] if heals else "-", ws[-1]["health"] if ws else "?"))
    again = [h for h in heals[1:] if heals and h[0] == heals[0][0]]
    ok(again and again[0][1:4] == (20, 80, 100) and 13000 <= again[0][4] - heals[0][4] <= 13100,
       "back after %s ms (N10 13 s), healed again 80 -> 100" % (again[0][4] - heals[0][4] if again else "-"))
    ok(len(heals) == 2, "%d heals in all" % len(heals))
    ok("ERROR" not in text and "Signal caught" not in text, "no errors")
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2], sys.argv[3])
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
