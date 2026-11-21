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

## Decisions put to the owner (answered below)

| # | Question | Options | Recommendation |
|---|---|---|---|
| D1 | Movement baseline | (a) Cat Arena's movement (its constants, sprint, slide, coyote time, jump buffer, slope rules) ported into a shared C pmove on BSP traces; (b) stock Quake III `bg_pmove` constants and feel, with Cat Arena's sprint / crouch / slide added | (a): it is the movement players know, already Quake-style (same accelerate / friction / air-control model); Q3's step-up, slide-move and trace code is reused underneath |
| D2 | Online play | (a) Quake III UDP only: LAN, port forwarding or a virtual LAN for internet play; Render relay retired; (b) also tunnel Quake III packets through a WebSocket relay so the Render service keeps working | (b) later in Q3E-E if internet play without port forwarding matters; (a) for the first playable builds |
| D3 | Licence | Quake3e and the Quake III game code are GPL v2. Distributing the game requires offering the source of the engine **and** of the game modules (where Cat Arena's gameplay code will live) under GPL v2. Art, sound and maps can keep their own terms | Owner's decision; nothing is distributed until it is made (LICENSING.md) |

## Owner decisions (2026-11-19)

- **D1 → stock Quake III movement.** The baseline is Quake III's `bg_pmove` / `bg_slidemove`
  (GPL game code) with its speeds, acceleration, friction, jump and gravity; Cat Arena's sprint,
  crouch and slide are added on top as extra movement states, shared by server and predicting
  client. Consequences:
  - Units: Quake III works in its own units (player box 30 × 30 × 56 standing, view height 26,
    speed 320 u/s, gravity 800 u/s², jump 270 u/s). Cat Arena maps are in metres with a 1.8 m
    player. The converter scales maps by one fixed factor chosen in Q3E-B so the Quake III player
    fits doors, stairs and jump gaps (about 32 units per metre makes 1.8 m ≈ 56 u, Quake III's own
    proportion); jump pads are re-aimed for Quake III gravity.
  - Weapon and enemy values given in m/s (projectile speeds, splash radii, knockback, enemy
    speeds) are converted with the same factor. Blast launches keep N10's rule (up to 2.25
    player heights, 8 m/s outwards), computed with Quake III gravity and player height, instead of
    Quake III's damage-based knockback (stage D part 3; the plan said Quake III knockback, changed
    because the N10 rule is what makes rocket and grenade jumps work as players know them).
  - Movement feel changes from N10 on purpose; N10's movement tapes no longer apply.
- **D2 → UDP now, WebSocket tunnel later.** First builds use Quake III UDP networking (LAN, port
  forwarding or a virtual LAN). In Q3E-E a WebSocket tunnel is added so the Render relay keeps
  working for internet play without port forwarding.
- **D3 → GPL v2 for the code.** Engine and game-module source under GPL v2; art, sounds, music and
  maps keep the owner's terms. LICENSING.md has the release checklist.

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
| **A** Source audit and preservation | Audit, revisions, branch, import, build proof | **Done** (2026-11-19); decisions D1–D3 answered |
| **B** Windows foundation | Cat Arena-branded engine build (name, icon, base folder `catarena`, no id data), `catarena` pk3 with a legally clean test map built by our own pipeline, C++20 game DLL skeleton loading on Windows and Linux, Vulkan device report, CI package | **Done** (2026-11-19): Windows CI run 44655962252 loads the test arena with the Cat Arena DLL; Linux client renders it and runs stock Quake III movement. The Windows client itself has not run on a GPU yet |
| **C** First playable loop | One map room, movement (D1), one weapon, one enemy type, health / damage, one basic wave, collision | **Done** (2026-11-19): scripted gameplay run 11/11 (Linux), `ca_selftest` 10/10 (Linux and Windows CI). Movement is stock Quake III by decision D1, so it is checked against Quake III values (320 / 480 u/s), not N10 tapes |
| **D** Map and gameplay | Reactor Bastion converted (then the other three maps), 16 weapons, 20 enemy types, animation, wave progression, settings | Weapon / enemy / movement tolerance tests on all maps; Reactor Bastion walkthrough (all rooms, levels, bridges, ramps reachable; no invisible walls). **Part 1 done** (2026-11-19): Reactor Bastion converted and checked against N10 (50 floors, 20 ramps, 4 pads). **Part 2 done** (2026-11-20): N10 navigation reused in qagame; fiends reach every level. **Part 3 done** (2026-11-20): all 16 N10 weapons (two slots, switch, frag reroll, projectiles, blasts, recoil, viewmodels, effects). **Part 4 done** (2026-11-20): all 20 enemy types with N10's animation. **Part 5 done** (2026-11-20): Helios Crucible, Bastion Keep and The Arsenal converted (N10 material meshes, baked sky, lighting matched to N10's lightmaps, portals, animated props); self-test, map test and waves on all four maps. **Part 6 done** (2026-11-20): N10's sounds and soundtrack on Quake3e's mixer, levels measured against N10's. **Part 7 done** (2026-11-21): N10's medkits on the four maps; spawn health 100. Stage D's gameplay ports are complete. **Review done** (2026-11-21, `STAGE_D_REVIEW.md`): all tests on one build, fall damage removed; owner's answers: keep q3map2's contrast, the stock player box and N10's flyer steering; N10's HUD / menus / settings in stage F; stage E next |
| **E** Multiplayer | 4-player co-op on Quake III networking: enemies, projectiles, damage, waves, downed / revive, disconnects, (D2) | 4 clients on one machine, packet loss / latency simulation, snapshot-size audit, soak. **Part 1 done** (2026-11-21): N10's co-op rules on the server (downed, revive 4 s within 3.2 m with line of sight, 45 health and 2 s grace, squad wipe after 2.7 s restarts at wave 1, a player left alone gets up), co-op name tags and status line; scripted-player test 19/19 on Linux. Next: Marine Cats for the other players (part 2), the four-client and network tests (part 3), the WebSocket tunnel (part 4) |
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
  MD3 / IQM and takes no bone poses from cgame. Decided in Q3E-D part 4: N10's posing and skeleton
  run in cgame on the CPU and the posed meshes are drawn as polygons. Cost (CPU skinning of about
  260 triangles per creature, up to 44 creatures and bodies as polygons) to be measured in Q3E-F.
- **Determinism.** The N10 simulation uses `double` and correctly rounded math for bit-exact
  replays; Quake III's pmove uses `float` and snaps velocities. Replay parity with N10 is not a goal
  of this branch; tolerance tests replace it.
- **Scale of the work.** Stages C–E re-host most of N3–N8 on a new engine. They are sized as
  several sessions each. If the foundation proves unsuitable at any stage, `n10` continues.
