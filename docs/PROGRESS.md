# Native rewrite progress — 2026-11-21

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented. N4 (four maps, navigation) delivered with
open items (MD3 prop models, portal / mirror feeds, PVS — still open).
N5 (16 weapons, projectiles, damage, pickups, weapon viewmodel, effects, HUD), N5.1 (dynamic
lights, medkit billboards), N6 (enemies, waves, AI, animation, Marine Cats), N6.5 (Windows build in
GitHub Actions) and N7 (menus, settings, HUD, audio) delivered.
**N8 (four-player co-op multiplayer): implemented and tested on one machine — 4 clients over TCP
(native relay and the original server.js), cross-play with the original browser game, soak,
window self-test; Windows build and GPU-free tests in GitHub Actions. Not yet played between
separate PCs, over the internet, or on Windows with a GPU.**
**N9 (Windows platform): implemented — display modes and refresh, controls, focus, audio device
recovery, Vulkan 1.1 baseline (branch `n9-windows`), plus crash reports, EXE icon / version /
manifest and a writable log folder (branch `n9`). Tested on Linux and in Windows CI without a GPU;
not yet run on a Windows PC.**
**N10 (gameplay parity): implemented — `n10-gameplay` (another session) merged onto N9 and reviewed
against the original game (branch `n10`): two co-op enemy-shot rules restored, weapon tapes on all
four maps bit-identical, cross-play and a one-hour soak pass. Not yet played on a Windows PC.**
Branch `n11-baseline` (another session, 2026-11-13) is not yet reviewed or merged.
**N10-Q3E (engine migration onto Quake3e, branch `migration/quake3e-n10`): stages A, B, C and D done; stage E (multiplayer) part 1 done.
Stage C: first playable loop in the test arena — stock Quake III movement with Cat Arena sprint and
slide, the NAILGUN, waves of fiends (N10 rules); self-test 10/10 on Linux and Windows CI, scripted
gameplay run 11/11 on Linux.
Stage D, part 1: Reactor Bastion converted from N10 to a Quake III BSP and checked in game against
the N10 data (50 floor spots, 20 ramps, 4 jump pads). Part 2: N10's navigation reused in the server module; fiends
reach the player on every level of Reactor Bastion. Part 3: all 16 N10 weapons (two at a time,
switch, frag reroll, projectiles, blasts, recoil, 16 viewmodels and their effects); self-test ALL
PASS on both maps (Linux, Windows CI run 44841066191), scripted client run 25/25 on Linux. Part 4: all
20 N10 enemy types (stats, spawn rules, ranged attacks, flyers) with N10's animation ported to
cgame; scripted lineup and waves 9/9 on Linux. Part 5: Helios Crucible, Bastion Keep and The Arsenal converted too (N10 material meshes, baked sky, lighting matched to N10, portals, animated props); self-test, map test and waves pass on all four maps on Linux; self-test ALL PASS on all four in Windows CI run 44868659259. Part 6: N10's sounds and soundtrack (gun takes, blasts, body sounds, beeps, enemy voices with N10's limits), output level measured equal to N10's on Linux; Windows CI run 44870467090 builds and packages it, self-test ALL PASS. Part 7: N10's medkits on the four maps (+40 health, back after 13 s), spawn health 100; Windows CI run 44910818901 self-test ALL PASS. Stage D review (docs/q3e/STAGE_D_REVIEW.md): every stage D test on one build passes, converters reproduce the committed data, fall damage removed; owner's answers: keep q3map2's contrast, the stock player box and N10's flyer steering; N10's HUD / menus / settings move to stage F; stage E (multiplayer) is next. Stage E, part 1: N10's co-op rules on the Quake III server (downed instead of dead with teammates, F held 4 s within 3.2 m revives with 45 health and 2 s grace, squad wipe after 2.7 s restarts at wave 1, a player left alone gets up) with co-op name tags and the revive status line; scripted-player test 19/19 on Linux. Next: Marine Cats for the other players, four clients and network tests, the WebSocket tunnel.
Stage B: branded engine, Cat Arena game modules (ioquake3 GPL game code + C++20), generated test
arena compiled with q3map2; Windows CI builds and packages it and a dedicated server loads the arena
with the Cat Arena DLL; on Linux the menu, the arena and stock Quake III movement run. Stage A — Quake3e
dec793de imported unmodified, audited, built for Windows x64 (CI) and Linux, started with the
Vulkan renderer and no id data. No Cat Arena gameplay runs on it yet. Owner decisions: stock Quake III
movement plus Cat Arena's sprint / crouch / slide; UDP now, WebSocket tunnel later; code under GPL v2. `n10` stays the playable game (docs/q3e/).**
Direction since 2026-11-16: Windows-only, 192 MiB combined memory target, Quake III Arena-inspired
art, Vulkan 1.1 intended baseline (docs/ROADMAP_N8_N20.md). Earlier trackers: docs/history/N0 … N6_5.

