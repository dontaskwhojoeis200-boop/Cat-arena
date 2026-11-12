# N2 -> N2.1 changes

No bytes changed in legacy/, reference/, assets/n1 or assets/n2 (docs/n2_1-evidence/source-integrity.txt:
593 files, verified against N2_FILE_MANIFEST.json). No shader source changed. `cat_arena_core`,
`cat_arena_assets`, `cat_arena_scene`, `cat_image_codec` unchanged.

Added
- native/include/cat/vulkan_caps.hpp, native/src/vulkan_caps.cpp: capability model and path
  selection (library `cat_arena_vkcaps`, GPU-free).
- native/third_party/vulkan-headers (Khronos C headers 1.4.309, Apache-2.0 OR MIT, PROVENANCE.txt)
  and native/third_party_notices/vulkan-headers.
- native/tests: vkcaps_tests.cpp, path_tests.cpp, vk_device_sim_layer.cpp (test-only layer),
  vk_sim_layer_selftest.cpp, check_vulkan_imports.cmake, vulkan_baseline_imports.txt and
  vk_command_table.inc (both generated from vk.xml).
- tools/n2_1: gen_vk_tables.py, run_path_matrix.sh, compare_golden.py (development/test tools).
- docs: N2_1_VULKAN_COMPATIBILITY.md, DEVELOPMENT_LOG_N2_1.md, N2_1_DIFF.md, n2_1-evidence/,
  history/N2/. README_N2_1.md, N2_1_FILE_MANIFEST.json.

Changed
- native/src/renderer.cpp (455 lines added, 81 replaced): Vulkan 1.2 device minimum; capability
  detection; path selection; Features2 device creation; run-time entry-point resolution;
  synchronization helpers; dynamic rendering, extended dynamic state, push descriptor and host
  image copy paths next to the unchanged N2 fallbacks; report fields.
- native/include/cat/renderer.hpp: constructors take an optional `vk::Request`; `DeviceReport`
  carries detected capabilities, the selected path and object counts. Existing calls compile unchanged.
- native/src/main.cpp: `--vk-info`, `--vk-baseline`, `--vk-max-api`, `--vk-no-extensions`,
  `--vk-disable`, `--resize-test`; `"vulkan"` block in reports.
- native/CMakeLists.txt: no SDK version requirement, pinned headers, loader library linked
  directly, new library and five new tests.
- docs: PROGRESS, TEST_MATRIX, FEATURE_PARITY, KNOWN_LIMITATIONS, BUILD (N2.1 section),
  N2_RENDERER (capability profile), DEPENDENCIES.json.

Behaviour changes
- A device reporting Vulkan 1.1 is no longer accepted.
- On devices that support them, the five optional features are used by default. Output is identical.
