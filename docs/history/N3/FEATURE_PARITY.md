# Native feature parity — N3

Rendering parity below is measured against the unmodified legacy game captured in headless
Chromium (WebGL2 on Mesa lavapipe) at identical poses; it is not a hardware-GPU or perceptual
sign-off. Previous trackers: docs/history/N2_1, N2, N1, N0. N2.1 and N3 changed no rendering result.

| Feature | Native status | Evidence / remaining |
|---|---|---|
| C++20/CMake/core 120 Hz | Implemented, regression tests pass | — |
| SDL3 window + Vulkan swapchain | **Executed** (Xvfb, lavapipe): FIFO 120 frames, MAILBOX 400 frames, validation clean | N2.1: live resize executed on 6 Vulkan paths. Hardware GPU and Windows untested |
| Vulkan compatibility | **N2.1**: 1.2 baseline; optional dynamic rendering, synchronization2, extended dynamic state, push descriptors, host image copy with 1.2 fallbacks; identical frames on every path | Real 1.2/1.3 drivers and hardware GPUs untested (docs/N2_1_VULKAN_COMPATIBILITY.md) |
| Offscreen Vulkan target + capture | Implemented, used by CI test and parity captures | — |
| Helios world stream | **Matches legacy** (atlas, lightmap, colour-shift, hum, fog, posterize) | Uses runtime world atlas (assets/n2) |
| Helios material surfaces (kits, lamps, coolant, strips, reactor beam/flare/glow) | **Matches legacy** at all 7 poses | — |
| Helios sky + cloud layers | **Matches legacy** (geometry drawn from bake eye; cloud dome/projection ported) | Direct draw instead of 512 px cube map: slightly sharper |
| Depth/projection/winding/culling | Ported; winding verified by normals test | — |
| Lightmap | Shipped atlas, exact bytes, legacy sampling/filtering | Vertex-light mode (lightQuality 0) and light-volume mode not exposed |
| Portal monitor (`$portalfeed`) | Drawn with black feed + scanlines | Live portal camera N10 |
| Mirrors / MD3 models / PVS / flares / dynamic lights / effects / marks | Not implemented | N10 (Helios has no MD3/mirror surfaces) |
| Four maps | Helios rendered; others load (N1) | Bastion Keep, Reactor Bastion, Arsenal N4 |
| Player movement (walk, sprint, crouch, slide, jump, buffer/coyote, air strafe, slopes) | **N3: matches legacy trajectories** on Helios (21,820 ticks, worst difference 1.8e-15) | Sprint toggle/auto modes, touch input N7; blast impulses N5 |
| Stacked floors/ramps/cavities/deck undersides, steps, walls | **N3: collision ported**, contact counters equal to legacy | Other maps: loads + sanity test only, reference N4 |
| Jump pads | **N3: matches legacy** (2 pads) | — |
| Camera bob, run pitch/roll, landing dip, step smoothing, movement FOV | **N3: matches legacy values** | Motion-strength setting N7 |
| Gun bob (weapon motion values) | **N3: values match legacy**; viewmodel not drawn | N5 |
| 16 weapons / 20 enemies / Cat Marine | Data only (N1) | N5/N6 |
| Navigation | Data only; `levelWalkable` ported for vertex light | N3/N6 |
| Audio / UI / settings / HUD | Not started | N7 |
| Four-player multiplayer | Not started | N8 |
| Android ARM64 APK | Not started | N9 |
| Shipping EXE/APK | Not built | N11 |
