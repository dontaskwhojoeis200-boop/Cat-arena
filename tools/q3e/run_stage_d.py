#!/usr/bin/env python3
"""N10-Q3E stage D review: every stage D test on one package, in one run (Linux).

  run_stage_d.py <repo root> <package dir> <out dir> [only ...]

The package is what tools/q3e/package.py writes (CatArena, CatArena-server, catarena/pak0.pk3).
Needs a display and a Vulkan driver for the client (lavapipe on Xvfb here) and ffmpeg for the
sound check. Each test gets its own home folder in <out dir>; logs, checks and pictures stay there,
and <out dir>/summary.txt lists the verdict of every test. Takes about three hours on two cores.

Tests (names usable as [only]):
  selftest-<map>   dedicated server ca_selftest on ca_test and the four N10 maps
  gameplay         stage C scripted run (movement, slide, reload, fiends, waves)
  weapons          16 weapons, frag rerolls, blasts, combat
  lineup           the 20 enemy types lined up (pictures, wind-up)
  waves-<map>      waves on each N10 map (spawns, kills, attack kinds, flyers, damage)
  map-<map>        floors, ramps and jump pads of each N10 map against the N10 data
  sound            output level, soundtrack level, every sound rule (SDL disk audio driver)
  medkit           medkits on Reactor Bastion
  views-<map>      pictures from six spawn spots of each N10 map (looked at, not checked)
"""
import json
import math
import os
import shutil
import subprocess
import sys
import time

MAPS = [("reactor-bastion", "ca_reactor"), ("helios-crucible", "ca_helios"), ("bastion-keep", "ca_bastion"), ("q3-gallery", "ca_arsenal")]


