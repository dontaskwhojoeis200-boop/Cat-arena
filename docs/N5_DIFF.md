# N5 change list (relative to the N4 package)

New
- `native/include/cat/combat.hpp`, `native/src/combat.cpp` — weapon table, `Arena` (fire, reload,
  swap, hitscan, three projectile kinds, blast damage and impulse, shotgun recoil, frag reroll,
  medkits, death / respawn), `Rng`.
- `native/include/cat/weapon_fx.hpp`, `native/src/weapon_fx.cpp` — effects and viewmodel camera.
- `native/include/cat/generated/weapon_fx_tables.inc` — generated from the legacy page.
- `native/include/cat/hud.hpp`, `native/src/hud.cpp` — HUD canvas, font, practice-target boxes.
- `native/shaders/overlay.glsl`, `fx.vert/.frag`, `gun.vert/.frag`, `hud.vert/.frag`, `solid.vert`.
- `native/tests/combat_tests.cpp`, `fx_tests.cpp`, `hud_tests.cpp`, `image_compare.cpp`,
  `weapon_view_check.cmake`.
- `native/tests/fixtures/n5/helios_combat.bin`, `helios_weapon_fx.bin`, `views/view_{00,04,07,10}.png`.
- `tools/n5/legacy_combat.cjs`, `tools/n5/legacy_weapon_assets.cjs`.
- `assets/n5/` — `fx-atlas.png`, `weapons/*.gun`, `weapons/*_{base,mr,normal}.jpg`,
  `weapon-assets.json`, `PROVENANCE.md`.
- `docs/N5_WEAPONS.md`, `DEVELOPMENT_LOG_N5.md`, `N5_DIFF.md`, `docs/n5-evidence/`,
  `docs/history/N4/`, `README_N5.md`, `N5_FILE_MANIFEST.json`.

Changed
- `native/include/cat/physics.hpp`, `native/src/physics_world.cpp` — `solid_at`,
  `shot_collision_t`, `pickups()`, solid heights, `js_hypot3` exported.
- `native/src/navigation.cpp` — uses the shared `js_hypot3` (no behaviour change; navigation
  fixtures still identical).
- `native/include/cat/renderer.hpp`, `native/src/renderer.cpp` — `Overlay`, `OverlayAssets`,
  `load_overlay`, `draw(view, overlay)`, six pipelines, polygon-offset units per pipeline.
- `native/src/main.cpp` — `Session`; fire / reload / swap input; `--weapons`, `--seed`,
  `--no-targets`, `--no-overlay`, `--no-hud`, `--player-pose`, `--list-weapons`,
  `--combat-fixture` / `--scenario` / `--capture-ticks`; walk script keys `fire`, `reload`, `swap`;
  tap latching; self-test extended.
- `native/CMakeLists.txt` — library `cat_arena_fx`, new tests, new shaders, `assets/n5` install.
- Trackers: `docs/PROGRESS.md`, `TEST_MATRIX.md`, `FEATURE_PARITY.md`, `KNOWN_LIMITATIONS.md`,
  `BUILD.md`.

Unchanged: `legacy/`, `reference/`, `assets/n1`, `assets/n2`, `assets/n4`, all earlier fixtures.

Behaviour changes visible to earlier commands
- `--walk` and the window now carry a weapon, effects and a HUD. `--no-overlay` gives the N4
  picture; player movement is unchanged (the N3 / N4 trajectory tests still pass with the same
  hashes).
- `draw(view)` (pose captures, `--capture`, `--capture-set`) is unchanged: the N2 golden frames
  are still byte-identical.
