#!/usr/bin/env python3
"""N10-Q3E stage D part 3: scripted test of the 16 Cat Arena weapons in the client (console script)
and the evaluation of its log.

  weapons_test.py write <file.cfg>     the script
  weapons_test.py check <console log>  measured values and PASS / FAIL per check

Run the client with:
  +set ca_waves 0 +set ca_loadout "0 1" +devmap ca_test +wait 150 +exec <file.cfg>
(cheats for ca_state / ca_give / ca_place / ca_aimnearest / god). The script:
  1. two slots: NAILGUN and SHOTGUN with their own magazines, switch (Q = +button7) and back;
  2. shotgun recoil on the ground (pushed back) and in the air (lifted: N10 mid-air double jump);
  3. every weapon in turn (ca_give 0..15): magazine size, then the fire rate measured between two
     states while the trigger is held (N10 delay), with a screenshot of each viewmodel;
  4. a rocket fired at the player's own feet: N10 self blast damage (38 x 1.15) and launch;
  5. waves on, god mode: kills with the railgun, then whatever each frag rerolls into; every frag
     replaces the weapon in hand (N10 rerollOne).
CA_STATE lines arrive in the order the script asked for them, so each is matched to its label by
position (the echo lines print locally at once and are not used for matching).
"""
import re
import sys

N10 = [  # name, delay ms, clip, kind
    ("NAILGUN", 90, 50, "hitscan"), ("SHOTGUN", 720, 8, "hitscan"), ("GRENADE LAUNCHER", 780, 6, "grenade"),
    ("RAILGUN", 980, 7, "hitscan"), ("ROCKET LAUNCHER", 860, 6, "rocket"), ("SUPER NAILGUN", 130, 36, "hitscan"),
    ("THUNDERBOLT", 75, 75, "hitscan"), ("SUPER SHOTGUN", 880, 12, "hitscan"), ("PLASMA REPEATER", 190, 48, "hitscan"),
    ("TRIPLE SPIKE", 340, 36, "hitscan"), ("RIFT CANNON", 1150, 5, "rift"), ("IRON CHAINGUN", 65, 100, "hitscan"),
    ("HAND CANNON", 820, 7, "hitscan"), ("HELLFIRE CARBINE", 280, 28, "hitscan"), ("SAWBLADE DRIVER", 380, 22, "hitscan"),
    ("QUAD SHOTGUN", 1150, 8, "hitscan"),
]
START = "ca_place -700 -300 40 0"


def plan():
    """(console lines, state labels in order)."""
    lines, labels = [], []

    def state(label):
        lines.append("ca_state")
        labels.append(label)

    lines += [START, "wait 20"]
    state("start")
    lines += ["+attack", "wait 30", "-attack", "wait 10"]
    state("nailgun fired")
    lines += ["+button7", "wait 2", "-button7", "wait 20"]
    state("switched")
    lines += ["+button7", "wait 2", "-button7", "wait 20"]
    state("switched back")
    # shotgun recoil: on the ground, then at the top of a jump
    lines += ["+button7", "wait 2", "-button7", "wait 30", START, "wait 80"]
    state("before recoil")
    lines += ["+attack", "wait 1", "-attack", "wait 2"]
    state("recoil ground")
    lines += ["wait 80", START, "wait 100", "+moveup", "wait 3", "-moveup", "wait 22", "+attack", "wait 1", "-attack", "wait 1"]
    state("recoil air")
    lines += ["wait 120", "god"]
    for i, (name, delay, clip, kind) in enumerate(N10):
        lines += [START, "ca_give %d" % i, "wait 15"]
        state("give %d" % i)
        lines += ["+attack", "wait 12"]
        state("firing a %d" % i)
        lines += ["wait 300"]
        state("firing b %d" % i)
        lines += ["screenshotJPEG weapon%02d" % i, "wait 2", "-attack", "wait 40"]
    # rocket at the feet, no god mode
    lines += ["god", "ca_give 4", "wait 60", "ca_place -700 -300 40 0 89", "wait 100"]
    state("before rocket jump")
    lines += ["+attack", "wait 1", "-attack", "wait 12"]
    state("after rocket jump")
    lines += ["wait 150", START, "ca_give 3", "god", "set ca_waves 1", "wait 40"]
    state("waves on")
    for i in range(400):
        lines += ["ca_aimnearest", "+attack", "wait 6", "-attack", "wait 1"]
        if i % 5 == 4:
            state("combat %d" % i)
        if i in (40, 120):
            lines += ["screenshotJPEG combat_%d" % i]
    lines += ["wait 30", "echo CATEST end", "quit"]
    return lines, labels


