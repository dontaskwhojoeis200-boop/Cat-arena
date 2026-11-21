#!/usr/bin/env python3
"""N10-Q3E stage D part 6: scripted test of Cat Arena's sounds on Quake3e.

  sound_test.py write <dir>                                     levels.cfg, music.cfg, sounds.cfg
  sound_test.py check <repo root> <levels log> <levels raw> <music log> <music raw> <sounds log> <sounds raw>

The client runs with SDL's "disk" audio driver, which writes what Quake III mixes to a file
(SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<raw>: 16-bit stereo at 44.1 kHz), so the test checks the
sound that comes out, not only the calls. Three runs on the test arena (BUILD_INSTRUCTIONS.md):
  levels  +set ca_music 0 +set ca_waves 0 +set ca_log 1 +set s_show 1: single shots of the HAND
          CANNON and nothing else; the loudest output sample must be the baked peak of what played
          x Quake III's local-sound scale (127 x int(255 s_volume) / 65536) within 3 %: N10's level
          (sample x gain x bus x master) arrives at the output.
  music   +set ca_music 1 +set ca_waves 0: the soundtrack alone; its loudness must be the Ogg's x
          s_musicvolume (0.322 = N10 music 46 % x master 70 %) within 15 %.
  sounds  +set ca_music 0 +set ca_waves 0 +set ca_log 1 +set s_show 1: jumps, landings, steps,
          sprint, slide, slot switch, reload, the 16 guns, rocket / grenade / rift blasts, the 20
          enemy types lined up (idle and wind-up voices) and killed (hurt and death voices), then a
          wave with god mode off for a while (damage beeps). Checks every N10 sound rule that fired and the
          enemy-voice limits, that the engine started only Cat Arena sounds, and that the output is
          not silent and not clipped.
"""
import os
import re
import subprocess
import sys

LEVEL_GUN = 12   # HAND CANNON (heavy: level .50)


