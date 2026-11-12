# Native rewrite progress — 2026-11-09

N0 implemented (core, SDL3/Vulkan shell). N1 implemented (offline bake, native loaders).
**N2 implemented: Helios Crucible renders natively in Vulkan and matches the legacy WebGL2 frame.**
Earlier trackers: docs/history/N0, docs/history/N1.

## N2 result

The real baked `helios-crucible` map (world stream, 404 material clusters, sky geometry, cloud
layers, lightmap atlas, 33 kit/material textures) renders through `cat_arena_render`, both in an
SDL3 window (swapchain) and offscreen (CI captures). Legacy rules were ported from source, not
approximated: projection/depth, camera basis and winding, world shader (atlas, lightmap,
colour-shift, emissive hum, exterior fog, posterize), the full Q3MAT stage system (collapse,
multipass blending, tcMod/rgbGen/alphaGen/tcGen, autosprites, deforms, animMap, fog modes), cluster
build/sort/culling and the frame order.

Parity measurement (same software rasterizer for both): the unmodified legacy game was run in
headless Chromium and captured at seven poses (legacy menu preview + spawn points on floors -4, 0,
7); native Vulkan captures of the same poses differ by a mean of **0.03-0.17 levels per channel,
PSNR 49-56 dB, >= 99.78% of pixels within 8 levels** (docs/n2-evidence/frames/compare).
Validation layer: 0 errors, 0 warnings (offscreen, window FIFO and window MAILBOX runs).

Getting there exposed two N1 data issues (bake unchanged, corrected for N2):
1. World vertex colours lacked the legacy `lightVertexColor` factor; N2 recomputes it exactly.
2. N1's world atlas is the repainted `texCanvas`, but the running game's world array holds the
   original tiles (captured before the repaint). N2 uses `assets/n2/world-atlas-runtime.png`,
   read from the legacy GPU and audited texture by texture (assets/n2/texture_audit.json).

## Status by area

| Area | Status |
|---|---|
| Core / timing / settings / input | N0 complete; N2 added crouch/sprint actions for the fly camera |
| Baked content + loaders | N1 complete; N2 corrections documented above |
| Helios Crucible rendering | **N2 implemented and parity-measured** on lavapipe |
| Other three maps | Data loads (N1); rendering not attempted (N4). Material parser covers every field used by all four maps |
| Portals/mirrors, MD3 models, PVS, effects, flares, dynamic lights | Not in N2 (monitor shows black feed; see KNOWN_LIMITATIONS) |
| Physics, gameplay, AI, audio, UI, network, Android | Not started (N3-N9) |
| Windows / Android builds | NOT_RUN (no toolchain/device in runner) |

## Next milestone: N3 — physics and first-person traversal

Deterministic 120 Hz movement and collision against the baked compiled world (stacked slabs,
ramps, cavities, walls), using the N2 window as the visual harness. N2's `WalkGrid` already ports
`levelWalkable`/footprint/massive-solid queries that N3 collision will extend.
