# Native migration log

| Tier | Result |
| --- | --- |
| N7 | Preserved the submitted ZIP. Rebuilt it locally and passed all 35 GPU-free checks. |
| N8 | Verified the existing native co-op implementation at GitHub commit `a0fd083bd4f8d78bdbf81e19b9b19a8796a23d14`. Windows CI passed 42 GPU-free checks and packaged the executable. Local Linux verification passed 39 GPU-free tests and two offscreen software-Vulkan tests. |
| N9 | Implemented Windows display, mouse and keyboard options, focus and audio recovery, and a Vulkan 1.1 fallback. Windows CI run `43425587347` compiled and packaged the executable, passed 42 GPU-free tests and ran the headless smoke test. Linux core and software-Vulkan checks passed. Physical Windows verification remains open. |
| N10 | Co-op projectile, remote gunfire and scoring fixes, combat HUD, and lighting and effects controls. Windows CI run `43460222820` passed 42 GPU-free tests and the packaged headless smoke test. Linux passed 41 GPU-free and 30 software-Vulkan tests in full runs. See the [N10 test report](N10_TEST_REPORT.md) and [acceptance matrix](N10_ACCEPTANCE.md) for remaining work and hardware checks. |

The [Windows roadmap](WINDOWS_ROADMAP.md) governs N8–N20. For the N8 feature
list, commands, and outstanding hardware checks see [README_N8](../README_N8.md).
