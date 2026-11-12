# Cat Arena: Feral Claw — N4: all four maps and navigation

N4 delivered 2026-11-10, with three renderer features still open (below). This package contains
N0-N4; the original references are unchanged.

**All four maps load, render, collide and navigate natively, each verified separately against the
original game.** Helios Crucible, Bastion Keep, Reactor Bastion and The Arsenal keep their own
geometry, materials, lightmaps, floor heights, jump pads and collision.

| Map | Picture vs original | Movement vs original | Navigation vs original |
|---|---|---|---|
| Helios Crucible | unchanged from N2 | 21,820 ticks match | graph and 50 routes identical |
| Bastion Keep | 44-61 dB on 5 views | 13,420 ticks match | identical |
| Reactor Bastion | 46-64 dB on 4 views | 14,820 ticks match | identical |
| The Arsenal | 48 dB on 2 views, 33 dB on the view with a missing model | 12,720 ticks match | identical |

Still open: The Arsenal's three prop models and its portal / mirror surfaces are not drawn (one
monitor on Helios likewise), and precomputed visibility is not used (performance only).

## Start

```sh
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug   # no GPU needed
cmake --build build-core && ctest --test-dir build-core --output-on-failure            # physics + navigation, all maps

./build-desktop/cat_arena --list-maps
./build-desktop/cat_arena --map q3-gallery      # or press 1-4 in the window to change map
```

## What to read

- docs/N4_MAPS_AND_NAVIGATION.md — results per map, the sky box, navigation, what is not finished
- docs/TEST_MATRIX.md and docs/n4-evidence/ (side-by-side sheets per map, logs)
- docs/DEVELOPMENT_LOG_N4.md, docs/N4_DIFF.md, docs/KNOWN_LIMITATIONS.md

Verified here: Linux x86_64, GCC and clang, Mesa lavapipe. NOT run: Windows, Android, hardware GPUs.
No EXE/APK is supplied.

Next: finish the open N4 renderer items (models, portals/mirrors) or go on to **N5 — weapons,
projectiles, damage and HUD**; your call.
