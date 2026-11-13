# Native feature parity — N10 checkpoint

Rendering parity below is measured against the unmodified legacy game captured in headless
Chromium (WebGL2 on Mesa lavapipe) at identical poses; it is not a hardware-GPU or perceptual
sign-off. Previous trackers: docs/history/N3, N2_1, N2, N1, N0. N2.1 and N3 changed no rendering result.

| Feature | Native status | Evidence / remaining |
|---|---|---|
| Four maps: geometry, materials, lightmaps, skies | **N4**: Bastion Keep, Reactor Bastion match the legacy (44-64 dB); The Arsenal matches with its image sky box (48 dB) | The Arsenal's 3 MD3 models and portal/mirror surfaces not drawn |
| Collision on every map | **N4**: legacy trajectories match on all four maps (62,780 ticks in total) | — |
| Stacked navigation (graph, A*, smoothing) | **N4**: identical to the legacy on all four maps; **N6**: sliced route jobs, queue, shared routes match the legacy tapes | Route budget: 110 expansions only (legacy also cuts at 1.8 ms wall time, see N6_ENEMIES.md §3) |
| Rooms / sectors | **N4**: region lookup from baked data | Not used for culling; no legacy reference |
| Map selection, internal ids | **N4**: `--map`, `--list-maps`, keys 1-4; ids and wire ids preserved; **N7: maps page with the live preview from the legacy cameras, MAP ↻ button** | — |
| C++20/CMake/core 120 Hz | Implemented, regression tests pass | — |
| SDL3 window + Vulkan swapchain | **Executed** (Xvfb, lavapipe): FIFO 120 frames, MAILBOX 400 frames, validation clean | N2.1: live resize executed on 6 Vulkan paths. Hardware GPU and Windows untested |
| Vulkan compatibility | **N9**: Vulkan 1.1 baseline; Vulkan 1.2–1.4 optional features remain capability-gated; render matrix and simulated 1.1 test pass | Physical legacy Windows driver testing pending |
| Offscreen Vulkan target + capture | Implemented, used by CI test and parity captures | — |
| Helios world stream | **Matches legacy** (atlas, lightmap, colour-shift, hum, fog, posterize) | Uses runtime world atlas (assets/n2) |
| Helios material surfaces (kits, lamps, coolant, strips, reactor beam/flare/glow) | **Matches legacy** at all 7 poses | — |
| Helios sky + cloud layers | **Matches legacy** (geometry drawn from bake eye; cloud dome/projection ported) | Direct draw instead of 512 px cube map: slightly sharper |
| Depth/projection/winding/culling | Ported; winding verified by normals test | — |
| Lightmap | Shipped atlas, exact bytes, legacy sampling/filtering | Vertex-light mode (lightQuality 0) and light-volume mode not exposed |
| Portal monitor (`$portalfeed`) | Drawn with black feed + scanlines | Live portal camera N10 |
| Mirrors / MD3 models / PVS / flares | Not implemented | N10; effects, marks and dynamic lights are done (N5, N5.1) |
| Player movement (walk, sprint, crouch, slide, jump, buffer/coyote, air strafe, slopes) | **N3: matches legacy trajectories** on Helios (21,820 ticks, worst difference 1.8e-15); **N7: sprint hold / toggle / auto, touch stick (legacy touchStick term)** | Touch stick not compared with a legacy tape |
| Stacked floors/ramps/cavities/deck undersides, steps, walls | **N3: collision ported**, contact counters equal to legacy | Other maps: loads + sanity test only, reference N4 |
| Jump pads | **N3: matches legacy** (2 pads) | — |
| Camera bob, run pitch/roll, landing dip, step smoothing, movement FOV | **N3: matches legacy values**; **N7: motion strength and FOV settings** | — |
| Gun bob (weapon motion values) | **N3: values match legacy**; **N5: drawn** | — |
| 16 weapons: magazine, cadence, reload, pellets, spread, range, hitscan / rocket / grenade / rift | **N5: match legacy tapes** (18,220 ticks bit-identical, Helios) | Other maps not compared |
| Hit boxes, headshots, frag reroll, splash damage and falloff | **N5: match legacy** against static targets; **N6: on real enemies, four maps** | — |
| Rocket / grenade jump, blast knockback, shotgun recoil and air jump | **N5: match legacy** | — |
| Medkits (rules) | **N5: match legacy**; **N7: pickup beep** | — |
| Death and respawn | **N6: match legacy tapes** (scored spawn choice, wave restart, projectiles cleared, protection) | — |
| First-person weapon models (16), viewmodel camera, recoil spring, raise, reload dip | **N5: models from the legacy data; camera within 1e-16; views 45-47 dB**; **N7: calibration sliders** | — |
| Muzzle flash, tracers, rail trail, lightning, projectile sprites, impacts, explosions, marks, brass | **N5: every quad equal to the legacy's** (counts and checksums); **N6: started in the legacy's event order** | — |
| Dynamic lights (flashes, projectiles, explosions) on world and material surfaces | **N5.1: selection equal to the legacy on every recorded tick; frames 41-59 dB**; **N6: on creatures and Marine Cats too** | Flares not implemented |
| Medkit billboard | **N5.1: matches legacy** (frame 57 dB) | — |
| HUD: health, ammo, weapon, score, crosshair, hit marker | **N7: legacy layout and look** (measured boxes, fonts, 8 crosshairs, event box, touch layout, editor, HUD scale, toggles, perf overlay); **N10:** damage numbers, incoming hits and downed notice | Guest incoming hit amounts come from authoritative health changes. Direction is available for solo and host hits when the source is known. N5 HUD is used in traversal captures. |
| 20 enemy types: stats, scaling, waves, spawning (20 alive, 0.2 s), 30 Hz decisions, stride cadence | **N6: match legacy tapes** (24 tapes, 48,000 ticks, four maps; within 1.5e-14) | — |
| Ground AI routes, flyers (swept 3-D steering), attacks (wind-up / strike / recovery), enemy projectiles, melee, burn | **N6: match legacy tapes** | — |
| Bodies (three death variants, flyer fall, sink), gibs at -40 hp | **N6: match legacy** (animation fixture, staged scene 52 dB) | No loot: the legacy has none |
| Enemy animation (gait, look-at, attack envelopes, pain), rig shader | **N6: poses within 1.2e-13 of the legacy every tick** | — |
| Creature rendering: textures, light volume, dynamic lights, sheen, fog; final-five outline; blob / projected shadows | **N6: frames 42.5-63.6 dB, staged scenes 48.5-52.8 dB** | Helios pictures only (other maps: data compared) |
| Enemy effects (projectile looks and trails, casting glows, melee arcs, muzzle flashes, impacts, blood, gibs) | **N6: cosmetic draws equal to the legacy on every tick**; **N7: voices, attacks, deaths audible (legacy limits)** | — |
| Cat Marine model, skin joints, hit parts | **N6: runtime mesh = shipped GLB vertices; joints and 6 parts per vertex; posed as the legacy (soldier gait + offsets)** | The 7 GLB clips: not used by the legacy runtime, not used natively. Mirror view N10; networked players N8 |
| Menus: home, maps, loading, pause, exit, multiplayer page, HUD editor | **N7: legacy routing, layout and look** (captures in docs/n7-evidence) | N8 adds host setup, browser, connecting and co-op pause |
| Settings (65 legacy controls) | **N7: same defaults / ranges / options**; keyboard-only navigation tested; **N10:** fog, lightmap intensity and overbright affect gameplay rendering | Internal scene resolution, AA, culling modes, texture and effect detail remain open |
| Audio: samples, music, mix, compressor, voices, beeps | **N7: Web Audio graphs match Chromium (RMS 6e-8 … 5e-2); legacy rules tested** | Not heard on real hardware; sound uses its own random stream |
| Input modes: keyboard / mouse, touch, gamepad, pointer capture | **N7: implemented; capture and mode rules unit-tested** | N9 adds Windows movement layouts, mouse options and focus handling. Physical hardware validation is open. |
| Four-player multiplayer | N8 implemented and tested on Windows loopback. N10 adds host-authoritative explosive flight and detonation visuals and attributes eliminations to the shooter. | Separate PCs, WAN/TLS and real GPU play remain unverified |
| Android ARM64 APK | Historical target | Windows 10/11 x64 is the active release target |
| Windows executable | Packaged Windows checkpoints start in GPU-free CI | Physical Windows launch and the N11 hardware benchmark remain open |
