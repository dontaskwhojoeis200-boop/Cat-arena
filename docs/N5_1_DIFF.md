# N5.1 change list (relative to the N5 package)

New
- `native/shaders/dynamic.vert`, `dynamic.frag`.
- `native/tests/effect_frame_check.cmake`, `native/tests/fixtures/n5/frames/` (3 legacy pictures).
- `docs/N5_1_DYNAMIC_LIGHTS.md`, `DEVELOPMENT_LOG_N5_1.md`, `N5_1_DIFF.md`, `docs/n5_1-evidence/`,
  `docs/history/N5/`, `README_N5_1.md`, `N5_1_FILE_MANIFEST.json`.

Changed
- `native/shaders/frame.glsl` (light arrays, `dlights()`), `world.frag`, `material.frag`,
  `material_data.glsl` (comment).
- `native/include/cat/renderer.hpp`, `native/src/renderer.cpp` — `DynamicLight`,
  `FrameView::lights`, `RenderFeatures::dynamic_lights`, `Overlay::atlas`, frame block 672 bytes,
  per-pass light flag, `dynamic` pipeline.
- `native/include/cat/weapon_fx.hpp`, `native/src/weapon_fx.cpp` — `select_lights`, `billboard`,
  `Frame::atlas`.
- `native/src/main.cpp` — lights and billboards passed to the renderer; `--features -dlights`.
- `native/tests/fx_tests.cpp` — fixture version 2, selection and billboard tests.
- `native/tests/fixtures/n5/helios_weapon_fx.bin` — version 2 (43 values per tick).
- `tools/n5/legacy_combat.cjs` — records the uploaded lights.
- `native/CMakeLists.txt` — new shaders, three frame tests, view threshold 40 dB.
- Trackers and `docs/N5_WEAPONS.md` (note at the top).

Unchanged: `legacy/`, `reference/`, all assets, `helios_combat.bin`, all earlier fixtures.