## N10 result (README_N10.md, docs/DEVELOPMENT_LOG_N10.md)

- Co-op: rockets, grenades, rift bolts and detonations of every player drawn on every machine;
  remote muzzle flashes, tracers and beams; guest kills credit the guest only (deliberate
  difference: the original host counted them too). Enemy shots aimed at a downed or departed
  teammate behave as in the original again (stop on the body / on the host, no damage).
- HUD damage numbers and incoming-hit indicator; fog, lightmap intensity, overbright and the
  effects settings applied; The Arsenal's animated flag and turrets.
- Weapon tapes from the original game on all four maps: 72,880 ticks bit-identical.
- 17 renderer settings still stored only; the menu names N13 / N14 / N16 for them.
- Linux 78/78; Windows CI run 44560790833: 48/48; cross-play 13/13; one-hour soak; solo replays
  identical on Linux and Windows to N9.

## N9 result (README_N9.md, docs/DEVELOPMENT_LOG_N9.md)

- Settings → HUD / UI: display resolution (window or exclusive fullscreen) and fullscreen refresh
  rate with fallback and status; Settings → Controls: WASD / ZQSD / arrows, mouse sensitivity,
  invert Y, raw input, fire button. Focus loss clears input; audio reopens after device changes.
- Vulkan 1.1 device baseline; 1.2–1.4 features behind runtime checks (simulated 1.1 device PASS).
- Crash reports (minidump + text with the last log lines) in CrashReports next to the log; log
  falls back to %APPDATA%\CatArena\FeralClaw when the game folder is read-only.
- CatArena.exe with icon, version information and manifest.
- Fixed an intermittent hang when a `wss://` (WinHTTP) connection closed — the Render relay path.
- Linux 75/75; Windows CI run 44469465569: 45/45, crash reports with minidumps, WinHTTP stress
  15/15; solo replays identical on Linux and Windows to N8.

## N8 result (docs/N8_MULTIPLAYER.md, docs/DEVELOPMENT_LOG_N8.md)

- Port of the legacy v3.24 co-op: WebSocket relay protocol (the native relay = server.js in C++,
  built into the game as HOST ON THIS PC and as `--relay-server`), host-authoritative world with
  90 ms snapshots, 48 ms poses, remote fire, kills, damage, medkits, nearest-player targeting,
  downed / revive (hold F 4 s) / squad wipe, join / leave / host-left handling, server browser.
- New: name tags over teammates; a guest loads the room's map itself; the game menu does not
  pause a server; map changes locked in a server.
- Tests: `net_relay_protocol` (79), interop with the original server.js (78 / 30),
  `coop_multiplayer` (52), `coop_soak` (900 s of game time), `window_coop_selftest`, cross-play
  with the original browser game (13 / 13), WinHTTP client on Windows CI. Solo replays identical
  to N7.
- Memory, first figures (lavapipe): renderer Vulkan allocations 104–150 MiB, working set
  437–535 MiB (docs/N8_MULTIPLAYER.md §4) — far from the 192 MiB target; N12 / N15 work.

## N7 result (docs/N7_UI_AUDIO.md, docs/DEVELOPMENT_LOG_N7.md)

- Menus in the legacy routing and look: home (backdrop, parallax, PLAY / MULTIPLAYER / SETTINGS /
  EXIT), map selection with the live view of each map from the legacy preview cameras, seven-stage
  loading screen with ENTER THE ARENA, pause menu, exit dialog, settings in six tabs, HUD editor.
  MULTIPLAYER is a placeholder page (N8).
