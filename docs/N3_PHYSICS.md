# N3 — physics and first-person traversal

The player can now walk, sprint, crouch, slide, jump, air-strafe, ride jump pads and collide with
the real Helios Crucible geometry (stacked floors, deck undersides, ramps, stairs, walls) in the
native engine, at a fixed 120 Hz. The movement and collision code is a port of the legacy model and
was checked against the unmodified legacy game: **17 recorded input tapes, 21,820 ticks (181.8 s),
every value within 1.8e-15 of the legacy, 21,725 ticks bit-identical, all contact counters equal.**

No enemies, weapons, HUD or audio yet. Reference trajectories exist for Helios only.

## Modules

| Module | Depends on | Content |
|---|---|---|
| `cat_arena_physics` (`cat/physics.hpp`, `physics_world.cpp`, `player.cpp`) | `cat_arena_assets` | Collision world, player controller, view/weapon motion. No GPU, SDL or renderer |
| `cat_arena` (`main.cpp`) | physics, render | First-person window mode, headless `--walk`, `--input-selftest` |

The renderer is unchanged. The app feeds the controller one input mask per 1/120 s tick from
`cat::FixedClock` (N0) and draws from `PlayerController::view_camera()`.

## What was ported (legacy/index.html)

| Legacy | Lines | Native |
|---|---|---|
| Movement constants | 5161-5171 | `cat::physics::k*` constants |
| `levelInside`, `levelInsideOrEdge`, `worldSlabZ`, `worldSlabThickness`, `slabQuery`, `worldInCavity`, `levelWalkable`, `levelFloorHeight` | 8225-8353 | `CollisionWorld` queries |
| Wall buckets (`levelAddWall`, `levelWallBucket`), `levelDistanceSqToWall` | 8364-8391 | `CollisionWorld` (same 4 m bucket membership and order) |
| Stage 4 collision: support, capsule clear, wall hit, ceiling plane, landing plane, nearest wall | 8461-8572 | `CollisionWorld` |
| `stage4Movement` (sub-stepped sweep, recovery, wall slide, axis fallback) | 8574-8660 | `PlayerController::stage4_movement` |
| Jump pads (`worldTriggerUpdate`, `worldPadLaunch`) | 9759-9785 | `trigger_update` |
| View motion (`q3PostMove`, `q3CrashLand`, `q3ShotOrigin`, `q3ViewCamera`, `q3WeaponMotion`) | 10457-10553 | `post_move`, `crash_land`, `shot_origin`, `view_camera`, `weapon_motion` |
| `marineSlope`, `marineFriction`, `marineAccelerate`, `moveMarine` | 12114-12220 | `slope`, `friction`, `accelerate`, `move_marine` |
| Player part of `update(dt)` | 12222-12231 | `tick` |
| `aim`, `beginSlide`, keydown/keyup movement handling | 14945-14976 | `aim`, `begin_slide`, `key_event` |

The collision world is built from the N1 `compiled` data (slabs, walls, cavities, footprint,
solids, pads, spawns and the baked 4 m grids). The legacy 0.5 m acceleration structures
(`collision-accel.js`) are optimisations with identical answers and are not reproduced; the
trajectory comparison confirms the results agree.

## Constants (measured from the source, not tuned)

Tick 1/120 s. Walk 7.6 m/s, sprint x1.50, crouch x0.80. Ground/air acceleration 17.5 / 1.85.
Friction ground 6.0, crouch 7.3, slide 0.62; stop speed 2.9. Jump impulse 7.65, gravity 18
(apex 1.63 m, 0.85 s). Jump buffer 0.115 s, coyote time 0.09 s, landing-slide buffer 0.30 s.
Slide 1.12 s, cooldown 0.72 s, entry boost x1.085 (capped at 20.5 m/s). Uphill penalty 0.38,
downhill bonus 0.13. Capsule radius 0.30, height 1.80. Sweep step 0.105 (max 180 substeps), step
height 0.52, ground snap 0.24, slab tolerance 0.035. Eye 1.5 m. Look 0.003 rad per mouse count,
pitch limit +/-1.1 rad. View constants as `Q3_VIEW`.

## Crouch collider discrepancy (preserved, on purpose)

The legacy declares `PLAYER_CROUCH_HEIGHT = 1.20`, but the collision uses
`max(1.20, 1.80 - crouchOffset * 0.60)` and `crouchOffset` only ever reaches 0.49. The effective
crouched collider is therefore **1.506 m**, not 1.20 m; the camera drops 0.49 m. N3 keeps the
effective behaviour because that is what the game plays like and what the fixtures record
(`colliderHeight` is one of the compared values). Consequence: a gap between 1.21 m and 1.50 m
looks crouchable from the constant but is not passable. `collider_height()` is the single place to
change if a later milestone decides to honour the nominal 1.20 m; the unit test
"1.6 m gap: standing blocked, crouched fits" and the fixtures would then need re-recording.

