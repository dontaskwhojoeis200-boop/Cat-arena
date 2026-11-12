# Development log — N6 (enemies, waves, AI, animation, hit boxes)

2026-11-11. Runner: Linux x64, GCC 13.3 / clang 18.1, Mesa lavapipe, Xvfb, Chromium + Playwright
(as N5). The work was interrupted twice by usage limits; it resumed from the files on disk each time.

## 1. Reading the legacy game

- Enemy code was read in place (see docs/N6_ENEMIES.md section 2 for the line map). Findings that
  shaped the port: the AI decides at 30 Hz inside the 120 Hz tick; route searches are sliced (110
  expansions per tick) **and** cut after 1.8 ms of wall time; shared routes live 0.75 s; the legacy
  has no loot drops; the Marine Cat's seven GLB clips are listed but never sampled.

## 2. Simulation (combat::Horde)

- Recorder `tools/n6/legacy_enemies.cjs`: unlike N3/N5 it calls the legacy `update(dt)` itself every
  tick after delivering the tape's input events. Cosmetic code that draws random numbers runs on a
  second seeded stream, animation on a third; `performance.now` is pinned and the adjacency cache is
  filled before recording (section 3 of N6_ENEMIES.md).
- First trial (1,920 ticks, Helios) matched bit for bit on the first native run. Full Helios
  recording: 4 of 6 scenarios passed; two failed on input. Cause: the recorder held the page's loop
  with `fovPanelOpen = true`, and the legacy key handlers ignore weapon swap and slide while a panel
  is open. Fixed in the recorder (panel closed during a scenario); all four maps re-recorded.
- Native additions: `Target` carries the legacy bot state; `Arena` gained burn, the scored respawn
  (`makeRandomSpawn`), fixture actions (KILL / HURT bits), `attach(Horde*)`; physics gained
  `overhead`, `hits_slab`, `feature_solid_at`, `PlayerController::respawn`; navigation a resumable
  search.
- Result: 24 tapes, 48,000 ticks, all values within 1.5e-14.
- Unit tests (`enemy_units`, 8,759 checks): table, wave sizes / upfront batch / forced flyers /
  scaling for waves 1-12, spawn cadence and the 20-enemy cap, spawn distance, 30 decisions per
  second, wave end and the 3.5 s pause, gib vs body, body lifetime, melee damage with scaling, shot
  damage by kind, the soft restart after the player's death.

## 3. Presentation (fx::EnemyFx) and assets

- `tools/n6/legacy_enemy_assets.cjs` extracts the 20 creature meshes with their limb tags and
  skeletons, the Marine Cat (matched to the GLB, joints and hit parts per vertex), the per-map light
  volume and the tables (`generated/enemy_tables.inc`).
- Pose evaluation, death state, hand kinematics, EFX (projectile looks, trails, glows, arcs,
  impacts), gibs and death blood ported. The recorder writes an animation fixture (`--pose-out`).
  First run: all four maps within 1.2e-13; two unit-test expectations of mine were wrong (strike
  timing at 74 % vs 76 %, which side the left hand is on) and were corrected to the legacy rules.
- Renderer: instanced creature pipelines (solid, "final five" outline, projected shadow, Marine
  Cat), rig vertex shader and lit fragment shader ported from the legacy shader source, light volume
  per map.

## 4. Pictures and the order of effects

- Native frames of the recorded tapes against legacy pictures: first 19.9-59.4 dB. Investigated one
  by one:
  - The legacy draws casting glows (EFX.build) **before** posing the creatures that frame, so a glow
    follows last frame's hands (or a zero pose for a creature never drawn). Native built the glows
    after posing. Staged wind-up scene 32.3 -> 48.5 dB.
  - Explosion and blood sparks differed. A trace of the legacy's cosmetic random draws per tick
    (scratch copy of the recorder with stack traces) showed the native started effects in a fixed
    order (deaths, then the volley, then blasts) where the legacy starts them in the order `update()`
    meets them. Every presentation event now has a sequence number; the volley's number is taken
    after its damage (WFX.fire follows damageBot). fight_spin_00530: 19.9 -> 63.6 dB.
  - The animation replay test used a 90 degree FOV; the legacy page uses 105 (the muzzle position
    depends on it and decides whether a tracer reaches a wall in the firing tick). Fixed in the test.
  - A rocket's smoke puff appeared in the legacy on the tick the player died, not natively: the
    legacy runs WFX.update / EFX.update **before** the respawn clears all projectiles. The effects
    now tick inside `Arena::step` at that place (`set_presentation_hook`).
  - To keep this from regressing, the animation fixture gained the cosmetic draw count per tick
    (version 2, 81 values). It equals the legacy on every tick of all 24 tapes.
  - Flyer wings differed in one frame: the recorder evaluates the local Marine Cat pose every tick
    (for the fixture), which draws two numbers from the animation stream at the start; the app's
    replay did not, so later enemies got other gait phases. The replay now mirrors the recorder.
  - The legacy takes pictures with `Math.random = () => 0.5`; the native replay now builds picture
    frames with a constant 0.5 as well (thunderbolt jitter). 30.5 -> 62.1 dB.
  Final: 21 frames 42.5-63.6 dB (median 52.0); the rest is world texture filtering.
- Staged scenes (`--lineup-dir` / `--enemy-lineup`): 48.5-52.8 dB.

## 5. Application

- Waves are the default; `--enemies practice|none`, `--wave`, `--shadows`, `--no-light-grid`,
  `--third-person`, `--god`; walk JSON reports wave and AI statistics.
- HUD: the first wave line overlapped the score line; replaced by a wave box like the legacy's
  `#waveDisplay`, the "last five" banner and the legacy announcements.
- The N5 weapon-view tests now pass `--enemies none` (their legacy references have no enemies).
- Window input self-test and map cycle with resize pass with waves running. In the evidence run the
  input self-test failed once: a rocket's splash killed an enemy, the frag rerolled the weapon in hand
  with a full magazine and the scripted R press then had nothing to reload. The self-test checks the
  input path, so it now runs without enemies unless `--enemies` is given; the map cycle keeps waves.
  All seven configurations were then built and tested again from clean.
- One q3-gallery recording ended with "Target crashed" in Chromium while a second browser was
  running; recorded again alone, it completed.

## 6. Verification and packaging

See docs/TEST_MATRIX.md (N6) and docs/n6-evidence.
