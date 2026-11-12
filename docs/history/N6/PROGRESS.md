# Native rewrite progress — 2026-11-11

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented. N4 (four maps, navigation) delivered with
open items (MD3 prop models, portal / mirror feeds, PVS — still open).
N5 (16 weapons, projectiles, damage, pickups, weapon viewmodel, effects, HUD) and N5.1 (dynamic
lights, medkit billboards) delivered.
**N6: enemies — 20 types, waves, AI, routes, flyers, attacks, animation, bodies, effects, the
"final five" outline, Marine Cats — implemented and matched to the legacy on all four maps.**
Open: sound (N7), the legacy HUD look (native layout), N4 items. Earlier trackers: docs/history/N0 … N5_1.

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

N7 (audio, UI, settings): enemy voices / attack and death sounds have their trigger points in the
simulation events. Open items that can be closed alongside: MD3 prop models, portal / mirror feeds
(the legacy's only view of the local Marine Cat), PVS (N4); the legacy HUD look.
