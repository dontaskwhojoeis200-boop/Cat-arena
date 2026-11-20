# Test matrix — N10-Q3E (branch migration/quake3e-n10)

The N10 matrix is `docs/TEST_MATRIX.md`. Status: Implemented / Compiled / Executed / Tested /
Unverified / Blocked.

## Q3E-A — 2026-11-19

| Check | Result | Status | Evidence (`docs/q3e/evidence/`) |
|---|---|---|---|
| Import of Quake3e dec793de into `engine/quake3e` | file-by-file SHA-1 identical to upstream, same line-ending classes | Tested | commit 53aca6b |
| Windows x64 MSVC build: dedicated server + client with Vulkan renderer (CI run 44652125412) | built: `quake3e-vulkan.x64.exe` 2,721,792 bytes, `quake3e.ded.x64.exe` 948,224 bytes | Compiled | windows-ci-44652125412.log; artifact `CAT_ARENA_Q3E_ENGINE_WINDOWS_x64` (id 14591925802) |
| Windows dedicated server start and quit with no id data (same run) | exit code 0 | Executed | same |
| Linux build (SDL2 back end, curl off) | client, server, both renderers | Compiled | linux-make-tail.txt |
| Linux client start, Vulkan on lavapipe, no id data, no modules | renderer up; stops at missing UI module (expected) | Executed | linux-start-no-modules.txt |
| Linux client with ioquake3 GPL `ui` as native library | module loaded, menu drawn (default textures), clean quit | Executed | linux-start-ioq3-ui.txt, linux-ioq3-ui-vulkan-lavapipe.jpg |
| Linux dedicated server start / quit, no id data | exit 0 | Executed | — |
| N10 map collision and mesh sizes | four maps; all slabs convex | Executed | map-collision-stats.txt, map-mesh-stats.txt |
| N10 reference build unchanged | branch `n10` at f406358 (Linux 78/78, Windows CI 48/48) | Tested (N10) | docs/TEST_MATRIX.md |
| Any Cat Arena gameplay on Quake3e | none yet | — | — |
| GTX 660 / Windows PC with GPU | not run | Unverified | — |

## Q3E-B — 2026-11-19 (commit f8ef3c6)

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-b/`) |
|---|---|---|---|
| Windows CI run 44655962252: branded engine (MSVC x64) | `CatArena.exe` 2,736,128 bytes, `CatArena-server.exe` 963,072 bytes | Compiled | windows-ci-44655962252.log |
| Same run: game modules (CMake + MSVC; C + C++20) | `qagamex86_64.dll` 335,872, `cgamex86_64.dll` 225,792, `uix86_64.dll` 22,016 bytes; Cat Arena C++ code without warnings | Compiled | same |
| Same run: dedicated server loads `ca_test` with the Cat Arena `qagame.dll` | DLL loaded, build line printed, C++20 bridge self-test PASS, `mapname\ca_test`, exit 0 | Executed | same; artifact `CAT_ARENA_Q3E_B_WINDOWS_x64` (id 14592179897, 2,464,791 bytes) |
| Windows client (`CatArena.exe`) with a GPU | not run (CI has no GPU) | Unverified | — |
| Linux: branded client, Vulkan on lavapipe: menu module (C++20) | main menu drawn, bridge self-test PASS | Executed | linux-menu.jpg |
| Linux: client loads `ca_test` (all three Cat Arena modules) | world with lightmaps, HUD, crosshair; missing Quake III player model reported as a warning | Executed | linux-test-arena.jpg, linux-client-map-load.txt |
| Linux: stock Quake III movement with scripted input | walk forward (displacement ~270–310 u in 90 frames), jump (view +33 u), land, crouch (view 50 → 36) | Executed | linux-movement-run.txt |
| Linux: dedicated server loads `ca_test` | bridge self-test PASS, exit 0 | Executed | linux-dedicated-map-load.txt |
| q3map2 build (NetRadiant-custom 8216133, GCC 13 patch) and `ca_test` compile | BSP + VIS (75 clusters, no leak) + LIGHT (6 lightmaps) | Executed | q3map2-ca_test.txt |
| Movement compared with Quake III reference values | not yet (stage C: tolerance tests) | — | — |

## Q3E-C — 2026-11-19 (commit 75e2291)

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-c/`) |
|---|---|---|---|
| Windows CI run 44659764665: engine + modules built, packaged | `qagamex86_64.dll` 357,888, `cgamex86_64.dll` 221,696, `uix86_64.dll` 22,016 bytes; no warnings in Cat Arena code | Compiled | windows-ci-44659764665.log |
| Same run: server self-test `ca_selftest` on Windows | 10/10 PASS: wave sizes 10 / 13 / 17 and scaling, spawn points, fiend spawn and health, fiend moved by Pmove (88 u in 0.5 s, same as Linux), 4 nailgun hits leave 48 health, death counts, NAILGUN table | Tested | same; artifact `CAT_ARENA_Q3E_C_WINDOWS_x64` (id 14595620105, 2,570,232 bytes) |
| Linux: same self-test | 10/10 PASS | Tested | linux-dedicated-selftest.txt |
| Linux client, scripted gameplay run (`tools/q3e/gameplay_test.py`, lavapipe) | 11/11 PASS: walk 320 u/s, sprint 480 u/s, slide 481 → 378 u/s then ends, reload 43 → reloading → 50, fiends hurt the player (health 116 → 12), 45 nailgun kills, waves 1–3 cleared and wave 4 (22) begun, squad wipe restarts at wave 1 | Tested | linux-gameplay-run.txt, linux-gameplay-checks.txt, linux-combat.jpg |
| Played by a person on Windows with a GPU | not yet | Unverified | — |
| Fiend navigation around the platform / stairs (N10 navigation graph) | not ported (straight chase with stuck detours) | — | stage D |
