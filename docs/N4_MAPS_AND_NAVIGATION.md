# N4 — all four maps and navigation topology

All four maps (internal ids `helios-crucible`, `bastion-keep`, `reactor-bastion`, `q3-gallery`;
titles HELIOS CRUCIBLE, BASTION KEEP, REACTOR BASTION, THE ARSENAL) now load, render, collide and
navigate in the native engine, each with its own geometry, materials, lightmap, heights and
collision data, and each checked separately against the unmodified legacy game.

**Not finished in N4** (details at the end): the three MD3 prop models and the portal / mirror
surfaces of The Arsenal are not drawn, and precomputed visibility (PVS) is not used.

## Result per map

| Map (floors) | Rendering vs legacy, same poses | Collision vs legacy trajectories | Navigation vs legacy graph and routes |
|---|---|---|---|
| Helios Crucible (-4 / 0 / 7) | N2: 7 poses, 49-56 dB; still byte-identical to the N2 frames | N3: 17 tapes, 21,820 ticks, worst 1.8e-15 | 5,270 nodes, 34,812 / 34,726 / 34,468 edges, 50 queries (49 routes, 28 between floors): identical |
| Bastion Keep (-4 / 0 / 7, roof 11) | 5 poses, PSNR 44.4-61.2 dB | 15 tapes, 13,420 ticks, worst 1.8e-15, 13,125 bit-identical | 6,359 nodes, 43,706 / 43,706 / 43,018 edges, 50 queries (50 routes, 39 between floors): identical |
| Reactor Bastion (-4 / 0 / 5, roof 10.4) | 4 poses, PSNR 46.0-63.5 dB | 17 tapes, 14,820 ticks, worst 1.1e-16, 14,378 bit-identical | 6,182 nodes, 37,550 / 37,112 / 33,381 edges, 50 queries (49 routes, 20 between floors): identical |
| The Arsenal (-3 / 0 / 4.5, roof 10) | 3 poses: 47.9, 47.9 and 33.5 dB (the 33.5 dB view contains a missing model) | 14 tapes, 12,720 ticks, worst 8.9e-16, 12,683 bit-identical | 2,535 nodes, 18,055 / 18,017 / 16,692 edges, 50 queries (50 routes, 7 between floors): identical |

Rendering numbers are mean image statistics against legacy WebGL2 frames on the same software
rasterizer at 1280x720 (docs/n4-evidence/frames). Collision: every one of 30 recorded values per
tick within 1e-9 (observed worst as listed) and all contact counters equal, including ceiling
contacts (3, 3 and 5 on the new maps), ramps and every jump pad. Navigation: every node, every
neighbour list with its order and costs, the chosen start and goal nodes, the number of A*
expansions, the node chain and every smoothed route point are equal.

Heights are taken from each map's own data; nothing is shared between maps or derived from Helios.

## Rendering

Bastion Keep and Reactor Bastion rendered correctly with the N2 renderer as it was: their content
uses the same material system and the same baked-sky path. No change was needed, and the frames
are the same on the automatic Vulkan path and on the Vulkan 1.2 baseline.

The Arsenal needed one addition: its sky material is `skyParms env/gallery_nebula 192 -`, an image
sky box, where the other three use `$baked` (sky geometry). In the legacy that box is not a set of
image files; a JavaScript generator paints the six faces at load. N4 therefore:
- extracts the six generated faces with `tools/n4/legacy_skybox.cjs` into
  `assets/n4/skybox-env-gallery_nebula.png` (512 px faces, generator called twice and required to
  return identical bytes; provenance in assets/n4/PROVENANCE.md);
- draws them with a port of the legacy box shader (`shaders/skybox.vert/.frag`): face selection,
  `skyRotate` (0.6 degrees per second), horizon fog (`meta.sky.horizonFog` = 0.25), behind
  everything with depth LEQUAL and no depth write, followed by the two cloud stages as before;
- skips the baked sky geometry for such maps, as the legacy does.
Without the asset the loader warns and the backdrop is the clear colour.

## Collision

No code change: the N3 collision world and player controller were already map-independent.
N4 adds legacy reference trajectories for the three other maps (`native/tests/fixtures/n4`,
recorded with `tools/n3/legacy_trajectories.cjs --map ...`): four random-play tapes from spawns on
every floor, up to three ramps or stairs, every jump pad, impulse-driven ceiling cases, slide,
bunny hop and crouch. Seven builds give bit-identical native trajectories on all four maps.

## Navigation (`cat/navigation.hpp`, `src/navigation.cpp`, in `cat_arena_physics`)

A port of the legacy "NAV3" layered graph (legacy/index.html 6147-6470):
- **Nodes**: 1 m grid over the 117 m map; one node per walkable floor surface in a cell, so stacked
  floors give stacked nodes. A surface counts when the smallest walker fits there.
- **Edges**: to the nodes of the eight neighbouring cells when the height difference and slope
  allow it, diagonal moves are not cutting a corner, and a sweep along the straight line, grounded
  on the real floor every 0.22 m with the player collision, succeeds for that walker.
- **Walker profiles**: light (radius 0.27, height 1.55), standard (0.32, 1.76), heavy (0.48, 2.08),
  each with its own adjacency. Costs are horizontal distance x (1 + 0.24 x height difference).
- **Search**: A* with the legacy heap, heuristic, single-precision g-scores and the 16,000
  expansion limit, so ties resolve to the same route as in the legacy.
- **Route smoothing**: string pulling over up to ten nodes ahead, only where the full sweep works.
- `nearest`, `target_floor` and `route` reproduce how the legacy picks start and goal.
The graph is built at load (the bake deliberately does not contain it). The legacy's time-sliced
job queue, shared-route cache and background adjacency precompute are scheduling around the same
search and belong to the enemy AI (N6); they are not ported yet.

`cat::nav::RegionMap` gives the room / sector of a point from the baked regions (9, 16, 18 and 4
regions on the four maps; every spawn and every navigation node lies in one). The legacy page has
no region lookup of its own to compare with.

## Map selection

`cat_arena --list-maps`, `--map <id>`, and keys 1-4 in the window (same order as the legacy menu).
An unknown id is an error. A map change reloads scene and collision, moves the player to the new
map's first spawn and updates the title. `--map-cycle-test` drives this in a window.

## Not finished in N4

- **MD3 prop models**: The Arsenal places three (`flag`, `turret`, one more); none is drawn. The
  model binaries are in the bake; drawing them needs the two-frame vertex format, entity lighting
  and the per-surface shaders. This is the cause of the 33.5 dB view.
- **Portal, mirror and monitor surfaces**: three on The Arsenal, one on Helios. The surfaces draw
  with a black feed (as in N2); no second view is rendered into them.
- **Visibility**: the legacy's PVS is an offline bake per map (camera leaves x render clusters) that
  only removes hidden clusters; it does not change the picture. N4 keeps the N2 cone culling only.
  Regions are available as data but are not used for culling.
- Pickups, lights and decor placements are loaded data without behaviour or rendering of their own
  (weapons and items are N5).
- No legacy check exists for `RegionMap`.