- Settings: the 65 legacy controls with the legacy defaults, ranges and options (recorded from the
  running page); 32 applied, 8 applied with a stated difference, 25 stored for N10 (scene
  resolution, AA, culling modes among them). Saved per user (SDL_GetPrefPath).
- HUD in the legacy layout and look (measured element boxes, safe area, keyboard / mouse and touch
  layouts, HUD scale and editor placements); performance overlay with the legacy FPS formulas,
  CPU, GPU timestamps, process RAM and exact GPU allocations.
- Input: keyboard-only navigation of every page and all 47 enabled settings controls (tested);
  mouse, touch (legacy joystick / look / buttons, analog stick in the simulation input), gamepad
  (native addition); control mode AUTO with the v0.18 Android / DeX mouse rule; pointer capture
  state machine (unit-tested; Android device NOT_RUN).
- Audio: 48 legacy sample takes and the ASHEN DRIVE soundtrack extracted with provenance; mixer,
  compressor, panners, filters and oscillators match Chromium's Web Audio on recorded graphs
  (relative RMS 6e-8 to 5e-2); legacy sound rules (guns, explosions, body sounds, enemy voices with
  their limits, beeps, music sync) fed by simulation events; SDL3 output. Legacy replays unchanged
  (bit-identical to N6.5). The v0.18 SOUND BREAK branch is catalogued, not imported.
- Tests: `audio_webaudio_parity`, `ui_menus_settings_hud` (936 checks), `window_menu_selftest`;
  Linux matrix 36 core / 67 desktop tests (TEST_MATRIX.md).

## N6.5 result (N6_5_BUILD_REPORT.md, docs/DEVELOPMENT_LOG_N6_5.md)

- Windows: preset `windows-release` (VS 2022, x64), static MSVC runtime, `vulkan-1.lib` generated
  (no Vulkan SDK), glslang built from the pinned source, `CatArena.exe` (GUI subsystem, delay-loaded
  Vulkan with a clear error when missing, CatArena.log, error box). Workflow
  `.github/workflows/windows-build.yml` on https://github.com/dontaskwhojoeis200-boop/Cat-arena:
  MSVC build, package check (incl. DLL dependencies), ZIP, artifact `CAT_ARENA_FERAL_CLAW_N6_WINDOWS`,
  34/34 GPU-free tests, start without GPU. Final run: N6_5_BUILD_REPORT.md section 6.
- Determinism across platforms: correctly rounded sin / cos / tan / atan2 / pow (`cat/js_math.hpp`),
  as Chromium computes them; legacy replays now bit-exact on (almost) every tick on Linux and Windows.
- UTF-8 paths for Windows user folders with non-ASCII names; rooted asset references rejected.
- Play controls: pause while the mouse is released, F5 restart, F11 fullscreen (with 1-4 maps, Esc).
- Packages: `cmake --install` + `tools/package/validate_package.cmake` + `zip_package.cmake`; a
  Linux package built this way was extracted and played from an empty folder.
- Linux: 7 configurations clean (34 core / 64 desktop tests).

## N6 result (docs/N6_ENEMIES.md)

- Simulation (`combat::Horde`): 24 legacy tapes on the four maps, 48,000 ticks, 64 values per tick
  (waves, every enemy's position / health / facing / attack timers / gait / route state, enemy
  projectiles, route statistics, bodies, the player, random draws): all within 1.5e-14.
- Animation (`fx::EnemyFx`): every enemy posed every tick, 441,990 poses; poses, hands, bodies and
  the Marine Cat within 1.2e-13. Cosmetic random draws (enemy effects, blood, gibs) equal to the
  legacy after every tick of every tape.
- Pictures: 21 frames of the tapes 42.5-63.6 dB (median 52.0); five staged scenes (all 20 types,
  wind-up with projected shadows, final five, bodies, Marine Cats) 48.5-52.8 dB.
- Cat Marine: runtime mesh verified against the shipped GLB (every vertex), GLB skin joints and the
  six hit parts per vertex; the seven GLB clips are not sampled by the legacy runtime, so neither
  natively. Local Marine Cat never drawn in first person; drawn in third person (debug view).
- The legacy has no loot drops (kills reroll the weapon in hand, N5).
- One deliberate difference: the route search budget is the 110 expansions per tick only, not the
  legacy's additional 1.8 ms wall-clock cut (machine-dependent).

## N5.1 result