def write(path):
    lines, _ = plan()
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def check(path):
    text = open(path, errors="replace").read()
    states = [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", text)]
    _, labels = plan()
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    if len(states) < len(labels):
        ok(False, "%d CA_STATE lines (expected %d)" % (len(states), len(labels)))
        print("%d FAILED" % fails)
        return False
    st = dict(zip(labels, states))
    n = lambda s, k: int(float(s[k]))

    s0, s1, s2, s3 = st["start"], st["nailgun fired"], st["switched"], st["switched back"]
    ok(n(s0, "w0") == 0 and n(s0, "w1") == 1 and n(s0, "inhand") == 0 and n(s0, "clip") == 50 and n(s0, "clip2") == 8,
       "loadout NAILGUN + SHOTGUN, magazines 50 / 8 (ca_loadout \"0 1\")")
    ok(n(s1, "clip") < 50, "NAILGUN fired: magazine %s" % s1["clip"])
    ok(n(s2, "slot") == 1 and n(s2, "inhand") == 1 and n(s2, "clip") == 8 and n(s2, "clip2") == n(s1, "clip"),
       "switch (Q): SHOTGUN in hand with 8, NAILGUN magazine %s kept" % s2["clip2"])
    ok(n(s3, "slot") == 0 and n(s3, "clip") == n(s1, "clip") and n(s3, "clip2") == 8, "switch back: NAILGUN magazine %s" % s3["clip"])

    rg, ra = st["recoil ground"], st["recoil air"]
    ok(n(st["before recoil"], "inhand") == 1 and float(st["before recoil"]["vz"]) == 0 and float(rg["hspeed"]) > 60 and float(rg["vz"]) <= 0,
       "SHOTGUN recoil on the ground: pushed back at %s u/s (N10 4.7 m/s = 150 u/s, then friction)" % rg["hspeed"])
    ok(float(ra["vz"]) > 150, "SHOTGUN in the air: lifted to vz %s u/s (N10 at least 7.3 m/s = 234 u/s, minus gravity since)" % ra["vz"])

    for i, (name, delay, clip, kind) in enumerate(N10):
        g, a, b = st["give %d" % i], st["firing a %d" % i], st["firing b %d" % i]
        shots = n(a, "clip") - n(b, "clip")
        dt = n(b, "t") - n(a, "t")
        expect = dt / delay
        good_clip = n(g, "inhand") == i and n(g, "clip") == clip
        reloaded = n(b, "reloading") == 1 or n(b, "clip") > n(a, "clip")
        if reloaded:
            ok(good_clip, "%-16s magazine %d; rate not measured (magazine emptied in the window)" % (name, clip))
        else:
            ok(good_clip and abs(shots - expect) <= 1.0 + 0.1 * expect,
               "%-16s magazine %d, %d shots in %d ms (N10 delay %d ms -> %.1f)" % (name, n(g, "clip"), shots, dt, delay, expect))

    b, a = st["before rocket jump"], st["after rocket jump"]
    drop = n(b, "health") - n(a, "health")
    ok(n(b, "inhand") == 4 and float(b["vz"]) == 0 and 38 <= drop <= 45 and float(a["vz"]) > 250,
       "rocket at the feet: %d damage (N10 38 x 1.15 = 44 at the centre), launched at vz %s u/s" % (drop, a["vz"]))

    combat = [s for l, s in zip(labels, states) if l.startswith("combat")]
    kills = re.findall(r"Cat Arena: frag reroll (.+?) -> (.+)", text)
    score = max(n(s, "score") for s in combat)
    ok(len(kills) >= 10 and all(x.strip() != y.strip() for x, y in kills),
       "%d frags, each rerolled the weapon in hand into a different one" % len(kills))
    weapons_used = {x.strip() for x, _ in kills}
    ok(len(weapons_used) >= 6, "frags scored with %d different weapons: %s" % (len(weapons_used), ", ".join(sorted(weapons_used))))
    ok(score >= len(kills) - 1, "score %d" % score)
    print("waves:", ", ".join(re.findall(r"Cat Arena: (wave \d+(?:, \d+ enemies| cleared)|squad wiped)", text)))
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2])
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
