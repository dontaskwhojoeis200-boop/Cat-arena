# N2.1 test matrix — 2026-11-10

Environment: docs/n2_1-evidence/environment.json (Ubuntu 24.04 container, GCC 13.3 and clang 18.1,
2 CPU cores, Mesa lavapipe software Vulkan 1.4.318, loader 1.3.275, locally built Khronos
validation layer 1.4.309). Logs: docs/n2_1-evidence. N2 matrix: docs/history/N2/TEST_MATRIX.md;
the N2 legacy-vs-native comparison (docs/n2-evidence) still applies because N2.1 frames are
identical to the N2 native frames.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest, Debug (7) | PASS 7/7 | core-*.txt |
| Core-only build + CTest, Release (7) | PASS 7/7 | core-release-*.txt |
| Desktop Debug build, GCC, -Werror | PASS, 0 warnings | desktop-debug-build.txt |
| Desktop Debug CTest (13) | PASS 13/13 | desktop-debug-tests.txt |
| Desktop Release build + CTest (13) | PASS 13/13 | desktop-release-*.txt |
| Desktop ASan + UBSan + LeakSanitizer, CTest incl. all GPU path tests (13) | PASS 13/13 | asan-desktop-*.txt (N2 ran sanitizers on the GPU-free tests only) |
| Desktop Debug with clang 18, -Werror, CTest (13) | PASS 13/13 | clang-desktop-*.txt |
| Build with a Vulkan 1.2.198 SDK header directory + GPU tests | PASS | sdk-1.2.198-headers-*.txt |
| Path selection rules, synthetic devices (GPU-free) | PASS, 69 checks | vulkan_path_selection.txt |
| Path matrix on the installed driver, 17 requests: selection, objects, identical frames, validation | PASS 17/17, 0 validation errors, 0 warnings | render_path_matrix.txt |
| Device-simulation layer self-test | PASS, 31 checks | vk_sim_layer_selftest.txt |
| Simulated devices: bare 1.2, 1.2+extensions, extension without bit, bare 1.3, 1.3 without bits, 1.4 without hostImageCopy, 1.4 without optional bits, 1.4 with unresolvable entry points, 1.1 rejected | PASS 11/11, 0 validation errors (2 loader notices per run about the test layer) | render_path_simulated_devices.txt |
| Linked Vulkan symbols are 1.0 + WSI only (library and executable) | PASS, 89 imports; negative check fails as intended | vulkan_link_imports*.txt, cat_arena_vulkan_imports.txt |
| 1280x720, 7 poses, 17 rendering configurations vs N2 golden frames | PASS: 7/7 pixel-identical in every configuration | matrix/matrix_summary.md, matrix/*/report.json |
| Simulated Vulkan 1.1 device (CLI) | PASS: rejected with "No Vulkan 1.2+ device" | matrix/sim_vk11_rejected/run.log |
| N2 offscreen render test (preview sanity, determinism, feature toggles, winding, validation) on the auto path | PASS | render_helios_offscreen.txt |
| Window (SDL3 X11, Xvfb), FIFO 120 frames with live resize 1280x720 -> 960x540, 6 paths | PASS, 0 errors / 0 warnings each | window/*_fifo.* |
| Window MAILBOX (`--no-vsync`), 300 frames, 6 paths | PASS, 0 / 0 each | window/*_novsync.* |
| Presented window image == offscreen capture (baseline, auto) | PASS (max difference 0) | window/*_screen.png |
| Swapchain recreation under live resize | PASS (first execution; NOT_RUN in N2) | window/*_fifo.log |
| N0/N1/N2 regression (core, assets, baked_maps for 4 maps, scene units, Helios scene) | PASS | included in every CTest run above |
| legacy/, reference/, assets/n1, assets/n2 unchanged vs N2 package | PASS | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (13), baseline frame == golden | PASS 13/13 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| Real Vulkan 1.2 / 1.3 driver | NOT_RUN | simulated only |
| Hardware GPU (any vendor) | NOT_RUN | software rasterizer only |
| Depth+stencil formats on the dynamic-rendering path | NOT_RUN | lavapipe selects D32_SFLOAT |
| Windows x64 build/run | NOT_RUN | no toolchain |
| Android ARM64 | NOT_RUN | N9; no NDK/device |
| Performance / FPS | NOT MEASURED | CPU rasterizer; object counts reported instead |
| Physics, gameplay, AI, audio, UI, network | NOT_RUN | not implemented |
