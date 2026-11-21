#!/usr/bin/env python3
"""N10-Q3E: assembles a runnable Cat Arena (Quake3e) folder.

  <out>/CatArena(.exe)              engine client (Vulkan renderer), renamed
  <out>/CatArena-server(.exe)       dedicated server
  <out>/catarena/pak0.pk3           everything in game/base/catarena except map sources
  <out>/catarena/<module>x86_64.dll|.so   native game modules (qagame, cgame, ui)
  <out>/COPYING.txt, README.txt, SOURCE.txt

Usage: package.py --engine-dir D --client NAME --server NAME --modules-dir D --out D
"""
import argparse
import os
import shutil
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BASE = os.path.join(ROOT, "game", "base", "catarena")
SKIP_EXT = {".map", ".srf", ".prt", ".lin", ".bak", ".obj", ".mtl"}

README = """Cat Arena: Feral Claw — Quake3e migration prototype (N10-Q3E stage D, part 5)

Start CatArena.exe and choose PLAY. The four Cat Arena maps converted from N10 (Reactor Bastion,
Helios Crucible, Bastion Keep, The Arsenal) and the test arena. Waves of the 20 Cat Arena enemy
types (N10 rules, navigation and animation), all 16 Cat Arena weapons (two at a time, a frag
replaces the weapon in hand), stock Quake III movement plus Cat Arena sprint and slide.
This is an engine prototype, not the full game; the playable game remains the N10 build.

Controls: WASD move, mouse look, SPACE jump, SHIFT sprint, C / CTRL crouch (slide while sprinting),
MOUSE1 fire, R reload, Q switch weapon, ESC menu, ` console.

Engine: Quake3e (GPL v2), see COPYING.txt and SOURCE.txt. Game modules: Cat Arena + ioquake3 game
code (GPL v2). Art, sound and maps: Cat Arena: Feral Claw.
"""

SOURCE = """Source code for this build (GPL v2 §3):
  https://github.com/dontaskwhojoeis200-boop/Cat-arena, branch migration/quake3e-n10, commit {commit}
  engine/quake3e  Quake3e (upstream ec-/Quake3e dec793de, changes listed by git log)
  game/           Cat Arena game modules + ioquake3 game code (upstream ioquake/ioq3 52088b9)
"""


def build_pk3(dest):
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, dirnames, filenames in os.walk(BASE):
            dirnames.sort()
            for name in sorted(filenames):
                if os.path.splitext(name)[1].lower() in SKIP_EXT:
                    continue
                full = os.path.join(dirpath, name)
                arc = os.path.relpath(full, BASE).replace(os.sep, "/")
                info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))   # reproducible archive
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as f:
                    z.writestr(info, f.read())
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine-dir", required=True)
    ap.add_argument("--client", required=True)
    ap.add_argument("--server", required=True)
    ap.add_argument("--renderer", default="", help="separate renderer library (Linux builds)")
    ap.add_argument("--modules-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--commit", default="(local build)")
    a = ap.parse_args()
    exe = ".exe" if a.client.endswith(".exe") else ""
    dll = ".dll" if exe else ".so"
    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    game = os.path.join(a.out, "catarena")
    os.makedirs(game)
    shutil.copy2(os.path.join(a.engine_dir, a.client), os.path.join(a.out, "CatArena" + exe))
    shutil.copy2(os.path.join(a.engine_dir, a.server), os.path.join(a.out, "CatArena-server" + exe))
    if a.renderer:
        shutil.copy2(os.path.join(a.engine_dir, a.renderer), os.path.join(a.out, a.renderer))
    for m in ("qagame", "cgame", "ui"):
        name = m + "x86_64" + dll
        src = os.path.join(a.modules_dir, name)
        if not os.path.isfile(src):
            sys.exit("missing module " + src)
        shutil.copy2(src, os.path.join(game, name))
    build_pk3(os.path.join(game, "pak0.pk3"))
    shutil.copy2(os.path.join(ROOT, "engine", "quake3e", "COPYING.txt"), os.path.join(a.out, "COPYING.txt"))
    with open(os.path.join(a.out, "README.txt"), "w", newline="\r\n" if exe else "\n") as f:
        f.write(README)
    with open(os.path.join(a.out, "SOURCE.txt"), "w", newline="\r\n" if exe else "\n") as f:
        f.write(SOURCE.format(commit=a.commit))
    for dirpath, _, filenames in os.walk(a.out):
        for n in sorted(filenames):
            p = os.path.join(dirpath, n)
            print("%10d  %s" % (os.path.getsize(p), os.path.relpath(p, a.out)))


if __name__ == "__main__":
    main()
