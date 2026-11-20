# Engine migration plan — Cat Arena on Quake3e (N10-Q3E)

Source: the N10-Q3E handoff of 2026-11-19. Facts behind this plan: SOURCE_AUDIT.md. Status of each
stage: TEST_MATRIX.md. This plan does not change branch `n10`, which stays the playable,
independently buildable game until the migration passes its acceptance tests.

## Starting point

- N10 is complete on branch `n10` (f406358): Linux 78/78 tests, Windows CI 48/48, weapon tapes
  bit-identical to the original game on four maps, co-op cross-play with the browser game.
- Quake3e dec793de is imported unmodified in `engine/quake3e` on branch `migration/quake3e-n10`.
- Q3E-A results: Quake3e builds for Windows x64 with MSVC (CI) and for Linux; it starts with the
  Vulkan renderer and no id Software data; a GPL game module loads as a native library.
  Nothing Cat Arena-specific runs on Quake3e yet.

## What carries over and what does not

Carries over unchanged in behaviour (ported into the game modules): weapons, damage, splash,
pickups, enemies, waves, navigation, revive, frag rules, HUD layout, menus, settings semantics,
sounds and music, textures, models, map layouts.

Changes by construction, because the handoff replaces the engine layer:
1. **Collision becomes BSP brushes and Quake III traces.** N10 reproduces the original game's
   2.5-D slab collision bit for bit. Brush traces give the same walls and floors but not the same
   numbers at edges, steps and ramps, so the legacy movement and weapon tapes can no longer be
   bit-exact. They become tolerance tests (same route, same landing spot, same hit / miss).
2. **Rendering becomes Quake3e's renderer.** Pictures are compared against N10 by eye and by
   metric, not byte for byte.
3. **Multiplayer becomes Quake III client / server over UDP.** The JSON relay protocol, the
   Render WebSocket relay and cross-play with the browser game end on this branch (D2).

## Decisions needed from the owner

| # | Question | Options | Recommendation |
|---|---|---|---|
| D1 | Movement baseline | (a) Cat Arena's movement (its constants, sprint, slide, coyote time, jump buffer, slope rules) ported into a shared C pmove on BSP traces; (b) stock Quake III `bg_pmove` constants and feel, with Cat Arena's sprint / crouch / slide added | (a): it is the movement players know, already Quake-style (same accelerate / friction / air-control model); Q3's step-up, slide-move and trace code is reused underneath |
| D2 | Online play | (a) Quake III UDP only: LAN, port forwarding or a virtual LAN for internet play; Render relay retired; (b) also tunnel Quake III packets through a WebSocket relay so the Render service keeps working | (b) later in Q3E-E if internet play without port forwarding matters; (a) for the first playable builds |
| D3 | Licence | Quake3e and the Quake III game code are GPL v2. Distributing the game requires offering the source of the engine **and** of the game modules (where Cat Arena's gameplay code will live) under GPL v2. Art, sound and maps can keep their own terms | Owner's decision; nothing is distributed until it is made (LICENSING.md) |

## Integration architecture (summary; detail in Q3E_ARCHITECTURE.md)

- `engine/quake3e` — Quake3e in C, changed only where Cat Arena needs it (branding, base folder
  `catarena`, memory defaults, entity limits if measurements require), each change a reviewed
  diff against the import commit.
- `game/` — Cat Arena game modules built as native Windows DLLs (QVM cannot run C++):
  `qagame` (server: authoritative combat, enemies, waves, revive), `cgame` (client: prediction,
  effects, HUD), `ui` (menus). Entry points `dllEntry` / `vmMain` in C; Cat Arena logic in C++20
  inside, no exceptions across the boundary. Shared movement in C (`bg_*`), compiled into both
  `qagame` and `cgame` so server and predicting client run the same code.
- `tools/q3e/` — converters: Cat Arena map data → `.map` brushes + `misc_model` visuals → q3map2
  (GPL, built from source) → `.bsp` with PVS and lightmaps; textures and sounds → `catarena/*.pk3`.

## Stages

| Stage | Goal | Done when |
|---|---|---|
| **A** Source audit and preservation | Audit, revisions, branch, import, build proof | **This stage**: done except the owner's decisions D1–D3 |
| **B** Windows foundation | Cat Arena-branded engine build (name, icon, base folder `catarena`, no id data), `catarena` pk3 with a legally clean test map built by our own pipeline, C++20 game DLL skeleton loading on Windows and Linux, Vulkan device report, CI package | The branded EXE starts into a test map on Windows CI (no GPU: dedicated server + map load) and on Linux with lavapipe (screenshot) |
| **C** First playable loop | One map room, movement (D1), one weapon, one enemy type, health / damage, one basic wave, collision | A recorded session (Linux, lavapipe) shows the loop; unit tests compare movement and the weapon with N10 within stated tolerances |
| **D** Map and gameplay | Reactor Bastion converted (then the other three maps), 16 weapons, 20 enemy types, animation, wave progression, settings | Weapon / enemy / movement tolerance tests on all maps; Reactor Bastion walkthrough (all rooms, levels, bridges, ramps reachable; no invisible walls) |
| **E** Multiplayer | 4-player co-op on Quake III networking: enemies, projectiles, damage, waves, downed / revive, disconnects, (D2) | 4 clients on one machine, packet loss / latency simulation, snapshot-size audit, soak |
| **F** Optimisation and stabilisation | Memory and frame-time measurement, loading, frame pacing, settings / UI, packaging, comparison with N10 | Measured report on the owner's G2020 + GTX 660 (only the owner can run it) |

## Risks found in the audit

- **Snapshot entity limit.** Worst case 20 enemies + 105 enemy shots + 96 player projectiles + 4
  players + about 40 pickups exceeds `MAX_SNAPSHOT_ENTITIES` (256) before PVS culling. Options in
  Q3E-E: enemy shots as predicted events instead of entities, or a raised limit (engine change).
- **Memory.** The client hunk alone is 128 MiB committed at start by default (`com_hunkMegs`), the
  zone another 12 MiB: most of the 128 MiB CPU budget before any game data. Measure the real hunk
  use of Cat Arena maps and lower `com_hunkMegs` in Q3E-F (minimum 64).
- **Map conversion.** Slabs and walls convert directly to brushes (all slabs convex), but Quake III
  brushes need a closed, sealed world for PVS; Cat Arena maps are open-sky arenas. A sky shell
  brush box per map is needed; leaks must be checked by q3map2.
- **Animation.** Cat Arena poses enemies procedurally at runtime (legacy rules). Quake3e draws
  MD3 / IQM; either the poses are baked into vertex or skeletal animations, or `cgame` builds
  refEntity poses each frame (IQM skeletal). Decide in Q3E-D with a cost measurement.
- **Determinism.** The N10 simulation uses `double` and correctly rounded math for bit-exact
  replays; Quake III's pmove uses `float` and snaps velocities. Replay parity with N10 is not a goal
  of this branch; tolerance tests replace it.
- **Scale of the work.** Stages C–E re-host most of N3–N8 on a new engine. They are sized as
  several sessions each. If the foundation proves unsuitable at any stage, `n10` continues.
