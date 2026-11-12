# N2 evidence index

All files were produced in the runner described by environment.json. Nothing here is fabricated;
every number is copied from the generated JSON next to it.

## Builds and tests
core-debug-*, core-release-*, asan-* (ASan+UBSan+LSan; lsan-selfcheck.txt proves LSan works here),
desktop-build/tests (Debug, validation layer active), desktop-release-*, source-integrity.txt,
n1-assets-unchanged.txt.

## Native vs legacy frames (frames/)
- legacy/: unmodified legacy/index.html in headless Chromium, WebGL2 on lavapipe
  (tools/n2/legacy_reference.cjs; state changes listed in legacy_reference.json).
- native/: `cat_arena --offscreen 1280x720 --capture-set tools/n2/helios_poses.txt --time 10`
  (llvmpipe (LLVM 20.1.2, 256 bits), validation layer on: 0 errors, 0 warnings).
- compare/: tools/n2/compare_frames.py — sheets (legacy | native | difference x4) and comparison.json.

| Pose | Mean abs error (R,G,B levels) | PSNR dB | within 8 | within 24 |
|---|---|---|---|---|
| bay0_l2 | [0.092, 0.071, 0.055] | 52.465 | 0.999 | 0.9999 |
| bay0_l3 | [0.169, 0.116, 0.08] | 48.951 | 0.9983 | 0.9996 |
| bay4_l2 | [0.133, 0.101, 0.071] | 51.728 | 0.9993 | 0.9999 |
| bay6_l3 | [0.16, 0.105, 0.069] | 49.709 | 0.9978 | 0.9997 |
| core_l3 | [0.077, 0.058, 0.033] | 50.53 | 0.9989 | 0.9996 |
| preview | [0.083, 0.057, 0.043] | 53.19 | 0.9994 | 0.9999 |
| vault_l1 | [0.055, 0.05, 0.029] | 55.955 | 0.9998 | 1.0 |

- features/ and feature_toggles.png: each renderer stage disabled individually at the preview pose,
  plus lightmap-only, fullbright and normals debug views.

## Window path (window/)
SDL3 X11 window on Xvfb with Vulkan swapchain: FIFO 120 frames (window_run.*), MAILBOX 400 frames
(window_novsync.*), both validation-clean; window_screen.png is the presented frame, byte-identical
to the offscreen preview capture.
