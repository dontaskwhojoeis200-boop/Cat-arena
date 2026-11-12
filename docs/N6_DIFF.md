# N6 change list (relative to the N5.1 package)

New
- Simulation: `native/include/cat/enemies.hpp`, `native/src/enemies.cpp` (`combat::Horde`, the 20
  types, waves, AI, routes, flyers, attacks, enemy projectiles, bodies).
- Presentation: `native/include/cat/enemy_fx.hpp`, `native/src/enemy_fx.cpp` (`fx::EnemyFx`: poses,
  death animation, instance lists, EFX, gibs, Marine Cats), `native/include/cat/generated/enemy_tables.inc`.
- Shaders: `native/shaders/enemy.vert`, `enemy.frag`, `enemy.glsl`.
- Tests: `native/tests/enemy_tests.cpp`, `enemy_fx_tests.cpp`, `lineup_check.cmake`;
  fixtures `native/tests/fixtures/n6/` (4 x enemies + poses, 5 legacy frames, 5 staged scenes, README).
- Tools: `tools/n6/legacy_enemies.cjs` (tapes, animation fixture, pictures, staged scenes),
  `tools/n6/legacy_enemy_assets.cjs` (assets and tables).
- Assets: `assets/n6/` (20 creature meshes, Marine Cat, 4 light volumes, report, PROVENANCE.md).
- Docs: `docs/N6_ENEMIES.md`, `DEVELOPMENT_LOG_N6.md`, `N6_DIFF.md`, `docs/n6-evidence/`,
  `docs/history/N5_1/`, `README_N6.md`, `N6_FILE_MANIFEST.json`.

Changed
- `combat.hpp/.cpp`: `Target` holds the enemy state; burn; scored respawn; fixture key bits;
  `attach(Horde*)`, `take_damage(amount, burn)`, public `damage_target`, `player_box`; event
  sequence numbers (`next_event_seq`, `Events::fire_seq`, `BlastEvent::seq`); presentation hook
  inside `step()`; `Rng::set_constant / clear_constant` (pictures only); `random_stream()`.
- `physics.hpp`, `physics_world.cpp`, `player.cpp`: `overhead`, `hits_slab`, `feature_solid_at`,
  `PlayerController::respawn`.
- `navigation.hpp/.cpp`: resumable search (`search_begin`, `search_pop`, `search_chain`).
- `weapon_fx.hpp/.cpp`: `start_fire / start_blast / advance` (tick in event order), `random_stream()`.
- `renderer.hpp/.cpp`: creature meshes and textures, light volume per map, four creature passes
  (solid, outline, projected shadow, Marine Cat), `EnemyBatch`, statistics.
- `hud.hpp/.cpp`: wave box, "last five" banner, burning.
- `main.cpp`: waves by default, `--enemies`, `--wave`, `--shadows`, `--no-light-grid`,
  `--third-person`, `--god`, `--enemy-fixture`, `--enemy-lineup` / `--lineup-shot`; wave
  announcements; walk JSON with wave and AI statistics.
- `native/CMakeLists.txt`: new sources, shaders and tests.
- `native/tests/combat_tests.cpp` (respawn draws 4 random numbers), `effect_frame_check.cmake`
  (`-DKIND=enemy`), `weapon_view_check.cmake` (`--enemies none`).
- Trackers (`docs/PROGRESS.md`, `TEST_MATRIX.md`, `FEATURE_PARITY.md`, `KNOWN_LIMITATIONS.md`, `BUILD.md`).

Unchanged: `legacy/`, `reference/`, assets n1 / n2 / n4 / n5, all N2-N5 fixtures.
