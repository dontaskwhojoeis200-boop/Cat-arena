# N1 -> N2 changes

No bytes changed in legacy/, reference/ or assets/n1/ (verified against the N1 package).

Added
- native/include/cat/scene.hpp, native/src/scene.cpp: GPU-free scene preparation (materials,
  camera, clusters, textures, legacy vertex light, sky dome).
- native/src/image_codec.cpp + native/third_party/stb (stb_image 2.30, stb_image_write 1.16,
  pinned commit, SHA-256 in PROVENANCE.txt, MIT/public-domain licence).
- native/shaders/{frame.glsl, material_data.glsl, sky.*, world.*, material.*}.
- native/tests/scene_tests.cpp, native/tests/render_tests.cpp.
- tools/n2: legacy_reference.cjs, legacy_texture_dump.cjs, audit_textures.py, compare_frames.py,
  helios_poses.txt (offline/test tooling only; nothing here is a runtime dependency).
- assets/n2: world-atlas-runtime.png, texture_audit.json, PROVENANCE.md.
- docs: N2_RENDERER.md, DEVELOPMENT_LOG_N2.md, N2_DIFF.md, n2-evidence/, history/N1/.
- README_N2.md, N2_FILE_MANIFEST.json.

Changed
- native/src/renderer.cpp, native/include/cat/renderer.hpp: N0 renderer extended (depth,
  textures, descriptors, map pipelines, offscreen target, reports, validation counters). The N0
  triangle path remains (`cat_arena --triangle`). Swapchain now prefers UNORM (was B8G8R8A8_SRGB).
- native/src/main.cpp: map view, offscreen captures/pose sets/reports, feature toggles, fly camera.
- native/include/cat/core.hpp: two input actions (crouch, sprint) appended in key/action order.
- native/CMakeLists.txt: scene/codec/render libraries, 8 shaders, GPU test, asset install.
- docs: PROGRESS, FEATURE_PARITY, TEST_MATRIX, KNOWN_LIMITATIONS rewritten (N1 copies kept in
  history/N1); BUILD.md gained an N2 section.

Data interpretation corrections (N1 bake unchanged)
- World vertex colours: legacy `lightVertexColor` applied at load (13,520 of 13,788 vertices).
- World atlas: runtime capture instead of the repainted canvas (10 of 16 tiles differ).
