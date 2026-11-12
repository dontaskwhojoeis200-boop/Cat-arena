# N6 — Enemies, waves, AI, animation and hit boxes

Status: implemented and checked against the unmodified legacy game (`legacy/index.html`,
SHA-256 e3c914bc…0393) on all four maps. Numbers below are from the runs listed in
`docs/TEST_MATRIX.md` (N6 section) and `docs/n6-evidence/`.

## 1. Result

| Area | How it was checked | Result |
|---|---|---|
| Simulation: 20 types, waves, scaling, spawning, 30 Hz decisions, ground routes, flyers, attacks, enemy projectiles, burn, player death and wave restart | 24 legacy tapes (6 per map), 48,000 ticks, 64 values per tick (`tests/enemy_tests.cpp`) | every value within 1.5e-14 on every tick (tolerance 1e-9); see TEST_MATRIX for bit-exact counts |
| Animation: poses of every enemy each tick, hands, attack progress, bodies (death pose, fall, side roll, sink), the local player's Marine Cat pose | 24 tapes, 81 values per tick, 441,990 poses (`tests/enemy_fx_tests.cpp`) | every value within 1.2e-13 (float32 poses; tolerance 2e-5) |
| Effects of enemies (projectile sprites and trails, impacts, casting glows, melee arcs, muzzle flashes, blood, gibs) | number of cosmetic random draws after every tick, same tapes | equal on every tick of all 24 tapes |
| Pictures | 21 frames of recorded tapes + 5 staged scenes against pictures the legacy game rendered (1280x720, lavapipe on both sides) | frames 42.5-63.6 dB (median 52.0); staged scenes 48.5-52.8 dB |
| Rules without a reference picture | unit tests (`enemy_units`, `enemy_fx_units`) | 8,759 + 83,061 checks |

Earlier results are unchanged: N5 combat and effect tapes, N3/N4 trajectories and navigation, N2
golden frames (see TEST_MATRIX).

## 2. What the legacy game does, and where it now lives

All line numbers refer to `legacy/index.html`.

| Legacy | Native |
|---|---|
| `ENEMY_TYPES`, `ENEMY_STATS`, `FLYING_STATS`, `RANGED_TYPES`, `ENEMY_SLAM_TYPES`, `ENEMY_ORB_TYPES`, `ENEMY_PALETTES`, `enemyHitScale` | `combat::enemy_types()` (20 rows, `src/enemies.cpp`) |
| `spawnBot` 11461, `beginWave` 11483, `tickWaves` 11494, `waveEnemyScale` | `combat::Horde::spawn_enemy / begin_wave / tick_waves / scale` |
| enemy part of `update()` 12236-12305: 30 Hz decision accumulator, far enemies steered every 2nd / 3rd tick, facing, attack timers, enemy shot loop, burn | `Horde::update`, `Horde::decision_tick`, `Arena::step` |
| `botNavigation` 6492, `nav3Move` 6471, sliced route jobs 6342-6571 (110 expansions per tick, queue of 80, 24 shared routes for 0.75 s, at most 3 shared per slice) | `Horde::navigate / move / process_jobs / start_job / finish_job / shared_route` on the N4 `nav::NavGraph` (resumable search added: `search_begin / search_pop / search_chain`) |
| `updateFlyer` 6597, `nav3FlyerFree`, swept steering | `Horde::update_flyer / flyer_free / flyer_sweep` |
| `enemyAttack` 11955, `enemyMuzzleOrigin` 11939, melee | `Horde::attack` |
| `damageBot` 11585, `killBot` 11909, `takeDamage` 11912, `finishPendingRespawn` 12097 (scored spawn choice) | `Arena::damage_target / take_damage / finish_respawn` |
| `enemyDeathFx` 11905, `enemyStartDeath` / `updateDyingEnemies` 10725 (24 bodies, 6.88 s; gibs at -40 hp) | `Horde::enemy_died / update_corpses` (bookkeeping), `fx::EnemyFx` (pose, gibs, blood) |
| `enemyEvalPose` 10630, `attackPhases` 10619, `enemyDeathState` 10742, `enemyHandWorld` 10759, tables 10575-10614 | `fx::eval_pose / attack_phases / death_state / hand_world`, `generated/enemy_tables.inc` |
| `EFX` 11190-11313 (projectile looks, trails, glows, arcs, impacts), `WFX.gibs` 10962 | `fx::EnemyFx` (adds to the N5 `fx::WeaponFx` batches and its cosmetic stream) |
| vertex shader rig 872-942, fragment path 980-1010 (+ Marine Cat atlas paths 1101, 1125-1180) | `shaders/enemy.vert`, `enemy.frag`, `enemy.glsl` |
| `fillEnemyInstances` 13345, `drawEnemyInstances` 13372, `drawEnemyShadows` 13383, `drawFinalFiveXray` 13397, `drawMarineCats` 13303, `inView` 5302 | `fx::EnemyFx::build` (instance lists), `Renderer` creature pipelines (solid, outline, shadow, Marine Cat) |
| `waveHUD` 11482, `#waveDisplay`, `#huntBanner`, `announce()` | `hud::build` wave box and banner, Session notices |

