# Native rewrite progress — 2026-11-10

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented. N4 (four maps, navigation) delivered with
open items (MD3 prop models, portal / mirror feeds, PVS — still open).
N5 (16 weapons, projectiles, damage, pickups, weapon viewmodel, effects, HUD) delivered.
**N5.1: dynamic lights on the map and the medkit billboards are done and match the legacy.**
Open from N5: the legacy HUD layout (the native HUD has its own) and sound. N6 not started.
Earlier trackers: docs/history/N0 … N5.

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
| Navigation topology | N4: identical to legacy on four maps; enemy-side scheduling is N6 |
| Weapons, projectiles, damage, pickups | **N5: match legacy tapes (Helios)** |
| Weapon viewmodel, muzzle flash, tracers, impacts, explosions, marks | N5: match legacy data |
| Dynamic lights on the map, medkit billboards | **N5.1: match legacy (selection exact; frames 41-59 dB)** |
| HUD | **N5: native HUD with the legacy's information; legacy layout not reproduced** |
| Enemies, audio, UI / menus, network, Android | Not started (N6-N9) |
| Windows / Android builds | NOT_RUN (no toolchain / device in runner) |

## Next

N6 (enemies): the hit boxes, damage, splash, navigation, effect entry points and dynamic lights
they need exist. Open items that can be closed before or alongside: MD3 prop models, portal /
mirror feeds, PVS (N4); the legacy HUD layout.