def main(root, pkg, out, only):
    tools = os.path.join(root, "tools", "q3e")
    os.makedirs(out, exist_ok=True)
    results = []

    def py(*args, capture=None):
        r = subprocess.run([sys.executable, os.path.join(tools, args[0])] + list(args[1:]), capture_output=True, text=True)
        if capture:
            with open(capture, "w") as f:
                f.write(r.stdout + r.stderr)
        return r

    def home(name):
        h = os.path.join(out, name)
        shutil.rmtree(h, ignore_errors=True)
        os.makedirs(os.path.join(h, "catarena"))
        return h

    def client(name, h, args, timeout=1800, env=None):
        log = os.path.join(out, name + ".log")
        e = dict(os.environ, SDL_AUDIODRIVER="dummy")
        e.update(env or {})
        with open(log, "w") as f:
            try:
                subprocess.run([os.path.join(pkg, "CatArena"), "+set", "fs_homepath", h] + args, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, env=e, cwd=pkg)
            except subprocess.TimeoutExpired:
                f.write("\nrun_stage_d: TIMEOUT after %d s\n" % timeout)
        return log

    def verdict(name, r, checks):
        text = open(checks).read() if os.path.exists(checks) else ""
        last = text.strip().splitlines()[-1] if text.strip() else "no output"
        ok = r.returncode == 0 and "ALL PASS" in last
        results.append((name, ok, last))
        print("%-22s %s  %s" % (name, "PASS" if ok else "FAIL", last), flush=True)

    def want(name):
        return not only or any(name == o or name.startswith(o + "-") or o == name.split("-")[0] for o in only)

    start = time.time()
    for m in ["ca_test"] + [q for _, q in MAPS]:
        name = "selftest-" + m
        if not want(name):
            continue
        h = home(name)
        log = os.path.join(out, name + ".log")
        with open(log, "w") as f:
            r = subprocess.run([os.path.join(pkg, "CatArena-server"), "+set", "fs_homepath", h, "+set", "dedicated", "1", "+map", m, "+ca_selftest", "+quit"],
                               stdout=f, stderr=subprocess.STDOUT, timeout=600, cwd=pkg)
        text = open(log, errors="replace").read()
        ok = "ca_selftest: ALL PASS" in text
        fails = [l for l in text.splitlines() if l.startswith("FAIL")]
        results.append((name, ok, "ALL PASS" if ok else "; ".join(fails[:3]) or "no verdict"))
        print("%-22s %s" % (name, "PASS" if ok else "FAIL"), flush=True)

    if want("gameplay"):
        h = home("gameplay")
        py("gameplay_test.py", "write", os.path.join(h, "catarena", "catest.cfg"))
        log = client("gameplay", h, ["+set", "ca_waves", "0", "+set", "ca_loadout", "0 1", "+devmap", "ca_test", "+wait", "150", "+exec", "catest.cfg"])
        verdict("gameplay", py("gameplay_test.py", "check", log, capture=log + ".checks"), log + ".checks")
    if want("weapons"):
        h = home("weapons")
        py("weapons_test.py", "write", os.path.join(h, "catarena", "weapons.cfg"))
        log = client("weapons", h, ["+set", "ca_waves", "0", "+set", "ca_loadout", "0 1", "+devmap", "ca_test", "+wait", "150", "+exec", "weapons.cfg"])
        verdict("weapons", py("weapons_test.py", "check", log, capture=log + ".checks"), log + ".checks")
    lineup_log = None
    if want("lineup"):
        h = home("lineup")
        py("enemies_test.py", "write", os.path.join(h, "catarena", "lineup.cfg"), os.devnull)
        lineup_log = client("lineup", h, ["+set", "ca_waves", "0", "+set", "ca_log", "1", "+devmap", "ca_test", "+wait", "150", "+exec", "lineup.cfg"])
    for mid, m in MAPS:
        name = "waves-" + m
        if not want(name):
            continue
        h = home(name)
        cfg = os.path.join(h, "catarena", "waves.cfg")
        # two far-apart spawn spots open to the sky on every map; The Arsenal's waves stall later on
        # (a flyer can hover out of reach), so god mode goes off earlier there
        py("enemies_test.py", "write", os.devnull, cfg, root, mid, "250" if m == "ca_arsenal" else "700")
        loadout = ["+set", "ca_loadout", "11 0"]
        log = client(name, h, ["+set", "ca_waves", "0", "+set", "ca_log", "1"] + loadout + ["+devmap", m, "+wait", "150", "+exec", "waves.cfg"])
        lineup_arg = lineup_log if (m == "ca_reactor" and lineup_log) else "-"
        verdict(name, py("enemies_test.py", "check", lineup_arg, log, capture=log + ".checks"), log + ".checks")
    for mid, m in MAPS:
        name = "map-" + m
        if not want(name):
            continue
        h = home(name)
        py("map_test.py", "write", root, mid, m, os.path.join(h, "catarena", "maptest.cfg"))
        log = client(name, h, ["+set", "ca_waves", "0", "+devmap", m, "+wait", "150", "+exec", "maptest.cfg"])
        r = py("map_test.py", "check", root, mid, m, log, capture=log + ".checks")
        verdict(name, r, log + ".checks")
    if want("sound"):
        logs = []
        for run, extra in (("levels", ["+set", "ca_music", "0", "+set", "ca_waves", "0", "+set", "ca_log", "1", "+set", "s_show", "1"]),
                           ("music", ["+set", "ca_music", "1", "+set", "ca_waves", "0"]),
                           ("sounds", ["+set", "ca_music", "0", "+set", "ca_waves", "0", "+set", "ca_log", "1", "+set", "s_show", "1"])):
            h = home("sound-" + run)
            py("sound_test.py", "write", os.path.join(h, "catarena"))
            raw = os.path.join(out, "sound-%s.raw" % run)
            if os.path.exists(raw):
                os.remove(raw)
            log = client("sound-" + run, h, extra + ["+devmap", "ca_test", "+wait", "150", "+exec", run + ".cfg"], timeout=1200,
                         env={"SDL_AUDIODRIVER": "disk", "SDL_DISKAUDIOFILE": raw})
            logs += [log, raw]
        log = os.path.join(out, "sound.log")
        verdict("sound", py("sound_test.py", "check", root, *logs, capture=log + ".checks"), log + ".checks")
    if want("medkit"):
        h = home("medkit")
        py("medkit_test.py", "write", root, os.path.join(h, "catarena", "medkit.cfg"))
        log = client("medkit", h, ["+set", "ca_waves", "0", "+set", "ca_log", "1", "+devmap", "ca_reactor", "+wait", "150", "+exec", "medkit.cfg"])
        verdict("medkit", py("medkit_test.py", "check", log, capture=log + ".checks"), log + ".checks")
    for mid, m in MAPS:
        name = "views-" + m
        if not want(name):
            continue
        h = home(name)
        c = json.load(open(os.path.join(root, "assets", "n1", "maps", mid, "map.json")))["compiled"]
        cx, cy = c["meta"]["center"]
        sp = c["spawns"]
        lines = ["wait 30", "god", "cg_drawFPS 0"]
        for i, s in enumerate(sp[::max(1, len(sp) // 6)][:6]):
            lines += ["ca_place %.1f %.1f %.1f %.1f 5" % ((s["x"] - cx) * 32, (s["y"] - cy) * 32, s["z"] * 32 + 28, math.degrees(s["a"])), "wait 40", "screenshotJPEG view_%s_%d" % (m, i)]
        lines += ["wait 5", "quit"]
        with open(os.path.join(h, "catarena", "view.cfg"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        log = client(name, h, ["+set", "ca_waves", "0", "+devmap", m, "+wait", "150", "+exec", "view.cfg"], timeout=600)
        shots = os.path.join(h, "catarena", "screenshots")
        n = len(os.listdir(shots)) if os.path.isdir(shots) else 0
        results.append((name, n == 6, "%d pictures" % n))
        print("%-22s %s  %d pictures" % (name, "PASS" if n == 6 else "FAIL", n), flush=True)

    with open(os.path.join(out, "summary.txt"), "w") as f:
        for name, ok, last in results:
            f.write("%-22s %s  %s\n" % (name, "PASS" if ok else "FAIL", last))
        f.write("%d of %d passed in %.0f min\n" % (sum(ok for _, ok, _ in results), len(results), (time.time() - start) / 60))
    print(open(os.path.join(out, "summary.txt")).read().splitlines()[-1])
    return all(ok for _, ok, _ in results)


if __name__ == "__main__":
    sys.exit(0 if main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]), sys.argv[4:]) else 1)