There are no loot drops in the legacy game: `killBot` scores the kill, plays the death effect,
rerolls the weapon in hand and counts the wave kill. Nothing is dropped, so nothing is dropped
natively either. Medkits are fixed pickups (N5).

### Rules (as ported)

- Waves: wave 1 has 10 enemies, every later wave `ceil(previous x 1.25)`. Health, speed and enemy
  damage scale by 1.05^(wave-1). A wave starts with `min(total, 18, 10 + floor(wave / 2))` enemies;
  the first is always a skyprobe, the second a skyraider from wave 3, the third a skyjuggernaut from
  wave 5. The rest arrive one every 0.2 s while fewer than 20 are alive. 3.5 s after the last kill
  the next wave begins.
- Spawning: ground enemies on a random navigation node of a randomly weighted floor layer at least
  30 m from the player; flyers on an open-sky node; otherwise a scored player spawn point.
- Decisions: the AI decides 30 times per second inside the 120 Hz tick; enemies far from the player
  are steered every 2nd or 3rd tick (stride cadence).
- Routes: at most 110 search steps per tick for all enemies together; requests queue (at most 80);
  a finished route is shared with enemies going to the same goal for 0.75 s.
- Attacks: wind-up, strike, recovery. Ranged types fire their projectile (shot 9, spike 6 — the
  marauder fires three, others two —, fireball 4 and sets the player burning for 3 s, void orb 19;
  flyers 7 / 12 / 24) with lead and spread; melee types hit for 11 / 17 / 27 within 1.55 m
  (2.2 m for slam types). At most 105 enemy projectiles exist.
- Death of the player: the wave restarts at wave 1 with three enemies at once and the rest 0.18 s
  later; enemy projectiles are removed; 2.5 s spawn protection.
- Bodies: an ordinary kill leaves a body that topples (0.78 s, one of three variants; flyers fall),
  lies for 4.5 s and sinks for 1.6 s; at -40 hp or below the body gibs instead. At most 24 bodies.
- "Final five": while 1-5 enemies of the wave are left, the enemies (at most five per type) are
  drawn a second time, 4.5 % larger, in amber through walls (depth test "greater").
- Shadows: the legacy default `cg_shadows 1` draws blob shadows under creatures; mode 3 (projected,
  posed bodies flattened along the sun) is available with `--shadows 3`.

## 3. Deliberate difference: route search budget

The legacy stops a tick's route search after 110 expansions **or 1.8 ms of wall time**, whichever
comes first, and fills adjacency lists in idle ticks by wall time. Wall time makes the same input
give different routes on different machines, which cannot be a property of the game. The native
budget is the 110 expansions only. The recorder pins the legacy clock (`performance.now = () => 0`)
and fills the adjacency cache before recording, which makes the legacy follow the same rule; the
cache content does not depend on when it is filled.

## 4. Animation, models and the Marine Cat

- Creature meshes and skeletons are taken from what the legacy page builds at load
  (`makeEnemyTemplate`: 20 meshes of 218-288 triangles, limb tags in the fraction of the material
  id; `ENEMY_RIGS` measured by `enemyRigFromMesh`). Textures are the N1 JPEG files, verified
  byte-identical to the page's data URLs (`tools/n6/legacy_enemy_assets.cjs`, `assets/n6`).
- Poses are evaluated on the CPU exactly as `enemyEvalPose` (gait by distance, flapping by time,
  look-at, attack envelopes, pain) and bent per vertex in the vertex shader as the legacy does.
  The animation draws from its own random stream (gait phase, idle seed, pain side, death variant
  and side); gameplay never depends on it.