- The legacy's per-frame light selection (best 16 of the requested lights) is reproduced on all
  18,220 recorded ticks; world and material shaders apply the lights by the legacy rules.
- Frames during effects against legacy pictures: median 27.8 dB -> 57.1 dB, minimum 41 dB
  (17 frames without practice targets). Weapon views 51.5-52.5 dB.
- Medkits are drawn as in the legacy (billboard and glow).
- No lights in a frame = the previous shaders' output: N2 golden frames byte-identical.

## N5 result

- Weapon simulation (`combat::Arena`): 29 legacy tapes, 18,220 ticks, every recorded gameplay
  value bit-identical (player, magazines, timers, projectiles, pellet end points, detonations,
  target health, frags, pickups, number of random draws). All 16 weapons fire in the tapes.
- Effects and viewmodel (`fx::WeaponFx`): viewmodel camera within 1.1e-16 of the legacy; every
  effect batch has the same quads (814,887 in the tapes, checksums equal); 17,578 cosmetic random
  draws equal. Effects never change the simulation.
- 16 weapon models, their textures and the effect atlas extracted from the legacy page; weapon
  views 45.4-47.5 dB against legacy pictures.
- Rendering: marks, depth-sorted effects, weapon model with its own camera, muzzle flash, HUD,
  practice targets; both Vulkan paths; validation clean.
- Window: left mouse fires, R reloads, Q / Tab swaps; a frag rerolls the weapon in hand; medkits
  heal; death respawns. Practice targets stand in for enemies.
- N2 golden frames still byte-identical; N3 / N4 trajectory and navigation tests pass with the
  same hashes.

## Status by area

| Area | Status |
|---|---|
| Core / timing / settings / input | N0 complete; N5: fire / reload / swap input; **N7: touch stick, sprint modes, gamepad, settings file** |
| Baked content + loaders | N1 complete; **N5: assets/n5 (weapon models, effect atlas)** |
| Rendering, four maps | Geometry, materials, lightmaps, skies match; **N10: Arsenal MD3 flag and turrets**; portals / mirrors, PVS open (N16 / N14) |
| Vulkan compatibility | N2.1: 1.2 baseline + optional 1.3/1.4 paths; **overlay passes on both** |
| Player physics and traversal | N3 / N4, verified on four maps; **N5: blast and recoil impulses** |
| Navigation topology | N4: identical to legacy on four maps; **N6: sliced route jobs and shared routes match** |
| Weapons, projectiles, damage, pickups | N5: match legacy tapes (Helios); **N10: all four maps, 72,880 ticks** |
| Weapon viewmodel, muzzle flash, tracers, impacts, explosions, marks | N5: match legacy data |
| Dynamic lights on the map, medkit billboards | **N5.1: match legacy (selection exact; frames 41-59 dB)** |
| Enemies: types, waves, AI, routes, flyers, attacks, damage, bodies | **N6: match legacy tapes on four maps** |
| Enemy animation, models, effects, final-five outline, shadows, Marine Cats | **N6: match legacy data; pictures 42.5-63.6 dB** |
| HUD | **N7: legacy layout and look, touch layout, editor, performance overlay** |
| Menus, settings | N7: legacy routing and look; **N10: 48 of 65 settings act (8 with a difference), 17 renderer settings stored (N13 / N14 / N16)** |
| Audio | **N7: legacy sounds and music; mixer matches Chromium Web Audio graphs; not heard on real hardware** |
| Network | N8: co-op multiplayer, tested on one machine (incl. original relay and browser game); **N10: explosives drawn on every machine, legacy enemy-shot rules, 1 h soak** |
| Android | Out of scope since N8 (code kept) |
| Windows build | **N7: built, packaged, 36 GPU-free tests pass in GitHub Actions; not run with a GPU** |
| Android build | Not pursued (Windows-only since N8) |

## Next

Play the N10 Windows package (CI run 44560790833) on a Windows PC with a GPU: the README_N9.md
checklist plus docs/N10_ACCEPTANCE.md (weapons, waves, co-op on two PCs). Then N11 (stable
baseline): review the `n11-baseline` branch and prepare the benchmark build and instructions; the
FPS / frame-time / RAM / VRAM figures have to be measured by the owner on the Pentium G2020 +
GTX 660 PC.
