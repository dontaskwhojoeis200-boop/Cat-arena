# N3 -> N4 changes

No bytes changed in legacy/, reference/, assets/n1 or assets/n2 (docs/n4-evidence/source-integrity.txt).

Added
- native/include/cat/navigation.hpp, native/src/navigation.cpp (in `cat_arena_physics`): layered
  navigation graph, A*, route smoothing, region lookup.
- native/shaders/skybox.vert, skybox.frag: image sky box.
- assets/n4: skybox-env-gallery_nebula.png (+ .json report), PROVENANCE.md.
- native/tests/nav_tests.cpp; native/tests/fixtures/n4: three trajectory fixtures, four navigation fixtures.
- tools/n4: legacy_skybox.cjs, legacy_navigation.cjs.
- docs: N4_MAPS_AND_NAVIGATION.md, DEVELOPMENT_LOG_N4.md, N4_DIFF.md, n4-evidence/, history/N3/.
- README_N4.md, N4_FILE_MANIFEST.json.

Changed
- native/src/renderer.cpp: sky box texture (set 0 binding 6), sky box pipeline and draw, frame
  block gains `skyBox`; baked sky geometry is skipped when a map has an image box.
- native/shaders/frame.glsl: `skyBox` field and `uBoxTex` binding (all shaders recompiled; output unchanged).
- native/include/cat/scene.hpp, native/src/scene.cpp: sky box image, horizon fog, `sky_box_dir`.
- native/src/main.cpp: map table, `--list-maps`, keys 1-4, `--map-cycle-test`, default pose per map.
- native/tests/physics_tests.cpp: replay takes a map id; zero-length read fix.
- tools/n3/legacy_trajectories.cjs: `--map`, `--random`, `--random-ticks`.
- native/CMakeLists.txt: new sources, shaders and 10 tests; assets/n4 installed.
- docs: PROGRESS, TEST_MATRIX, FEATURE_PARITY, KNOWN_LIMITATIONS, BUILD.
