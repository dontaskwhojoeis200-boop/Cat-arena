# Native feature parity — N2

Rendering parity below is measured against the unmodified legacy game captured in headless
Chromium (WebGL2 on Mesa lavapipe) at identical poses; it is not a hardware-GPU or perceptual
sign-off. Previous trackers: docs/history/N1, docs/history/N0.

| Feature | Native status | Evidence / remaining |
|---|---|---|
| C++20/CMake/core 120 Hz | Implemented, regression tests pass | Gameplay trajectories N3 |
| SDL3 window + Vulkan swapchain | **Executed** (Xvfb, lavapipe): FIFO 120 frames, MAILBOX 400 frames, validation clean | Hardware GPU, resize-under-load and Windows untested |
| Offscreen Vulkan target + capture | Implemented, used by CI test and parity captures | — |
| Helios world stream | **Matches legacy** (atlas, lightmap, colour-shift, hum, fog, posterize) | Uses runtime world atlas (assets/n2) |
| Helios material surfaces (kits, lamps, coolant, strips, reactor beam/flare/glow) | **Matches legacy** at all 7 poses | — |
| Helios sky + cloud layers | **Matches legacy** (geometry drawn from bake eye; cloud dome/projection ported) | Direct draw instead of 512 px cube map: slightly sharper |
| Depth/projection/winding/culling | Ported; winding verified by normals test | — |
| Lightmap | Shipped atlas, exact bytes, legacy sampling/filtering | Vertex-light mode (lightQuality 0) and light-volume mode not exposed |
| Portal monitor (`$portalfeed`) | Drawn with black feed + scanlines | Live portal camera N10 |
| Mirrors / MD3 models / PVS / flares / dynamic lights / effects / marks | Not implemented | N10 (Helios has no MD3/mirror surfaces) |
| Four maps | Helios rendered; others load (N1) | Bastion Keep, Reactor Bastion, Arsenal N4 |
| Stacked floors/ramps/cavities | Rendered; collision not implemented | N3 |
| 16 weapons / 20 enemies / Cat Marine | Data only (N1) | N5/N6 |
| Navigation | Data only; `levelWalkable` ported for vertex light | N3/N6 |
| Audio / UI / settings / HUD | Not started | N7 |
| Four-player multiplayer | Not started | N8 |
| Android ARM64 APK | Not started | N9 |
| Shipping EXE/APK | Not built | N11 |
