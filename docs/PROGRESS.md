# Native rewrite progress — 2026-11-12

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented. N4 (four maps, navigation) delivered with
open items (MD3 prop models, portal / mirror feeds, PVS — still open).
N5 (16 weapons, projectiles, damage, pickups, weapon viewmodel, effects, HUD), N5.1 (dynamic
lights, medkit billboards), N6 (enemies, waves, AI, animation, Marine Cats) and N6.5 (Windows
build in GitHub Actions) delivered.
**N7 (menus, settings, HUD, audio): implemented and tested on Linux; Windows build in GitHub Actions
(README_N7.md). Not yet played on Windows with a GPU or heard on a real sound card.**
Earlier trackers: docs/history/N0 … N6_5.

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
| Rendering, four maps | Geometry, materials, lightmaps, skies match; models, portals / mirrors, PVS open (N4) |
| Vulkan compatibility | N2.1: 1.2 baseline + optional 1.3/1.4 paths; **overlay passes on both** |
| Player physics and traversal | N3 / N4, verified on four maps; **N5: blast and recoil impulses** |
| Navigation topology | N4: identical to legacy on four maps; **N6: sliced route jobs and shared routes match** |
| Weapons, projectiles, damage, pickups | **N5: match legacy tapes (Helios)** |
| Weapon viewmodel, muzzle flash, tracers, impacts, explosions, marks | N5: match legacy data |
| Dynamic lights on the map, medkit billboards | **N5.1: match legacy (selection exact; frames 41-59 dB)** |
| Enemies: types, waves, AI, routes, flyers, attacks, damage, bodies | **N6: match legacy tapes on four maps** |
| Enemy animation, models, effects, final-five outline, shadows, Marine Cats | **N6: match legacy data; pictures 42.5-63.6 dB** |
| HUD | **N7: legacy layout and look, touch layout, editor, performance overlay** |
| Menus, settings | **N7: legacy routing and look; 40 of 65 settings act (8 with a difference), 25 stored (N10)** |
| Audio | **N7: legacy sounds and music; mixer matches Chromium Web Audio graphs; not heard on real hardware** |
| Network, Android | Not started (N8, N9) |
| Windows build | **N6.5: built and GPU-free tests pass in GitHub Actions; not run with a GPU** |
| Android build | NOT_RUN (no toolchain / device in runner) |

## Next

Play the N7 Windows artifact on a Windows PC with a GPU and speakers (README_N7.md). Then N8
(multiplayer). Open items that can be closed alongside: the 25 stored settings (scene resolution,
AA, culling modes, effects detail …), MD3 prop models, portal / mirror feeds, PVS (N10).
