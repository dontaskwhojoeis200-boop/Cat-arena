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

## Q3E-D, part 1: Reactor Bastion — 2026-11-19

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| Conversion from N10 data (`tools/q3e/n10map2q3.py`) | 343 slab, 207 solid, 3,789 wall brushes, none skipped; 24,398 triangles in 77 material groups; 92 lights, 12 spawns, 4 pads | Executed | converter-output.txt |
| q3map2 compile (BSP, VIS, LIGHT) | no leak, 848 clusters, 26 lightmaps, 43 s; `ca_reactor.bsp` 5.7 MB | Executed | q3map2-ca_reactor-summary.txt |
| Map checked against N10 data in game (`tools/q3e/map_test.py`, Linux, lavapipe) | floors at all 50 N10 spawn and pickup spots; 20 / 20 ramps climbed from bottom to top; 4 / 4 jump pads launch the player | Tested | linux-reactor-map-test-run.txt, linux-reactor-map-test-checks.txt |
| `ca_selftest` on Reactor Bastion (Linux dedicated server) | 10 / 10 | Tested | linux-reactor-dedicated-selftest.txt |
| Fiend wave on Reactor Bastion (Linux client) | wave 1 spawns over the map; 8 of 10 fiends killed; the last 2 never reached the player across levels (no navigation yet) | Executed | linux-reactor-wave-run.txt, linux-reactor-fight.jpg |
| Pictures | bridge to the reactor tower under the N10 clouds, hall with stairs and mezzanine | Executed | linux-reactor-bridge-and-hall.jpg |
| Windows CI run 44683859233: both maps load on the dedicated server, `ca_selftest` ALL PASS on each | success | Tested | windows-ci-44683859233.log; artifact `CAT_ARENA_Q3E_D1_WINDOWS_x64` (id 14605323896, 4,126,623 bytes) |
| Jump heights between levels with stock Quake III physics (jump 1.42 m vs N10 1.63 m) | not measured; pads re-aimed by the engine | Unverified | — |

## Q3E-D, part 2: N10 navigation for enemies — 2026-11-20

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| N10 navigation graph built in qagame from `maps/ca_reactor.cadata` | 6,182 nodes; dedicated map load + self-test 0.12 s | Executed | linux-reactor-dedicated-selftest.txt |
| `ca_selftest` route check (N10 `NavGraph::route`, light profile) | from the lowest spawn spot: L1 7/7, L2 22/24, L3 18/18 spots reachable; 11/11 checks | Tested | same |
| Fiend waves, player standing still on the low level (Linux client) | waves 1–3 cleared, 60 kills | Tested | linux-reactor-nav-wave-low-level.txt |
| Fiend waves, player standing still on the high bridge | waves 1–5 cleared, 91 kills (before navigation: 8 of 10 fiends of wave 1 arrived) | Tested | linux-reactor-nav-wave-bridge.txt |
| N10's sliced / shared route scheduler (110 expansions per tick, route sharing) | not ported: whole searches, at most 2 per server frame | — | — |
| Windows CI run 44746882913: navigation graph built on Windows, route check passes, `ca_selftest` ALL PASS on both maps | success (the job prints the full console only for the first map, so the Windows route counts themselves are not in the log) | Tested | windows-ci-44746882913.log; artifact `CAT_ARENA_Q3E_D2_WINDOWS_x64` (id 14632884110, 4,295,518 bytes) |

