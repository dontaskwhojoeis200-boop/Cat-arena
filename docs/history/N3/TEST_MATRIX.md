# N3 test matrix — 2026-11-10

Environment: docs/n3-evidence/environment.json (Ubuntu 24.04 container, GCC 13.3 and clang 18.1,
2 CPU cores, Mesa lavapipe software Vulkan 1.4.318, validation layer 1.4.309; legacy reference in
Playwright Chromium headless). Logs: docs/n3-evidence. The N2.1 matrix follows below unchanged in
content; its tests were re-run as part of every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest, GCC Debug (10) | PASS 10/10 | core-*.txt |
| Core-only build + CTest, GCC Release -O3 (10) | PASS 10/10 | core-release-*.txt |
| Core-only build + CTest, clang Release (10) | PASS 10/10 | clang-release-*.txt |
| Desktop GCC Debug build (-Werror) + CTest (16) | PASS 16/16, 0 warnings | desktop-debug-*.txt |
| Desktop GCC Release + CTest (16) | PASS 16/16 | desktop-release-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (16) | PASS 16/16 | asan-desktop-*.txt |
| Desktop clang Debug + CTest (16) | PASS 16/16 | clang-desktop-*.txt |
| Physics unit tests (synthetic world): speeds, jump apex/air time, jump buffer, coyote time, step up / too-high block, slope up/down, ceiling, stacked floors, low gap vs crouch collider, wall slide, slide, jump pad, look clamps, view bob/roll, determinism | PASS, 9,157 checks | physics_units.txt |
| Legacy trajectory replay, Helios, 17 tapes, 21,820 ticks, 30 values per tick | PASS: worst difference 1.8e-15 (tolerance 1e-9), 21,725 ticks bit-identical, contact counters equal | physics_legacy_trajectories.txt, legacy_trajectories_capture.json |
| Same native trajectory bits in 7 builds (GCC/clang, Debug/Release, sanitizers) | PASS: identical hash | trajectory-hash-by-build.txt |
| All four maps: collision data loads, every spawn clear and supported, 5 s random play per spawn stays inside | PASS (no legacy reference for 3 maps) | physics_all_maps.txt |
| Headless first-person walk, 1,339 ticks, 13 frames, validation | PASS, 0 errors / 0 warnings | walk/walk.json, walk/walk_sheet.png |
| Window input self-test (Xvfb): SDL key/mouse events through the event path; relative mouse on, yaw change exact, moved, jumped, slid, crouched; auto and Vulkan 1.2 baseline paths | PASS, validation 0 / 0 | window/input_selftest_*.{log,json} |
| N2 golden preview frame after N3 (baseline path) | PASS: byte-identical | n2-golden-check.txt |
| legacy/, reference/, assets/ unchanged vs N2.1 package | PASS, 593 files | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (16) | PASS 16/16 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| Legacy reference trajectories for Bastion Keep, Reactor Bastion, The Arsenal | NOT_RUN | N4 |
| Real keyboard/mouse devices; Windows capture | NOT_RUN | events were synthetic SDL events on Linux/Xvfb |
| Windows x64, Android ARM64, hardware GPU | NOT_RUN | no toolchain/device |
| Cross-platform bit-identical simulation (other C math libraries) | NOT_RUN | Linux glibc only |
| Weapons, enemies, HUD, audio, UI, network | NOT_RUN | not implemented |

# N2.1 test matrix (2026-11-10, results as recorded for N2.1; evidence in docs/n2_1-evidence)

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