def write(d):
    levels = ["wait 60", "god", "wait 60", "ca_give %d" % LEVEL_GUN, "wait 40"]
    for _ in range(3):
        levels += ["+attack", "wait 2", "-attack", "wait 80"]
    levels += ["wait 40", "echo CATEST end", "quit"]
    music = ["wait 400", "s_info", "echo CATEST end", "quit"]
    s = ["wait 40", "god", "ca_place -700 -300 40 0", "wait 60",
         "+moveup", "wait 3", "-moveup", "wait 80",                                  # jump, landing
         "+forward", "wait 60", "-forward", "wait 20",                               # steps
         "+forward", "+button5", "wait 60", "+movedown", "wait 8", "-movedown", "wait 40", "-button5", "-forward", "wait 40",   # sprint, slide
         "ca_place -700 -300 40 0", "wait 20",
         "+button7", "wait 4", "-button7", "wait 30",                                # slot switch
         "ca_give 0", "wait 10", "+attack", "wait 15", "-attack", "+button6", "wait 4", "-button6", "wait 120"]   # reload
    for i in range(16):
        s += ["ca_give %d" % i, "wait 15", "+attack", "wait 80", "-attack", "wait 10"]
    s += ["ca_place -700 -300 40 0 75", "wait 20"]                                  # blasts at the player's feet
    for w in (4, 2, 10):
        s += ["ca_give %d" % w, "wait 15", "+attack", "wait 2", "-attack", "wait 90", "ca_place -700 -300 40 0 75", "wait 10"]
    s += ["ca_place -700 -300 40 0", "wait 20", "ca_lineup", "wait 400"]             # the 20 types: idle and wind-up voices
    for i in range(160):                                                             # kill them: hurt and death voices
        s += ["ca_aimnearest", "+attack", "wait 6", "-attack", "wait 1"]
        if i % 5 == 4:
            s.append("ca_give 11")
    s += ["ca_lineup off", "set ca_waves 1", "wait 200",                            # a wave attacks for real:
          "god", "echo CATEST god off", "wait 600", "god", "echo CATEST god on",    # damage beeps
          "wait 60", "echo CATEST end", "quit"]
    for name, lines in (("levels.cfg", levels), ("music.cfg", music), ("sounds.cfg", s)):
        with open(os.path.join(d, name), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


def read_raw(path):
    import numpy as np
    return np.fromfile(path, dtype="<i2").reshape(-1, 2).astype(float) / 32768.0


def baked(root):
    out = {}
    for line in open(os.path.join(root, "game", "base", "catarena", "sound", "catarena", "sounds.txt")):
        name, frames, peak = line.split("\t")
        out[name] = (int(frames), float(peak))
    return out


def check(root, levels_log, levels_raw, music_log, music_raw, sounds_log, sounds_raw):
    import numpy as np
    fails = 0

    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    table = baked(root)
    # levels: N10's level at the output
    text = open(levels_log, errors="replace").read()
    played = set(re.findall(r"Cat Arena: sound (\w+) ", text))
    a = read_raw(levels_raw)
    scale = 127 * int(255 * 0.7) / 65536.0
    expect = max(table[n][1] for n in played) * scale if played else 0
    got = float(np.abs(a).max()) if len(a) else 0
    guns = sorted(n for n in played if n.startswith("gun"))
    ok(played and guns and abs(got - expect) <= 0.03 * expect,
       "levels: output peak %.4f, expected %.4f (loudest of %s at s_volume 0.7)" % (got, expect, ", ".join(sorted(played))))
    # music: s_musicvolume x the soundtrack
    text = open(music_log, errors="replace").read()
    m = read_raw(music_raw)
    ref = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", os.path.join(root, "game", "base", "catarena", "music", "ashen_drive.ogg"),
                          "-f", "s16le", "-ar", "44100", "-ac", "2", "-"], capture_output=True, check=True).stdout
    r = np.frombuffer(ref, dtype="<i2").reshape(-1, 2).astype(float) / 32768.0
    skip = int(44100 * 1.0)   # the spawn landing in the first second
    tail = m[skip:]
    n = len(tail)
    rms_out = float(np.sqrt((tail ** 2).mean())) if n else 0
    rms_ref = float(np.sqrt((r[skip:skip + n] ** 2).mean())) * 0.322 if n else 1
    ok("Background file: music/ashen_drive.ogg" in text and n > 44100 * 3 and abs(rms_out - rms_ref) <= 0.15 * rms_ref,
       "music: %.1f s of output, RMS %.4f, soundtrack x 0.322 %.4f" % (n / 44100.0, rms_out, rms_ref))
    # sounds
    text = open(sounds_log, errors="replace").read()
    ok("Cat Arena: 85 sounds registered" in text and "Cat Arena: sound " in text and not re.search(r"Cat Arena: sound \S+ missing", text),
       "85 sounds registered, none missing")
    events = [(m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)) for m in re.finditer(r"Cat Arena: sound (\w+) t=(-?\d+) ms=(\d+)(.*)", text)]
    names = [e[0] for e in events]
    started = re.findall(r"^\d+ : (\S+)$", text, re.M)
    # sound/feedback/hit.wav is the silent file that stands for every Quake III sound not shipped
    placeholder = started.count("sound/feedback/hit.wav")
    others = sorted(set(f for f in started if not f.startswith("sound/catarena/") and f != "sound/feedback/hit.wav"))
    ok(started and not others, "the engine started %d sounds: Cat Arena's and %d of the silent placeholder%s"
       % (len(started), placeholder, "" if not others else " — others: " + ", ".join(others)))
    gun_files = set(n for n in names if n.startswith("gun"))
    weapons = set(int(re.match(r"gun(\d+)_", n).group(1)) for n in gun_files)
    shots = {w: [n for n in names if n.startswith("gun%d_" % w)] for w in range(16)}
    one_take = [w for w, v in shots.items() if len(v) >= 8 and len(set(v)) < 2]   # N10's skip can repeat a take a few times
    ok(len(weapons) == 16 and not one_take, "guns: %d of 16 weapons (%d shots), %d of 32 takes; both takes of every weapon fired 8 times or more%s"
       % (len(weapons), sum(len(v) for v in shots.values()), len(gun_files), "" if not one_take else " — not: %s" % one_take))
    for group, want in (("explosions", ["explosion_rocket", "explosion_grenade", "explosion_rift"]),
                        ("body", ["body_jump", "body_step_walk", "body_step_sprint"]),
                        ("beeps", ["beep_switch", "beep_reload", "beep_hurt"])):
        have = [w for w in want if w in names]
        ok(len(have) == len(want), "%s: %s" % (group, ", ".join("%s x%d" % (w, names.count(w)) for w in want)))
    ok(any(n.startswith("body_land_") for n in names) and any(n.startswith("body_slide_") for n in names),
       "landing and slide: %s" % ", ".join(sorted(set(n for n in names if n.startswith(("body_land", "body_slide"))))))
    voices = [e for e in events if e[0].startswith("voice_")]
    kinds = set(e[0].split("_")[2] for e in voices)
    families = set(e[0].split("_")[1] for e in voices)
    ok(kinds == {"idle", "attack", "hurt", "eliminated"} and families == {"normal", "bass", "light"},
       "voices: %d; kinds %s; families %s" % (len(voices), ", ".join(sorted(kinds)), ", ".join(sorted(families))))
    # N10 voice limits
    bad = []
    for i, (name, t, ms, extra) in enumerate(voices):
        kind = name.split("_")[2]
        d = float(re.search(r"d=([\d.]+)", extra).group(1))
        enemy = int(re.search(r"enemy=(\d+)", extra).group(1))
        active = sum(1 for (n2, t2, ms2, _) in voices[:i] if t2 <= t < t2 + ms2)
        if d > 31:
            bad.append("%s at %.1f m" % (name, d))
        if (kind != "eliminated" and active >= 3) or active >= 5:
            bad.append("%s with %d playing" % (name, active))
        if kind != "eliminated":
            prev = [t2 for (n2, t2, _, x2) in voices[:i] if n2.split("_")[2] != "eliminated"]
            if prev and t - prev[-1] < 95:
                bad.append("%s %d ms after the previous voice" % (name, t - prev[-1]))
            mine = [t2 for (n2, t2, _, x2) in voices[:i] if "enemy=%d " % enemy in x2 + " "]
            gap = {"attack": 420, "hurt": 280, "idle": 1800}[kind]
            if mine and t - mine[-1] < gap:
                bad.append("%s %d ms after enemy %d's previous voice" % (name, t - mine[-1], enemy))
    ok(not bad, "N10 voice limits (31 m, 3 at once / 5 with deaths, 95 ms apart, per-enemy cooldowns): %s" % ("; ".join(bad[:6]) or "kept"))
    god_off = text.split("CATEST god off")[1].split("CATEST god on")[0] if "CATEST god off" in text else ""
    ok("Cat Arena: sound beep_hurt" in god_off, "damage beep while god mode is off: %d" % god_off.count("Cat Arena: sound beep_hurt"))
    a = read_raw(sounds_raw)
    peak = float(np.abs(a).max()) if len(a) else 0
    clipped = int((np.abs(a) >= 32767 / 32768.0).sum())
    ok(len(a) > 44100 * 10 and peak > 0.05 and clipped == 0, "output: %.1f s, peak %.3f, %d clipped samples" % (len(a) / 44100.0, peak, clipped))
    ok("ERROR" not in text and "Signal caught" not in text, "no errors")
    print("ALL PASS" if fails == 0 else "%d FAILED" % fails)
    return fails == 0


if __name__ == "__main__":
    if sys.argv[1] == "write":
        write(sys.argv[2])
    else:
        sys.exit(0 if check(*sys.argv[2:9]) else 1)
