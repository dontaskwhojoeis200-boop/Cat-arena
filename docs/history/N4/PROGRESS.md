# Native rewrite progress — 2026-11-10

N0 (core, shell), N1 (bake, loaders), N2 (Helios rendered), N2.1 (Vulkan 1.2 baseline, optional
1.3/1.4), N3 (player physics and traversal) implemented.
**N4 delivered with open items: all four maps render, collide and navigate natively and match the
legacy per map; MD3 prop models, portal/mirror surfaces and PVS are not done.** N5 not started.
Earlier trackers: docs/history/N0, N1, N2, N2_1, N3.

## N4 result

- Bastion Keep and Reactor Bastion render at N2 quality without renderer changes (44-64 dB over 9
  legacy views). The Arsenal needed its image sky box (extracted from the legacy generator, ported
  box shader): 48 dB on two views, 33 dB on the view that contains a missing model.
- Collision verified per map with new legacy trajectories: 40,960 further ticks, worst difference
  1.8e-15, contact counters equal, including ceilings, ramps and every jump pad.
- Stacked navigation ported (graph, three walker profiles, A*, smoothing): nodes, adjacency and 200
  route queries identical to the legacy on all four maps; 94 of the routes change floor.
- Region (room/sector) lookup from baked data; map selection by id, list and keys 1-4.
- N2 golden frames still byte-identical; all N2.1 and N3 tests pass.

## Status by area

| Area | Status |
|---|---|
| Core / timing / settings / input | N0 complete |
| Baked content + loaders | N1 complete |
| Rendering, four maps | Geometry, materials, lightmaps, skies match; **models, portals/mirrors, PVS open** |
| Vulkan compatibility | N2.1: 1.2 baseline + optional 1.3/1.4 paths; new maps identical on both |
| Player physics and traversal | N3; **N4: verified on all four maps** |
| Navigation topology | **N4: identical to legacy on all four maps**; enemy-side scheduling is N6 |
| Map selection | **N4**: `--map`, `--list-maps`, keys 1-4 |
| Weapons, enemies, HUD, audio, UI, network, Android | Not started (N5-N9) |
| Windows / Android builds | NOT_RUN (no toolchain/device in runner) |

## Next

Either close the open N4 items (MD3 models, portal and mirror rendering) or start N5 (weapons,
projectiles, damage, HUD). The open items do not block N5; models are needed for N5's weapon
viewmodels anyway, so they fit naturally at its start.
