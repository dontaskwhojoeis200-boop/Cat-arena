# Native rewrite progress — 2026-11-09

N0 implemented, headless-tested, graphics runtime unverified.
**N1 implemented; content and native loader acceptance verified.**

All four original maps are baked using original WorldKit/compiler/visual routines,
with separate collision and indexed art streams. Callback-driven builders are
consumed explicitly; compiled source links are preserved as references. All four
shipped lightmap hashes match the CPU-rebuilt geometry/chart layouts. Spawns,
triggers, nav metadata, materials, textures, mesh assets, model rig/hitboxes,
lightmaps/PVS and source gameplay constants are exported. Native C++20 readers,
writers, validation CLI and malformed-input tests work without a GPU or JS engine.

Two clean bakes:577 content files plus manifest, byte-identical. Native Debug4/4,
Release4/4, ASan/UBSan4/4 with leak detection disabled; desktop build/headless5/5.
No gameplay/rendering parity claim follows from these data checks. Dynamic A*
construction and native model animation remain future implementation work.

Next milestone N2: use assets/n1/maps/helios-crucible/map.cadata and indexed meshes
in the existing renderer; implement camera/projection/depth and attribute layouts,
textures/material bindings and matched lightmap atlas, preserving Z-up and source
UV conventions. Render the real authored map, not the inventory schematic. Record
real native frames/validation results only if execution succeeds. Continue existing
core/asset tests. Windows/Android/GPU execution remains unverified.
