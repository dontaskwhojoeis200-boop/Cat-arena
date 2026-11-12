# Native feature parity — N4

Rendering parity below is measured against the unmodified legacy game captured in headless
Chromium (WebGL2 on Mesa lavapipe) at identical poses; it is not a hardware-GPU or perceptual
sign-off. Previous trackers: docs/history/N3, N2_1, N2, N1, N0. N2.1 and N3 changed no rendering result.

| Feature | Native status | Evidence / remaining |
|---|---|---|
| Four maps: geometry, materials, lightmaps, skies | **N4**: Bastion Keep, Reactor Bastion match the legacy (44-64 dB); The Arsenal matches with its image sky box (48 dB) | The Arsenal's 3 MD3 models and portal/mirror surfaces not drawn |
| Collision on every map | **N4**: legacy trajectories match on all four maps (62,780 ticks in total) | — |
| Stacked navigation (graph, A*, smoothing) | **N4**: identical to the legacy on all four maps | Job queue / route sharing with enemies N6 |
| Rooms / sectors | **N4**: region lookup from baked data | Not used for culling; no legacy reference |
| Map selection, internal ids | **N4**: `--map`, `--list-maps`, keys 1-4; ids and wire ids preserved | Menu UI N7 |
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
| Gun bob (weapon motion values) | **N3: values match legacy**; **N5: drawn** | — |
| 16 weapons: magazine, cadence, reload, pellets, spread, range, hitscan / rocket / grenade / rift | **N5: match legacy tapes** (18,220 ticks bit-identical, Helios) | Other maps not compared |
| Hit boxes, headshots, frag reroll, splash damage and falloff | **N5: match legacy** against static targets | Real enemies N6 |
| Rocket / grenade jump, blast knockback, shotgun recoil and air jump | **N5: match legacy** | — |
| Medkits | **N5: match legacy** | Sound N7 |
| Death and respawn | N5: implemented, unit-tested | Not compared with legacy; spawn choice uniform |
| First-person weapon models (16), viewmodel camera, recoil spring, raise, reload dip | **N5: models from the legacy data; camera within 1e-16; views 45-47 dB** | Calibration sliders fixed at defaults |
| Muzzle flash, tracers, rail trail, lightning, projectile sprites, impacts, explosions, marks, brass | **N5: every quad equal to the legacy's** (counts and checksums) | Dynamic lights on the map not applied; death effects N6 |
| HUD: health, ammo, weapon, score, crosshair, hit marker | **N5: native layout, built-in font** | Legacy DOM layout, damage numbers, incoming-hit indicator not reproduced |
| 20 enemies / Cat Marine | Data only (N1) | N6 |
| Navigation | Data only; `levelWalkable` ported for vertex light | N3/N6 |
| Audio / UI / settings | Not started | N7 |
| Four-player multiplayer | Not started | N8 |
| Android ARM64 APK | Not started | N9 |
| Shipping EXE/APK | Not built | N11 |
