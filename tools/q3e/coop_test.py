#!/usr/bin/env python3
"""N10-Q3E stage E: N10's co-op rules on the dedicated server, with three scripted players.

  coop_test.py write <coop.cfg>
  coop_test.py check <server log>
  coop_test.py view <view.cfg>      pictures of the co-op HUD from the client (looked at, not checked)
  coop_test.py marines <view.cfg>   pictures of the other players as Marine Cats (standing, walking,
                                    firing, hurt, down; looked at, not checked)

Run: CatArena-server +set dedicated 1 +set ca_waves 0 +set ca_log 1 +map ca_test +exec coop.cfg
View: CatArena +set com_maxfps 60 +set ca_waves 0 +set ca_log 1 +devmap ca_test +wait 150 +exec view.cfg
(client waits count frames: 60 fps makes "wait 60" about one second)

The scripted players are bot slots without bot AI (ca_addplayer); ca_input sets their buttons,
ca_moveplayer places them, ca_hurt damages them through G_Damage, ca_coopstate prints every
player's health, downed flag, status line (N10 coopReviveStatus) and grace. Server frames are 50 ms;
"wait 20" is about one second. The checks use the server times in the log, not the waits.
"""
import re
import sys

USE = 4   # BUTTON_USE_HOLDABLE (+button2, bound to F)
A, B, C = 0, 1, 2
STEPS = []


def write(path):
    L = ["ca_addplayer Alpha", "ca_addplayer Bravo", "ca_addplayer Charlie", "wait 20"]

    def mark(label):
        L.extend(["echo CASTEP %s" % label, "ca_coopstate"])

    def place(who, x, y, yaw=0):
        L.append("ca_moveplayer %d %d %d 40 %d" % (who, x, y, yaw))

    place(A, -700, -300); place(B, -640, -300, 180); place(C, 0, 300)
    L.append("wait 20"); mark("start")
    L.extend(["ca_hurt %d 200" % B, "wait 4"]); mark("bravo down")
    L.extend(["ca_hurt %d 50" % B, "wait 4"]); mark("downed hurt")
    L.extend(["ca_input %d 0 0 0 %d" % (C, USE), "wait 30"]); mark("charlie holds far away")
    L.append("ca_input %d 0 0 0 0" % C)
    L.append("ca_input %d 0 0 0 %d" % (A, USE)); mark("alpha holds")
    L.append("wait 40"); mark("half way")
    L.append("wait 50"); mark("after 4.5 s")
    L.append("ca_input %d 0 0 0 0" % A)
    L.extend(["ca_hurt %d 30" % B, "wait 2"]); mark("hurt in grace")
    L.extend(["wait 50", "ca_hurt %d 30" % B, "wait 2"]); mark("hurt after grace")
    # distance: 3.5 m (112 u) no, 3.0 m (96 u) yes
    L.extend(["ca_hurt %d 200" % B, "wait 2"])
    place(A, -640 - 112, -300)
    L.extend(["ca_input %d 0 0 0 %d" % (A, USE), "wait 20"]); mark("holding 3.5 m away")
    place(A, -640 - 96, -300)
    L.append("wait 20"); mark("holding 3.0 m away")
    L.extend(["ca_input %d 0 0 0 0" % A, "wait 4"])
    # line of sight: the low cover block (x -96..96, y -160..-128, 48 u high) between them, 2.5 m apart
    place(B, 0, -100); place(A, 0, -190, 90)
    L.append("wait 4")
    L.extend(["ca_input %d 0 0 0 %d" % (A, USE), "wait 20"]); mark("holding behind cover")
    L.extend(["ca_input %d 0 0 0 0" % A, "wait 4"])
    # squad wipe: everybody down
    L.extend(["ca_hurt %d 200" % A, "ca_hurt %d 200" % C, "wait 4"]); mark("all down")
    L.append("wait 30"); mark("wiping")
    L.append("wait 40"); mark("after the wipe")
    # left alone while down: gets up
    # (after the 2.5 s grace the restart gives, N10 finish_respawn)
    L.extend(["wait 40", "ca_hurt %d 200" % A, "wait 4", "ca_removeplayer %d" % B, "ca_removeplayer %d" % C, "wait 10"]); mark("alone after leaving")
    # solo: dies, no downed state
    L.extend(["wait 60", "ca_hurt %d 200" % A, "wait 4"]); mark("solo death")
    L.extend(["echo CASTEP end", "quit"])
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")


VIEW = """wait 60
ca_place -700 -300 40 0
ca_addplayer Bravo
ca_addplayer Charlie
wait 60
ca_moveplayer 1 -540 -330 40 180
ca_moveplayer 2 -620 -240 40 180
wait 60
ca_hurt 2 200
ca_hurt 1 35
wait 60
screenshotJPEG coop_hold_f
+button2
wait 120
screenshotJPEG coop_reviving
wait 180
-button2
wait 30
ca_coopstate
screenshotJPEG coop_revived
ca_hurt 0 200
wait 60
screenshotJPEG coop_down
ca_moveplayer 1 -640 -290 40 180
wait 10
ca_input 1 0 0 0 4
wait 120
screenshotJPEG coop_being_revived
wait 150
ca_input 1 0 0 0 0
wait 150
ca_hurt 1 200
ca_hurt 2 200
ca_hurt 0 200
wait 40
screenshotJPEG coop_wiped
wait 200
ca_coopstate
wait 10
quit
"""


