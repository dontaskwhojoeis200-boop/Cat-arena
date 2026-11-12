# N2.1 -> N3 changes

No bytes changed in legacy/, reference/, assets/ (docs/n3-evidence/source-integrity.txt, 593 files).
Renderer, shaders, scene, assets and core libraries unchanged; N2 golden frame still byte-identical.

Added
- native/include/cat/physics.hpp, native/src/physics_world.cpp, native/src/player.cpp: library
  `cat_arena_physics` (collision world, player controller, view/weapon motion).
- native/tests/physics_tests.cpp; native/tests/fixtures/n3/helios_trajectories.bin (legacy reference).
- tools/n3/legacy_trajectories.cjs (development tool; Node + Playwright Chromium).
- docs: N3_PHYSICS.md, DEVELOPMENT_LOG_N3.md, N3_DIFF.md, n3-evidence/, history/N2_1/.
- README_N3.md, N3_FILE_MANIFEST.json.

Changed
- native/src/main.cpp: window mode is first-person traversal (`--fly` keeps the fly camera);
  `--spawn`, `--walk`, `--capture-every`, `--input-selftest`; collision world loaded with the map.
- native/CMakeLists.txt: physics library, three tests (`physics_units`, `physics_legacy_trajectories`,
  `physics_all_maps`), `cat_arena` links the physics library.
- docs: PROGRESS, TEST_MATRIX, FEATURE_PARITY, KNOWN_LIMITATIONS, BUILD (N3 section), ORIGIN_MAP.
