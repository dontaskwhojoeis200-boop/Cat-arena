# Native rewrite progress — 2026-11-11

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented. N4 (four maps, navigation) delivered with
open items (MD3 prop models, portal / mirror feeds, PVS — still open).
N5 (16 weapons, projectiles, damage, pickups, weapon viewmodel, effects, HUD), N5.1 (dynamic
lights, medkit billboards) and N6 (enemies, waves, AI, animation, Marine Cats) delivered.
**N6.5 (Windows build and packaging): source ported for MSVC / Windows, packaging and the GitHub
Actions workflow written; a Windows executable has NOT been built yet** — this build machine has no
Windows toolchain and no linked GitHub account (N6_5_BUILD_REPORT.md). The packaging and play
controls were verified on Linux. Earlier trackers: docs/history/N0 … N6.

## N6.5 result (N6_5_BUILD_REPORT.md, docs/DEVELOPMENT_LOG_N6_5.md)

- Windows: preset `windows-release` (VS 2022, x64), static MSVC runtime, `vulkan-1.lib` generated
  (no Vulkan SDK), glslang built from the pinned source, `CatArena.exe` (GUI subsystem, delay-loaded
  Vulkan with a clear error when missing, CatArena.log, error box). Workflow
  `.github/workflows/windows-build.yml` builds, packages, validates (incl. DLL dependencies), zips,
  uploads and runs the GPU-free tests. **Not executed.**
- UTF-8 paths for Windows user folders with non-ASCII names; rooted asset references rejected.
- Play controls: pause while the mouse is released, F5 restart, F11 fullscreen (with 1-4 maps, Esc).
- Packages: `cmake --install` + `tools/package/validate_package.cmake` + `zip_package.cmake`; a
  Linux package built this way was extracted and played from an empty folder.
- Linux: 7 configurations clean (33 core / 63 desktop tests); replay hashes identical to N6.

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
| Core / timing / settings / input | N0 complete; **N5: fire / reload / swap input, tap latching** |
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
| HUD | N5: native HUD with the legacy's information; **N6: wave box, last-five banner, announcements**; legacy look not reproduced |
| Audio, UI / menus, network, Android | Not started (N7-N9) |
| Windows / Android builds | NOT_RUN (no toolchain / device in runner) |

## Next

First: run the Windows workflow (or the preset on a Windows PC) and close the items N6.5 leaves
unverified (MSVC build, start on Windows, Windows GPU tests). Then N7 (audio, UI, settings): enemy voices / attack and death sounds have their trigger points in the
simulation events. Open items that can be closed alongside: MD3 prop models, portal / mirror feeds
(the legacy's only view of the local Marine Cat), PVS (N4); the legacy HUD look.
