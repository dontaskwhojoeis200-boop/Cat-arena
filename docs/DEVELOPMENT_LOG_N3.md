# N3 development log — 2026-11-10

1. Read the N3 brief and the audited source ranges. The movement model is spread over six places
   (constants, moveMarine, Stage 4 collision, key handlers, jump pads, Q3 view motion); listed in
   N3_PHYSICS.md with line numbers.
2. Checked the baked data: `compiled.walls` is the complete runtime wall list (the legacy
   `levelBuildWalls` copies it in order), slabs carry planes/bases, and the 4 m grids are baked.
   Nothing had to be re-extracted from the legacy; assets/n1 is unchanged.
3. Decided how to obtain ground truth: instead of reasoning about expected numbers, record them.
   `tools/n3/legacy_trajectories.cjs` drives the unmodified page with input tapes at fixed ticks.
   The whole of `update()` could not be used (enemies attack the player), so the tool runs the
   player part of `update()` in the legacy order with the legacy functions; documented in the tool
   and in the capture report.
4. First capture: 15 scenarios. The low-ceiling search found nothing; after widening it the
   reason was clear: the lowest underside in Helios is 3.5 m above a floor and a standing jump
   reaches 3.43 m. Added two scenarios that start with an upward velocity (fixture version 2).
5. Wrote `CollisionWorld` and `PlayerController` function by function from the source, keeping
   the floating-point operation order, with `Math.hypot` reproduced as V8 implements it.
6. First replay run: all 17 scenarios within 1.8e-15, 21,725 of 21,820 ticks bit-identical, all
   contact counters equal. No tuning or correction was needed afterwards.
7. Unit tests on a synthetic world. Five of my first expectations were wrong, not the code:
   a wall I had placed in the walking lane, slab edges having no radius, a 0.40 m step-down being
   a fall, wall-strafe speed gain carrying the player past the wall end, and the air slide buffer
   being set by the crouch key rather than the slide key while the cooldown runs. The tests now
   state the legacy behaviour; the list is in N3_PHYSICS.md.
8. App: first-person window mode (fly camera kept as `--fly`), headless `--walk` with frame
   captures, `--input-selftest` pushing SDL events through the normal event path under Xvfb
   (relative mouse mode engaged, yaw change exact, moved, jumped, slid, crouched).
9. GCC -O3 rejected the test helpers with a false `-Warray-bounds` in `vector<Value>{...}`;
   helpers now build arrays element by element. Product code was not affected.
10. Seven build configurations pass and give the same native trajectory hash. Added a
    four-map collision sanity test as early input for N4.

Not done: see N3_PHYSICS.md "Not done in N3". No Windows or Android run; no hardware GPU.
