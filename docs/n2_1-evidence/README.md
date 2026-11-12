# N2.1 evidence (2026-11-10)

Everything here was produced by the commands in docs/BUILD.md (N2.1 section) on the environment in
environment.json. Paths inside logs are runner paths.

Builds and CTest (clean builds)
- core-*, core-release-*: GPU-free build, 7 tests.
- desktop-debug-*, desktop-release-*: GCC, 13 tests.
- asan-desktop-*: ASan + UBSan + LeakSanitizer, 13 tests including all GPU path tests.
- clang-desktop-*: clang 18, 13 tests.
- sdk-1.2.198-headers-*: configured with a Vulkan 1.2.198 header directory as the "SDK" headers.
- fresh-extract-*: configure/build/test of the extracted delivery ZIP (see the note in that file).

Vulkan path tests (stdout of the test programs)
- vulkan_path_selection.txt: capability model unit tests.
- render_path_matrix.txt: 17 requests on the installed driver; one PASS line each with the selected
  source per feature, object counts and validation counts.
- vk_sim_layer_selftest.txt: proof that the simulation layer hides what it claims to hide.
- render_path_simulated_devices.txt: simulated 1.1/1.2/1.3/1.4 devices.
- vulkan_link_imports.txt, cat_arena_vulkan_imports.txt: the 89 Vulkan symbols the executable
  imports; vulkan_link_imports_negative_check.txt: the same test failing on purpose when
  `vkQueueSubmit` is removed from the allow list.
- render_helios_offscreen.txt: the N2 render test on the automatically selected path.
- vk-info-*.json: `cat_arena --vk-info` for auto, `--vk-baseline` and `--vk-max-api 1.2`.

Golden-frame matrix (matrix/)
- matrix_summary.md / .json: 18 configurations at 1280x720, seven poses each, compared with
  docs/n2-evidence/frames/native. Per configuration: report.json (capabilities, path, objects,
  per-frame draw statistics, validation counts) and the Vulkan lines of the run log.
- preview.png for auto, baseline and sim_vk12_bare (file-identical to the N2 golden preview);
  the other frames are not stored because they are pixel-identical to the golden ones.

Window runs (window/)
- <path>_fifo.*: 120 frames, FIFO, live resize 1280x720 -> 960x540 at frame 60.
- <path>_novsync.*: 300 frames, MAILBOX.
- baseline_screen.png, auto_screen.png: the presented window, captured from the X server;
  identical to the offscreen capture of the same pose.

source-integrity.txt: legacy/, reference/ and assets/ against the N2 manifest.

The two "validation warnings" counted in simulated-device runs are Vulkan loader notices that the
test layer was enabled through VK_INSTANCE_LAYERS; no validation-layer warning or error occurred.