- **Cat Marine.** The legacy draws the Marine Cat from mesh data embedded in the page
  (`buildMarineCat`, 274 triangles) that its comment says comes from `cat_marine_rigged.glb`. The
  asset tool reads the GLB that ships with the game (`assets/n1/models/character`) and matches
  every runtime vertex to a GLB vertex (worst distance 0; the triangle order differs). Each vertex
  carries the dominant GLB skin joint (JOINTS_0 / WEIGHTS_0, 21 joints) and its hit part from
  `cat_marine_hitboxes.json` (head, torso, arm_L, arm_R, leg_L, leg_R; head x2 damage) in
  `assets/n6/marine.emesh`.
- **The seven GLB clips (idle, walk, run, shoot, melee, hurt, death) are not used by the legacy
  runtime.** `CAT_MARINE_REFERENCE.animationStates` only lists them; `drawMarineCats` poses the
  Marine Cat with `enemyEvalPose` on `MARINE_RIG` (soldier gait) plus small shoot / melee / hurt
  offsets and a downed pose. The native port does the same and makes no claim of matching the
  clips; they are listed in `assets/n6/enemy-assets.json` with their channel count and length.
- Visibility: other players' Marine Cats (`fx::Guest`, filled by networking in N8) are always
  drawn. The local player's own Marine Cat is never drawn in first person — only the weapon
  viewmodel is — and is drawn in third person (`--third-person`, a native debug view: the legacy has
  no chase camera and shows the local Marine Cat only in mirror views, which are still N10 work).
- Per-part hit data: enemies use the legacy hit boxes (body + oversized head, three size classes;
  head x2 damage) as in N5. The Marine Cat's six parts are available (`fx::marine_part_at`); the
  legacy keeps `selfHitboxesEnabled: false` and has no damage to players by other players in solo,
  so they are data for N8.

## 5. Effects and the order of events

Cosmetic randomness depends on the order in which effects start. The legacy starts them in the
order `update()` meets them (a hitscan kill's blood before the volley's tracer, a rocket's blast
before the death it causes, enemy projectile impacts in the enemy shot loop). Every presentation
event of a step now carries a sequence number (`Arena::next_event_seq`) and `fx::EnemyFx` replays
them in that order. The effects tick inside `Arena::step` at the place the legacy runs
`WFX.update` / `EFX.update` (`Arena::set_presentation_hook`): before a pending respawn clears every
projectile and moves the player. With both, the number of cosmetic random draws equals the legacy
after every tick of every tape.

The light volume the creatures are lit by (the legacy "entity light grid": 24 slices of 76 x 76
probes) is the texture `bakeStaticLightmap` uploads, captured per map from the running page
(`assets/n6/light-volume/<map>.png`).

## 6. Using it

- Wave play is the default whenever a player exists (window, `--walk`, fixture replays).
  `--enemies practice` restores the N5 practice targets, `--enemies none` an empty map,
  `--wave N` starts at wave N, `--shadows 0|1|3`, `--no-light-grid`, `--third-person`, `--god`.
  The window input self-test runs without enemies unless `--enemies` is given.
- HUD: wave box ("WAVE n", "x REMAINING - y ACTIVE"), the "LAST FIVE - X-RAY TRACKING" banner, the
  legacy announcements ("WAVE n - t ENEMIES - +p% STATS", "WAVE n CLEARED!") and BURNING.
- Reference replays: `--enemy-fixture <fixture> --scenario <name> --capture-ticks ...` and staged
  scenes `--enemy-lineup lineup.txt --lineup-shot <name> --capture out.png`.
- Recording (Chromium + Playwright, see docs/BUILD.md):
  `node tools/n6/legacy_enemies.cjs --map <id> [--scale 0.5] --out <enemies.bin> --pose-out <poses.bin>`,
  `--frames "scenario:tick,..." --frames-dir <dir> --width 1280 --height 720`, `--lineup-dir <dir>`;
  assets: `node tools/n6/legacy_enemy_assets.cjs`.

## 7. Not done / differences that remain

- No sound (enemy voices, attack and death sounds) — N7.
- Not compared with a legacy picture: projected shadows on maps other than Helios, the Marine Cat
  in motion, other maps' creature frames (their simulation, animation and effect draws are
  compared). Staged scenes and frames are from Helios Crucible.
- Remaining picture differences are mostly world texture filtering (known since N2/N4); creature
  pixels differ at most a few levels.
- Lens flares, mirrors / portals (the legacy's only view of the local Marine Cat), MD3 props and PVS
  remain open from N4.
- Windows / Android not built here; hardware GPUs not run.
