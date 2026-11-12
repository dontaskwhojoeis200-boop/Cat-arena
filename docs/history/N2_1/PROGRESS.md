# Native rewrite progress — 2026-11-10

N0 implemented (core, SDL3/Vulkan shell). N1 implemented (offline bake, native loaders).
N2 implemented (Helios Crucible renders natively and matches the legacy WebGL2 frame).
**N2.1 implemented: Vulkan 1.2 is the mandatory baseline; 1.3/1.4 features are optional,
detected at run time and each has a 1.2 fallback.** N3 has not been started.
Earlier trackers: docs/history/N0, N1, N2.

## N2.1 result

- Device minimum raised from Vulkan 1.1 to **1.2**; the build no longer needs a 1.4 SDK (pinned
  headers are vendored, only Vulkan 1.0 + WSI symbols are linked, everything newer is resolved at
  run time).
- Run-time detection of loader/device versions, device extensions, core and extension feature
  bits, and host-copy suitability; the result and the reasons are logged and reported
  (`cat_arena --vk-info`).
- Five optional features, each selected independently (core, extension, or fallback): dynamic
  rendering, synchronization2, extended dynamic state, push descriptors, host image copy. The N2
  code (render pass + framebuffers, legacy barriers/submit, static pipelines, dynamic-offset
  uniform set, staging uploads) is the fallback for each and is the complete Vulkan 1.2 path.
- Rendering is unchanged: every path, including a simulated bare Vulkan 1.2 device, reproduces the
  N2 golden frames of all seven poses pixel for pixel, with 0 validation errors.
- N0–N2 functionality, assets, module layout and the 120 Hz core are preserved; `legacy/`,
  `reference/`, `assets/n1` and `assets/n2` are byte-identical to the N2 package.

Details: docs/N2_1_VULKAN_COMPATIBILITY.md. Results: docs/TEST_MATRIX.md. History: docs/DEVELOPMENT_LOG_N2_1.md.

Honest scope: one test device (Mesa lavapipe, Vulkan 1.4 software rasterizer). Vulkan 1.1/1.2/1.3
devices were simulated with a test layer. No hardware GPU, Windows or Android run.

## Status by area

| Area | Status |
|---|---|
| Core / timing / settings / input | N0 complete; unchanged in N2.1 |
| Baked content + loaders | N1 complete; N2 corrections unchanged |
| Helios Crucible rendering | N2 parity preserved on every Vulkan path |
| Vulkan compatibility | **N2.1: 1.2 baseline + optional 1.3/1.4 paths, tested per path** |
| Other three maps | Data loads (N1); rendering not attempted (N4) |
| Portals/mirrors, MD3 models, PVS, effects, flares, dynamic lights | Not implemented (see KNOWN_LIMITATIONS) |
| Physics, gameplay, AI, audio, UI, network, Android | Not started (N3-N9) |
| Windows / Android builds | NOT_RUN (no toolchain/device in runner) |

## Next milestone: N3 — physics and first-person traversal (not started)

Deterministic 120 Hz movement and collision against the baked compiled world, using the N2 window
as the visual harness.
