# N2 test matrix — 2026-11-09

Environment: docs/n2-evidence/environment.json (Ubuntu 24.04 container, GCC 13.3, 2 CPU cores,
Mesa lavapipe software Vulkan 1.4.318, locally built Khronos validation layer). Logs: docs/n2-evidence.

| Check | Result | Evidence / scope |
|---|---|---|
| Core + scene, Debug (6 CTest) | PASS 6/6 | core-debug-tests.txt |
| Core + scene, Release (6) | PASS 6/6 | core-release-tests.txt |
| Core + scene, ASan + UBSan + LeakSanitizer (6) | PASS 6/6 | asan-tests.txt; LSan proven active by lsan-selfcheck.txt (N1 could not run LSan) |
| Desktop Debug build, -Werror | PASS, 0 warnings | desktop-build.txt |
| Desktop Debug CTest (8 incl. GPU test) | PASS 8/8 | desktop-tests.txt (validation layer active) |
| Desktop Release build + CTest | PASS | desktop-release-*.txt |
| Scene units: waves/noise golden values, tcMod order, camera/depth mapping, atlas flip, mips, material parse/validation, vertex-light golden values | PASS | scene_units |
| Helios scene preparation (counts, cluster order/coverage, layer bounds, vertex light range) | PASS | helios_scene |
| Offscreen render: preview frame sanity, byte determinism, each feature toggle changes pixels, winding (normals), animation, validation | PASS, 0 validation errors/warnings | render_helios_offscreen |
| Window (SDL3 X11, Xvfb) FIFO, 120 frames, validation | PASS, 0/0 | window/window_run.* |
| Window MAILBOX (`--no-vsync`), 400 frames, validation | PASS, 0/0 | window/window_novsync.* |
| Presented window image == offscreen capture | PASS (MAE 0.0) | window/window_screen.png |
| Legacy vs native, 7 poses | PASS: MAE 0.03-0.17, PSNR 49-56 dB, >=99.78% within 8 levels | frames/compare/comparison.json |
| Legacy runtime texture audit (world array, 45 material, 30 kit, lightmap) | Recorded | assets/n2/texture_audit.json |
| stb JPEG decode vs browser-equivalent decode (30 kit textures) | MAE 0.0097, max 3 | DEVELOPMENT_LOG_N2.md |
| Legacy/reference/N1 assets unchanged | PASS | source-integrity.txt, n1-assets-unchanged.txt |
| N0/N1 regression (core, assets, baked_maps for 4 maps) | PASS | included above |
| Swapchain recreation under live resize | NOT_RUN | no window manager input in Xvfb; code path from N0 |
| Hardware GPU (any vendor) | NOT_RUN | software rasterizer only |
| Windows x64 build/run | NOT_RUN | no toolchain |
| Android ARM64 | NOT_RUN | N9 |
| Performance / FPS | NOT MEASURED | lavapipe CPU rendering is not representative; no numbers claimed |
| Physics, gameplay, AI, audio, UI, network | NOT_RUN | not implemented |