## Legacy behaviours that are kept and may look like bugs

- Slab volumes (decks, blocks) are tested at the capsule centre only; the radius applies to wall
  segments. A block without wall segments can be approached to its edge.
- Holding forward into a wall at an angle builds tangential speed above walk speed (Quake-style
  projected acceleration).
- Stepping down more than 0.24 m is a short fall, not a step; up to 0.52 m up is a step.
- An invalid position (for example after standing up under something) is repaired by a search of
  up to 1 m around the player and costs 65% of the speed.
- No fall damage. Landing only dips the view.
- The "continuous sweep" is sampled every 0.105 m, not an exact time-of-impact solve.

## Determinism

- Fixed 1/120 s ticks; one input mask per tick; press/release events derived from mask changes.
- Floating-point contraction is disabled for the physics library (`-ffp-contract=off`), and the
  code keeps the legacy operation order. `Math.hypot` is reproduced as V8 computes it (`js_hypot`).
- Seven builds (GCC Debug, GCC -O3, ASan+UBSan, clang Debug, clang -O3, with and without the
  desktop targets) produce the same hash over all 654,600 native values of the fixture replay
  (`docs/n3-evidence/trajectory-hash-by-build.txt`).
- Not guaranteed: bit-identical results on a different C math library. `sin`, `cos`, `atan2` come
  from the platform; the 95 ticks that differ from the legacy by at most 1.8e-15 are consistent
  with last-bit differences between V8 and glibc in those functions (not traced call by call).
  Windows and Android were not run.

## Reference trajectories

`tools/n3/legacy_trajectories.cjs` runs the hash-checked, unmodified legacy page in headless
Chromium. Per tick it sends real `KeyboardEvent`s for key changes, calls the legacy `aim()` for
mouse motion, then runs the player part of the legacy `update(dt)` in its original order
(`moveMarine`, `stage4Movement`, `worldTriggerUpdate`, `q3PostMove`) and records 30 values:
position, velocity, ground flag, crouch offset, yaw, pitch, all movement timers, slope angle, FOV
scale, pad cooldown, rendered eye height / forward.z / roll / up.z, bob cycle, step and landing
state, the four weapon-motion values and the collider height. The rest of `update()` (weapons,
enemies, pickups) is not run. The fixture is `native/tests/fixtures/n3/helios_trajectories.bin`
(5.76 MB, SHA-256 `6edb28b7...c207b7`).

| Scenarios | Ticks | What they cover |
|---|---|---|
| 6 random-play tapes from spawns on floors -4, 0 and 7 | 14,400 | Mixed keys, sprint, crouch, jumps, slides, mouse turns; walls, ledges, falls between floors, recoveries |
| 3 ramps/stairs (0 -> 7 and -4 -> 0) | 2,700 | Climb, stop, descend, sprint and jump on the incline; 1,671 step contacts |
| 2 jump pads | 1,400 | Launch arc to z = 10.7, landing |
| 2 ceiling cases | 720 | Upward impulse under the lowest deck undersides (see note) |
| slide + crouch, air-strafe bunny hop, crouch walk, idle | 2,600 | Slide entry/duration/cooldown, strafe-jump speed, crouch offset and collider height |

Totals: 7,310 horizontal contacts, 85 landings, 2,081 steps, 15 recoveries, 1 ceiling contact —
identical in native and legacy. Note: Helios has no underside within reach of a standing jump
(lowest clearance 3.5 m, head reach 3.43 m), so the legacy ceiling case starts with an upward
velocity as a blast jump would give. Ceilings, low gaps, steps and slopes are additionally covered
by unit tests on a small synthetic world.

## Using it

```sh
cat_arena                         # window: click to capture the mouse; WASD, Shift, Space, Ctrl, C; Esc
cat_arena --spawn 12              # start at another baked spawn point
cat_arena --fly                   # the N2 fly camera
cat_arena --offscreen 960x540 --walk "w:150,w+shift:200,w+shift+jump:3,w@4:200" --capture-every 60 --capture-dir out
cat_arena --frames 60 --input-selftest
```

## Not done in N3

- Other maps: their collision data loads and random play stays inside (test `physics_all_maps`),
  but there are no legacy reference trajectories for them. N4.
- Sprint modes "toggle" and "auto", touch stick, arrow-key look, edge-turn: settings/UI work (N7).
- Downed state, blast impulses, knockback: the hooks exist in the state, the sources are N5/N6.
- Footstep/jump/landing/slide sounds: counted (`footsteps`, `landings`, `jumps`), silent until N7.
- The weapon viewmodel is not drawn; `weapon_motion()` is verified numerically only (N5).
- Interpolation between ticks for rendering: the legacy renders the latest tick; so does N3.
- Windows keyboard/mouse capture: SDL3 relative mouse mode and scancodes are used; NOT_RUN on Windows.