## Q3E-D, part 3: the 16 weapons — 2026-11-20 (commit 0c533b9)

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| `ca_selftest`, dedicated server, both maps (Linux): N10 weapon table and the real server weapon code fired at a fiend | 16/16 rows match N10 (damage, rate, magazine, reload, pellets, range); NAILGUN body hit 13; HAND CANNON head hit 156 (x2); QUAD SHOTGUN 20 pellets at point blank 280; THUNDERBOLT misses at 25 m (range 20 m), RAILGUN hits at 25 m; rocket blast 1 m from the box 81 (N10 formula), none at 4 m, rift blast reaches 4 m; grenade at 1 s (704, −192) u and life 2.3 s; RAILGUN kills a 100 hp fiend; ALL PASS on `ca_test` and `ca_reactor` | Tested | linux-dedicated-selftest-d3-ca_test.txt, linux-dedicated-selftest-d3-ca_reactor.txt |
| Scripted client run on the test arena (`tools/q3e/weapons_test.py`, Linux, lavapipe) | 25/25 PASS: two slots with their own magazines and switch (Q); shotgun recoil 134 u/s on the ground and lift to 228 u/s in the air; every weapon's magazine and its fire rate over about 3 s within one shot of the N10 delay (e.g. NAILGUN 33 shots in 3000 ms vs 33.3, IRON CHAINGUN 56 in 3650 vs 56.2, RAILGUN 3 in 2900 vs 3.0); rocket at the player's feet 44 damage (N10 38 × 1.15) and launch at 350 u/s; 54 frags, each rerolled the weapon in hand into a different one, frags with all 16 weapons; waves 1–3 cleared | Tested | linux-weapons-run.txt, linux-weapons-checks.txt, linux-weapons-16-viewmodels.jpg, linux-weapons-combat.jpg |
| Reactor Bastion, god mode, random loadout, waves (Linux client) | 37 frags with all 16 weapons over waves 1–2 and part of 3; no missing-shader or model warnings | Executed | linux-reactor-weapons-waves.txt, linux-reactor-weapons.jpg |
| Windows CI run 44841066191: MSVC build (no warnings in Cat Arena code), both maps load on the dedicated server, horde + weapon self-test ALL PASS on each with the same values as Linux | success; `qagamex86_64.dll` 468,992, `cgamex86_64.dll` 233,472 bytes | Tested | windows-ci-44841066191.log; artifact `CAT_ARENA_Q3E_D3_WINDOWS_x64` (id 14680881912, 5,233,460 bytes) |
| Differences from N10, by construction or not yet ported | head hits use the top third of the Quake III enemy box (N10 has a separate, oversized head box); projectiles that reach the sky are removed without a blast (N10 explodes them at the end of their life); the frag reroll is decided on the server, so for one snapshot the client may still predict the old weapon; effects approximate N10's WFX sprites; no weapon sounds; enemy hit-box size classes (heavy / fast) come with the other enemy types | — | — |
| Played by a person with a mouse on Windows | not yet | Unverified | — |

