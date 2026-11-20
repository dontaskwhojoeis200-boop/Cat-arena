#!/usr/bin/env python3
"""N10-Q3E stage C: writes the scripted gameplay test (a console script) and evaluates its log.

  gameplay_test.py write <file.cfg>     the script: walk, sprint, slide, fight a wave without and
                                        with god mode (ca_aimnearest + attack), reload
  gameplay_test.py check <console log>  prints the measured values and PASS / FAIL per check

Run the client with:  +set ca_waves 0 +devmap ca_test +wait 150 +exec <file.cfg>   (cheats are needed for
ca_state, ca_aimnearest and god; waves are held until the movement part is done). The checks use the
CA_STATE lines printed by the game. Fire is released for a frame between bursts, as a player does:
Quake III does not fire after a respawn until the button has been released once.
"""
import re
import sys


def write(path):
    lines = ["setviewpos -700 -300 40 0", "wait 20", "echo CATEST movement", "ca_state", "+forward", "wait 80", "echo CATEST walking", "ca_state",
             "+button5", "wait 80", "echo CATEST sprinting", "ca_state", "+movedown", "wait 4", "echo CATEST slide started", "ca_state",
             "wait 40", "echo CATEST sliding", "ca_state", "wait 160", "echo CATEST after slide", "ca_state",
             "-movedown", "-button5", "-forward", "wait 40",
             "+attack", "wait 60", "-attack", "wait 5", "echo CATEST reload", "ca_state", "+button6", "wait 4", "-button6", "wait 4",
             "ca_state", "wait 250", "ca_state", "set ca_waves 1", "wait 20", "echo CATEST combat without god mode"]
    for i in range(70):
        lines += ["ca_aimnearest", "+attack", "wait 6", "-attack", "wait 1"]
        if i % 10 == 9:
            lines += ["ca_state"]
    # respawn if the fiends won (click), let a squad-wipe reset finish, then god mode
    lines += ["screenshotJPEG combat", "wait 250", "+attack", "wait 3", "-attack", "wait 400", "echo CATEST god mode, clear the wave", "god"]
    for i in range(500):
        lines += ["ca_aimnearest", "+attack", "wait 6", "-attack", "wait 1"]
        if i % 25 == 24:
            lines += ["ca_state"]
        if i == 30:
            lines += ["screenshotJPEG combat2"]
    lines += ["echo CATEST end", "quit"]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def check(path):
    """CA_STATE lines reach the log in the order the script asked for them (the echo labels do not:
    they print locally at once), so the states are matched by position: 0 start, 1 walking,
    2 sprinting, 3 slide started, 4 sliding, 5 after slide, 6-8 reload, 9-15 combat without god
    mode, then combat in god mode."""
    text = open(path, errors="replace").read()
    states = [dict(kv.split("=", 1) for kv in m.group(1).split()) for m in re.finditer(r"CA_STATE (.*)", text)]
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    if len(states) < 16:
        ok(False, "%d CA_STATE lines (expected at least 16)" % len(states))
        print("%d FAILED" % fails)
        return False
    walk, sprint, start, sliding, after = states[1], states[2], states[3], states[4], states[5]
    w, s = float(walk["hspeed"]), float(sprint["hspeed"])
    ok(300 <= w <= 330, "walk speed %.0f u/s (stock Quake III 320)" % w)
    ok(450 <= s <= 490, "sprint speed %.0f u/s (320 x 1.5 = 480)" % s)
    ok(start["sliding"] == "1" and start["ducked"] == "1" and float(start["hspeed"]) > 440,
       "slide started: %s u/s, crouched, %s ms left (boost x1.085 then slide friction)" % (start["hspeed"], start["slide_ms"]))
    ok(sliding["sliding"] == "1" and float(sliding["hspeed"]) < float(start["hspeed"]),
       "still sliding later: %s u/s, %s ms left" % (sliding["hspeed"], sliding["slide_ms"]))
    ok(after["sliding"] == "0", "slide over: %s u/s while crouched, cooldown %s ms" % (after["hspeed"], after["cooldown_ms"]))
    r0, r1, r2 = states[6], states[7], states[8]
    ok(int(r0["clip"]) < 50 and r1["reloading"] == "1" and r2["clip"] == "50" and r2["reloading"] == "0",
       "reload: clip %s, R -> reloading (%s ms), then clip %s" % (r0["clip"], r1["wtime"], r2["clip"]))
    combat = states[9:16]
    hp = [int(c["health"]) for c in combat]
    ok(min(hp) < 110, "fiends hurt the player (no god mode): health %s" % hp)
    score = max(int(st["score"]) for st in states)
    ok(score >= 10, "nailgun kills counted: score %d" % score)
    ok("godmode ON" in text, "god mode on for the wave-clearing part")
    ok("wave 1 cleared" in text and "wave 2, 13 enemies" in text, "wave 1 cleared, wave 2 (13 enemies) began")
    if "squad wiped" in text:
        after = text.split("squad wiped", 1)[1]
        ok(re.search(r"Cat Arena: wave (\d+), ", after) is not None and re.search(r"Cat Arena: wave (\d+), ", after).group(1) == "1",
           "after a squad wipe the next wave announced is wave 1")
    print("waves:", ", ".join(re.findall(r"Cat Arena: (wave \d+(?:, \d+ enemies| cleared)|squad wiped)", text)))
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2])
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
