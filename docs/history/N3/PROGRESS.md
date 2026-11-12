# Native rewrite progress — 2026-11-10

N0 (core, SDL3/Vulkan shell), N1 (offline bake, native loaders), N2 (Helios Crucible rendered,
matches the legacy frame), N2.1 (Vulkan 1.2 baseline, optional 1.3/1.4 paths) implemented.
**N3 implemented: deterministic 120 Hz player physics and first-person traversal on Helios
Crucible, matching recorded legacy trajectories.** N4 has not been started.
Earlier trackers: docs/history/N0, N1, N2, N2_1.

## N3 result

- New GPU-free library `cat_arena_physics`: collision world from the baked compiled data, player
  controller (walk, sprint, crouch, slide, jump with buffer and coyote time, air strafe, slopes,
  steps, ceilings, wall slide, jump pads) and the legacy view/weapon motion (bob, run pitch/roll,
  landing dip, step smoothing).
- Ground truth was recorded from the unmodified legacy game: 17 input tapes, 21,820 ticks. Native
  replay: worst difference 1.8e-15 on any of 30 values per tick, 21,725 ticks bit-identical,
  contact counters (7,310 wall/obstacle, 85 landings, 2,081 steps, 15 recoveries, 1 ceiling) equal.
- Seven build configurations give bit-identical native trajectories.
- The window is now a first-person view driven by the controller; a headless `--walk` mode renders
  traversal frames; an input self-test drives SDL keyboard/mouse events through the event path.
- The legacy's effective crouch collider (1.506 m, not the nominal 1.20 m) is preserved and
  documented (docs/N3_PHYSICS.md).
- Rendering is untouched: the N2 golden frame is still byte-identical; all N2.1 path tests pass.

Honest scope: reference trajectories exist for Helios only; no enemies, weapons, HUD or audio;
Windows keyboard/mouse capture, Android and hardware GPUs were not run.

## Status by area

| Area | Status |
|---|---|
| Core / timing / settings / input | N0 complete |
| Baked content + loaders | N1 complete |
| Helios Crucible rendering | N2 parity preserved |
| Vulkan compatibility | N2.1: 1.2 baseline + optional 1.3/1.4 paths |
| Player physics and traversal | **N3: implemented, matches legacy trajectories on Helios** |
| Other three maps | Collision data loads and is traversable (sanity test); rendering and reference trajectories are N4 |
| Weapons, enemies, HUD, audio, UI, network, Android | Not started (N5-N9) |
| Windows / Android builds | NOT_RUN (no toolchain/device in runner) |

## Next milestone: N4 — all four maps and navigation topology

Render and verify Bastion Keep, Reactor Bastion and The Arsenal (sky boxes, mirrors, MD3, patches),
record legacy trajectories for each, add map selection and the layered navigation data.
Blockers known today: none for collision (the loader already handles all four maps); rendering
needs the features listed in KNOWN_LIMITATIONS (portals, mirrors, MD3 models).