## Q3E-D, part 4: the 20 enemy types and their animation — 2026-11-20 (commit 99eae90)

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| `ca_selftest`, dedicated server, both maps (Linux) | ALL PASS: forced first flyers of a wave's batch; 10 sampled rows of the N10 enemy table (health, speed, flying, ranged, slam, melee, wind-up, cooldown) and the hit box per class; enemy projectile constants; all 20 types spawn with their N10 health (20/20 on each map); fiend Pmove; a skyprobe steers toward a target with N10's `updateFlyer` (235 of 1,589 u closer in 1 s on the test arena = its full speed; 126 of 2,211 u on Reactor Bastion, around obstacles); plus the earlier route and weapon checks | Tested | linux-dedicated-selftest-d4-ca_test.txt, linux-dedicated-selftest-d4-ca_reactor.txt |
| Scripted client runs (`tools/q3e/enemies_test.py`, Linux, lavapipe): lineup on the test arena, waves on Reactor Bastion | 9/9 PASS: 20 creature meshes loaded; lineup of all 20 types (standing, wind-up, strike); 113 spawns of all 20 types over waves 1–5, the three flyers among them; 94 kills of all 20 types; the player hit by every attack kind (fireball: cultist, ogre, pyromancer; melee: crusher, fiend, reaver, shambler, skitter; shot: grunt, skyprobe; spike: harrier, marauder, sapper, skyraider; void orb: banshee, sentinel, skyjuggernaut, warden); flyers attacked 44 times; damage taken with god mode off | Tested | linux-enemies-lineup-run.txt, linux-enemies-waves-run.txt, linux-enemies-checks.txt, linux-enemies-lineup-and-fight.jpg |
| Footprint edge clips on Reactor Bastion (converter + q3map2), then the map test again | 12 player-clip walls; floors 50/50, ramps 20/20, pads 4/4 still pass | Tested | q3map2-ca_reactor-d4-summary.txt, linux-reactor-map-test-d4-checks.txt |
| Found while testing, fixed | (1) a rocket / rift blast could throw the player over an open edge of Reactor Bastion and out of the map (N10 never lets anyone leave its footprint): clip walls added; (2) the 16 viewmodels' textures were upside down since part 3 (the converter flipped V; N10 and Quake III both sample top row first); the part 3 viewmodel picture shows the wrong textures | Tested | — |
| Windows CI run 44856074298: MSVC build (no warnings in Cat Arena code), both maps on the dedicated server, `ca_selftest` ALL PASS on each; same enemy results as Linux (20/20 types spawn; the skyprobe's 126 of 2,211 u on Reactor Bastion) | success; `qagamex86_64.dll` 494,592, `cgamex86_64.dll` 265,216 bytes | Tested | windows-ci-44856074298.log; artifact `CAT_ARENA_Q3E_D4_WINDOWS_x64` (id 14688100747, 5,950,287 bytes) |
| Differences from N10 | creatures are posed on the CPU and drawn as polygons lit by the light grid (N10: GPU skinning with normal and metal / roughness maps, its light volume and dynamic lights; Quake III polygons get no dynamic lights); one hit box per enemy (the union of N10's two); the enemy targets the nearest living player, the animation looks at the local player; the "final five" outline, casting glows, melee arcs, blob shadows and N10's gib meshes are not ported (gibs are sprites); enemy voices and sounds not yet; the route scheduler is still the simplified one of part 2 | — | — |
| Frame time and memory with 20 creatures on the owner's PC | not measured | Unverified | Q3E-F |

## Q3E-D, part 5: Helios Crucible, Bastion Keep, The Arsenal — 2026-11-20

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| Conversion from N10 data (`n10map2q3.py` + `n10materials.py`), all four maps | Helios: 167 slab, 120 solid, 2,479 wall brushes, 161,014 material-mesh triangles in 33 materials, 1 portal surface, 61 lights, 28 spawns, 2 pads; Bastion: 156 / 96 / 2,652, 126,814 triangles in 44 materials, 61 lights, 12 spawns, 2 pads; Arsenal: 988 / 34 / 3,083, 1,000 triangles in 31 materials, 3 portal surfaces, 31 lights, 7 spawns, 1 pad, flag and turret props; Reactor Bastion unchanged apart from spawn facing and sky; none skipped; output byte-identical on a second run | Executed | converter-output-d5.txt |
| q3map2 compile with the light pass repeated until the lightmaps are within 15 % of N10's lightmap brightness | no leaks; light scale Reactor 3.794, Helios 2.977, Bastion 2.949, Arsenal 7.576 (ratio after the last pass 1.02 / 1.04 / 1.05 / 1.01); BSP 5.7 / 13.8 / 11.6 / 1.4 MB | Executed | q3map2-ca_*-d5-summary.txt |
| `ca_selftest`, dedicated server, five maps (Linux) | ALL PASS on each. Navigation graphs: Helios 5,270, Bastion 6,359, Arsenal 2,535 nodes; routes from the lowest spot Helios L1 11/11, L2 25/25, L3 17/17; Bastion 8/8, 17/17, 19/19; Arsenal 0/0, 9/9, 2/2; all 20 types spawn; a skyprobe flies 235 u toward its target in 1 s on each N10 map except Reactor Bastion (126, around obstacles) | Tested | linux-dedicated-selftest-d5-*.txt |
| Map test against the N10 data (`map_test.py`; the ramp height is now sampled six times over the walk) | Reactor 50 floors, 20/20 ramps, 4/4 pads; Helios 54 floors, 8/8 ramps, 2/2 pads; Bastion 45 floors, 10/10 ramps, 2/2 pads; Arsenal 12 floors, 97/99 ramps, 1/1 pad. The two other Arsenal ramps are 0.9 m triangular spiral steps, smaller than the Quake III player box: the start point puts the player on the next step 0.5 m higher, and the top is reached | Tested | linux-ca_*-map-test-d5-run.txt, linux-ca_*-map-test-d5-checks.txt |
| Waves on each new map (`enemies_test.py` with two far-apart spawn spots open to the sky, god mode off for 60 shots) | ALL PASS (8 checks each). Helios: 130 spawns, 128 kills of all 20 types, waves to 5, flyers attacked 65 times, 5 hits without god mode. Bastion: 160 spawns, 134 kills, waves to 6, flyers 95, 14 hits (health 42). Arsenal: 155 spawns, 124 kills, waves to 6, flyers 55, 17 hits (health 50). Every attack kind hit the player on each map | Tested | linux-ca_*-waves-run.txt, linux-ca_*-waves-checks.txt |
| Found while testing, fixed | (1) flyers that fell back to a map spawn spot were put 4.3–6.4 m above it even under a roof and then could not move (N10 places them at most 2.8 m below the roof): fixed, the Arsenal run above is after the fix (Helios and Bastion ran before it; their flyers spawned in the open); (2) spawn facing was 90° off on all maps since part 1; (3) Bastion Keep interiors black and kit pieces white before N10's baked light was carried in the vertex colours; (4) the sky was a blue Quake III default instead of N10's baked sunset box | Tested | — |
| Test-script findings, not map defects | a player standing under a roof stalls a wave: N10's flyers steer greedily toward a point 2.45 m above the player and stay above the roof (same code in N10); the waves test therefore uses spawn spots open to the sky. Map tests must run with `ca_waves 0` (enemies block the walks) | — | — |
| Pictures (Linux, lavapipe) | each map from a spawn spot and in a fight; The Arsenal's portal / camera-feed surfaces, banner and turret | Executed | linux-d5-four-maps.jpg |
| Compared with N10 pictures | by eye only (sky, light level, kit pieces); no metric comparison | Unverified | — |
| Windows CI run 44868659259: MSVC build, the test arena and the four maps on the dedicated server, `ca_selftest` ALL PASS on each with the same route and flyer results as Linux | success; `qagamex86_64.dll` 495,104, `cgamex86_64.dll` 267,264 bytes | Tested | windows-ci-44868659259.log; artifact `CAT_ARENA_Q3E_D5_WINDOWS_x64` (id 14692044570, 16,219,846 bytes) |