MARINES = """wait 60
ca_place -700 -300 40 0
cg_drawGun 0
ca_addplayer Bravo
ca_addplayer Charlie
wait 60
ca_moveplayer 1 -590 -320 40 180
ca_moveplayer 2 -500 -190 40 270
wait 90
screenshotJPEG marines_stand
ca_input 2 127 0 0 0
wait 25
screenshotJPEG marines_walk
ca_input 2 0 0 0 0
ca_input 1 0 0 0 1
wait 3
screenshotJPEG marines_fire
ca_input 1 0 0 0 0
wait 60
ca_moveplayer 2 -600 -250 40 200
wait 30
ca_hurt 2 200
wait 60
screenshotJPEG marines_down
ca_place -470 -260 40 180
wait 60
screenshotJPEG marines_back
wait 10
quit
"""


def view(path, text=None):
    with open(path, "w", newline="\n") as f:
        f.write(text or VIEW)


def check(path):
    text = open(path, errors="replace").read()
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    steps = {}
    for part in text.split("CASTEP ")[1:]:
        label, _, body = part.partition("\n")
        rows = {}
        for m in re.finditer(r"CA_COOP (.*)", body):
            d = dict(kv.split("=", 1) for kv in m.group(1).split())
            rows[int(d["client"])] = {k: (v if k in ("name", "pos") else int(v)) for k, v in d.items()}
        steps[label.strip()] = rows

    def st(label, who):
        return steps.get(label, {}).get(who, {})

    def times(pattern):
        return [int(t) for t in re.findall(pattern + r" t=(\d+)", text)]

    ok(len(re.findall(r"Cat Arena: player \d \w+ joined", text)) == 3, "three scripted players joined")
    ok(all(st("start", w).get("health") == 100 and st("start", w).get("status") == 0 for w in (A, B, C)), "all three at 100 health, no status line")
    b = st("bravo down", B)
    ok(b.get("downed") == 1 and b.get("health") == 0 and b.get("dead") == 0 and b.get("status") == 4,
       "Bravo at 0 health is down, not dead: YOU ARE DOWN (%s)" % b)
    a = st("bravo down", A)
    ok(a.get("status") == 1 and a.get("subject") == B and st("bravo down", C).get("status") == 0,
       "Alpha 1.9 m away sees HOLD F TO REVIVE Bravo; Charlie far away sees nothing")
    ok(st("downed hurt", B).get("downed") == 1 and st("downed hurt", B).get("health") == 0, "a downed player takes no damage and does not die")
    ok(st("charlie holds far away", B).get("downed") == 1 and st("charlie holds far away", C).get("status") == 0, "holding F far away revives nobody")
    h = st("half way", A)
    ok(h.get("status") == 2 and h.get("subject") == B and 30 <= h.get("pct", 0) <= 70 and st("half way", B).get("status") == 3,
       "half way: Alpha REVIVING Bravo %s%%, Bravo BEING REVIVED" % h.get("pct"))
    t_hold = st("alpha holds", A).get("t", 0)
    revived = [int(t) for t in re.findall(r"Cat Arena: Bravo revived by Alpha t=(\d+)", text)]
    r = st("after 4.5 s", B)
    ok(revived and 3950 <= revived[0] - t_hold <= 4100 and r.get("health") == 45 and r.get("downed") == 0 and r.get("grace") == 1,
       "revived %s ms after Alpha began holding F (N10 4 s), back with %s health and 2 s of grace" % (revived[0] - t_hold if revived else "-", r.get("health")))
    ok(st("hurt in grace", B).get("health") == 45, "no damage during the grace (%s)" % st("hurt in grace", B).get("health"))
    ok(st("hurt after grace", B).get("health") == 15, "damage again after it (%s)" % st("hurt after grace", B).get("health"))
    ok(st("holding 3.5 m away", A).get("status") == 0 and st("holding 3.5 m away", B).get("status") == 4, "3.5 m away: out of reach (N10 3.2 m)")
    ok(st("holding 3.0 m away", A).get("status") == 2 and st("holding 3.0 m away", A).get("pct", 0) > 0, "3.0 m away: reviving")
    ok(st("holding behind cover", A).get("status") == 0 and st("holding behind cover", B).get("downed") == 1, "2.5 m away behind a cover block: no line of sight, no revive")
    ok(all(st("all down", w).get("downed") == 1 for w in (A, B, C)) and st("all down", A).get("status") in (4, 5), "all three down")
    ok(all(st("wiping", w).get("status") == 5 for w in (A, B, C)), "SQUAD ELIMINATED - RESTARTING ROUND for everybody")
    downs = [int(t) for t in re.findall(r"Cat Arena: \w+ down t=(\d+)", text)]
    wiped = times("Cat Arena: squad wiped, round restarts")
    t_all = max(t for t in downs if not wiped or t <= wiped[0]) if downs else 0
    after = steps.get("after the wipe", {})
    ok(wiped and 2700 <= wiped[0] - t_all <= 2800 and all(after.get(w, {}).get("health") == 100 and after.get(w, {}).get("downed") == 0
                                                          and after.get(w, {}).get("grace") == 1 for w in (A, B, C))
       and "round restarts at wave 1" in text,
       "squad wiped %s ms after the last went down (N10 2.7 s): everybody back at 100 with 2.5 s grace, wave 1" % (wiped[0] - t_all if wiped else "-"))
    al = st("alone after leaving", A)
    ok("Alpha alone and down: gets up" in text and al.get("health") == 100 and al.get("downed") == 0 and len(steps.get("alone after leaving", {})) == 1,
       "Alpha left alone while down gets up with 100 health")
    so = st("solo death", A)
    ok(so.get("dead") == 1 and so.get("downed") == 0, "alone, a player dies (no downed state)")
    ok("ERROR" not in text and "Signal caught" not in text, "no errors")
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2])
    elif sys.argv[1] == "view":
        view(sys.argv[2])
    elif sys.argv[1] == "marines":
        view(sys.argv[2], MARINES)
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
