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

## Q3E-D, part 6: sounds and soundtrack — 2026-11-20

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| Sound conversion (`convert_sounds.py`: `sound_bake.cpp` built against N10's mixer, ffmpeg) | the 48 N10 samples rendered into 86 WAV files with N10's levels, bus levels, voice tones and filters baked in (32 gun takes, 3 blasts, 10 body sounds, 36 voices, 5 beeps), none clipped (highest peak 0.98, rift blast); soundtrack as Ogg Vorbis (1.9 MB); output identical on a second run | Executed | — (files in `game/base/catarena/sound`, `music`) |
| Output level (Linux client, SDL disk audio driver capturing Quake3e's mix) | loudest output sample 0.1926 for an expected 0.1926 (baked peak x 127 x int(255 x 0.7) / 65536): N10's sample x gain x bus x master reaches the output | Tested | linux-sound-levels-run.txt, linux-sound-checks.txt, linux-sound-output.png |
| Soundtrack | plays as the background track; output RMS 0.0029 = the soundtrack's x 0.322 (N10 music 46 % x master 70 %) | Tested | linux-sound-music-run.txt |
| Scripted run with every sound rule (`sound_test.py`) | 15/15 PASS: 84 sounds registered; the engine started 344 sounds, all Cat Arena's apart from 3 of the silent placeholder; 16 guns (240 shots, 25 of 32 takes, both takes of every weapon fired 8+ times); rocket / grenade / rift blasts; jump, landing (3 force levels), walk and sprint steps, slide; switch, reload and damage beeps; 67 enemy voices of all four kinds and all three families, N10's limits kept (31 m, at most 3 playing, 95 ms apart, per-enemy cooldowns); 63 s of output, peak 0.43, nothing clipped | Tested | linux-sound-run.txt, linux-sound-checks.txt, linux-sound-output.png |
| Found while testing, fixed | the reload button acted only when held past the shot cooldown (a short press of R right after a shot did nothing since part 3); the press is now kept until the weapon is ready (N10 reloads on the press) | Tested | — |
| Regression after the reload change | `ca_selftest` ALL PASS on the five maps; weapons scripted run ALL PASS (27 checks) | Tested | linux-dedicated-selftest-d6-*.txt, linux-weapons-d6-run.txt, linux-weapons-d6-checks.txt |
| Differences from N10, by construction | per-play random pitch (+-1.4 %) not reproduced; no compressor on the weapon and enemy buses; voices use Quake III's distance curve (full to 2.5 m, silent at 41.6 m, linear) and pan instead of N10's (1 - d / 35)^1.1 and equal-power pan; landing, slide and blast levels baked at 4 / 3 / 3 steps; weapon and enemy volume settings fixed at N10's defaults (only master = `s_volume` and music = `s_musicvolume` adjustable); voices only for enemies in the client's snapshot; Quake3e warns that the soundtrack is not 22 kHz (it plays at 44.1 kHz) | — | — |
| Heard by a person on real audio hardware | not yet (lavapipe / disk driver only) | Unverified | — |
| Windows CI run 44870467090: MSVC build (no warnings in the new Cat Arena code), 86 sounds and the soundtrack in `pak0.pk3`, `ca_selftest` ALL PASS on the five maps | success; `qagamex86_64.dll` 496,128, `cgamex86_64.dll` 274,432 bytes; no audio device on the runner, so nothing is played there | Tested | windows-ci-44870467090.log; artifact `CAT_ARENA_Q3E_D6_WINDOWS_x64` (id 14694011711, 21,551,903 bytes) |

## Q3E-D, part 7: medkits — 2026-11-21

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/`) |
|---|---|---|---|
| Medkit spots | N10's own `world.pickups()` (medkit spots on a navigation floor), read by qagame from the map's N10 data: Reactor Bastion 38, Helios Crucible 26, Bastion Keep 33, The Arsenal 5 (all of the spots in the map data), test arena none | Tested | linux-dedicated-selftest-d7-*.txt |
| N10's medkit rule (`ca_selftest`, a stand-in player on each map's medkits) | 11/11 on each N10 map: 50 -> 90 and the medkit hidden for 13 s with EV_CA_MEDKIT 40; not again within 13 s; back after 13 s; not taken at 100; 75 -> 100; 0.80 m taken, 0.90 m not (N10 0.85 m); 1.1 m above taken, 1.3 m not (N10 1.2 m); taken when passed over within one 2.4 m move (the move is sampled every 0.125 m, N10 checks every 1/120 s); not by a dead player | Tested | linux-dedicated-selftest-d7-*.txt |
| Client run on Reactor Bastion (`medkit_test.py`) | ALL PASS (8): spawn health 100 (N10; Quake III gave 125 and counted down); a medkit is not taken at full health; walked over at 40 -> 80 with the medkit beep; back and taken again 13,000 ms later, 80 -> 100; 2 heals in all | Tested | linux-medkit-run.txt, linux-medkit-checks.txt, linux-medkit.jpg |
| Drawing | glow (additive, 1.3 m, pulsing) and the medkit tile of N10's world atlas on a 0.65 m quad turning with the player's yaw and bobbing 0.20 +- 0.06 m; the cut-out and N10's flat light (0.853) baked into the texture; gone while it recharges | Executed (pictures) | linux-medkit.jpg |
| Differences from N10 | no dynamic lights or fog on the medkit; the bob phase uses the Quake III x coordinate (N10's uses N10's x) | — | — |
| Regression | `ca_selftest` ALL PASS on the five maps; sound run ALL PASS (85 sounds with the medkit beep); weapons run ALL PASS (27; 39 frags, all 16 weapons) and stage C gameplay run ALL PASS (11; waves 1-4 cleared) after two test-script changes the 100 health needed: the gameplay run holds the waves while it turns god mode on (with 100 health instead of 125 the fiends usually win the no-god part, and a dead player cannot turn it on) and runs with `ca_loadout "0 1"` (its reload check is the NAILGUN's, the loadout has been random since part 3); the weapons run moves the player between two spots every 100 shots (a skyprobe hovering behind a pillar held wave 1 at 9 frags) | Tested | linux-weapons-d7-*.txt, linux-gameplay-d7-*.txt, linux-sound-d7-*.txt |
| Windows CI run 44910818901 | success: MSVC build; `ca_selftest` ALL PASS on the five maps (the verdict includes the medkit checks; the log keeps the full output of the test arena run only); `qagamex86_64.dll` 501,760, `cgamex86_64.dll` 275,456 bytes | Tested | windows-ci-44910818901.log; artifact `CAT_ARENA_Q3E_D7_WINDOWS_x64` (id 14709448187, 21,556,463 bytes) |

## Q3E-D review — 2026-11-21

Full account in `STAGE_D_REVIEW.md`; logs in `evidence/stage-d/review/`.

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-d/review/`) |
|---|---|---|---|
| Clean clone: engine, game modules | build; 0 engine warnings, 2 in unmodified ioquake3 game code | Compiled | engine-build.log, game-build.log |
| Converters re-run on the clean clone | every committed output reproduced byte for byte | Tested | convert.log |
| q3map2 re-run (test arena, The Arsenal) | all lumps identical except the light grid (threaded grid pass); single-threaded light reproducible | Tested | — |
| Every stage D test on one build (e6d303f), `run_stage_d.py` | self-tests 5/5, gameplay, weapons, lineup, map tests 4/4, sound, medkits ALL PASS; waves ALL PASS on Helios, Bastion, Arsenal and on Reactor in the run before the last (8/9 in the last: no melee hit) | Tested | summaries.txt and the logs / checks |
| Player box survey (`ca_fitsurvey`) | the stock Quake III box reaches 91-98 % of the nodes N10's player reaches; the rest lie within 1 m of its reach (wall strips) except 6 nodes on Reactor Bastion (four corner pockets) and 6 within 2 m on The Arsenal | Tested | fitsurvey-*.txt |
| Lightmap brightness spread | N10 percentiles 10-90 about 36-72, q3map2 5-146 (Reactor Bastion; same means): q3map2's shadows near black | Tested (measured) | STAGE_D_REVIEW.md section 5 |
| Fall damage | removed (N10 has none); an 8 m fall costs no health | Tested | medkit.log.checks |
| Windows CI run 44938184467 (commit 7ec5023) | success: MSVC build, `ca_selftest` ALL PASS on the test arena and the four maps | Tested | windows-ci-44938184467.log; artifact `CAT_ARENA_Q3E_D7_WINDOWS_x64` (id 14720367748, 21,558,675 bytes; the review's build, the name was not changed) |

## Q3E-E, part 1: co-op rules — 2026-11-21

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-e/`) |
|---|---|---|---|
| N10's co-op rules on the dedicated server, three scripted players (`coop_test.py`; bot slots without bot AI, driven by `ca_input` / `ca_moveplayer` / `ca_hurt`) | ALL PASS (19): at 0 health with teammates a player goes down, not dead (YOU ARE DOWN); a teammate 1.9 m away sees HOLD F TO REVIVE, one far away nothing; a downed player takes no damage; holding F far away does nothing; half way REVIVING 50 % / BEING REVIVED; revived 4,000 ms after F was first held (N10 4 s) with 45 health and 2 s without damage, damage again after it; 3.5 m out of reach, 3.0 m in reach (N10 3.2 m); no revive through a cover block 2.5 m away (line of sight); all three down → SQUAD ELIMINATED for everybody, wiped 2,750 ms after the last went down (N10 2.7 s; 50 ms server frames), everybody back at 100 at wave 1; a player left alone while down gets up; alone, a player dies (no downed state) | Tested | linux-coop-e1-run.txt, linux-coop-e1-checks.txt |
| Co-op HUD in the client (`coop_test.py view`, two scripted teammates) | name tags (name, health bar, DOWN - HOLD F / REVIVING, nearest on top, smaller and fainter with distance), the status line for each state (HOLD F TO REVIVE, REVIVING n %, YOU ARE DOWN, BEING REVIVED n %, SQUAD ELIMINATED), no weapon and no Quake III scoreboard while down; F revives (bound to `+button2`), Quake III's "No item to use" and crosshair names switched off | Executed (pictures) | linux-coop-e1-hud.jpg, linux-coop-e1-view-run.txt |
| Regression (`run_stage_d.py selftest coop gameplay weapons medkit sound`, commit aa27736) | self-tests 5/5, coop, weapons, sound, medkits ALL PASS; gameplay 9/11: one enemy held wave 1 at 9 of 10 kills while the player stood at one spot (as the weapons run's skyprobe in D7). The gameplay script now moves between two spots every 100 shots, as the weapons script does: ALL PASS (11; waves 1-3 cleared, 44 kills) on the part 2 build | Tested | linux-regression-e1-summary.txt, linux-gameplay-e1-checks.txt, linux-regression-e2-summary.txt, linux-gameplay-e2-checks.txt |
| Differences from N10 | the revive key is F through Quake III's use-item button; the status line and tags are drawn with the Quake III 2D calls in the stage B look (N10's fonts and panels with the HUD port, stage F); a wipe restarts the round in place (N10 reloads the map); teammates are not drawn yet (Marine Cats: part 2) | — | — |

## Q3E-E, part 2: Marine Cats — 2026-11-21

| Check | Result | Status | Evidence (`docs/q3e/evidence/stage-e/`) |
|---|---|---|---|
| Other players drawn as N10's Marine Cat (`coop_test.py marines`, two scripted teammates) | N10's mesh (822 vertices) loaded; standing (breathing), walking (soldier gait from the player's own movement), firing (shot layer), down (N10's downed pose, lying on its side), seen from behind; armour colours, cat face cut-out and the gun as in N10's mesh | Executed (pictures) | linux-marines-e2.jpg, linux-marines-e2-run.txt |
| Regression on the part 2 build | coop ALL PASS (19), gameplay ALL PASS (11) | Tested | linux-regression-e2-summary.txt |
| Differences from N10 | every teammate gets the guests' tint (a Quake III client does not know which player hosts); no melee layer (no melee event); tracers of other players start at the eye, not at the Marine Cat's gun; enemy hits on players use the Quake III player box, not N10's six Marine Cat parts; the local Marine Cat only in third person (not in mirrors) | — | — |
| Windows CI run 44944222390 (commit abecd23, parts 1 and 2) | success: MSVC build; `ca_selftest` ALL PASS on the test arena and the four maps; the co-op rules test ALL PASS (19) on the Windows dedicated server; `cgamex86_64.dll` 283,136 bytes | Tested | windows-ci-44944222390.log; artifact `CAT_ARENA_Q3E_E1_WINDOWS_x64` (id 14723165437, 21,627,281 bytes) |
