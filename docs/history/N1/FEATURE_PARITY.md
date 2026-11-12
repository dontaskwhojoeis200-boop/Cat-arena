# Native feature parity — N1

Data extraction/validation is distinct from native gameplay and visible rendering.
Original legacy/reference files are byte-preserved. N0 tracker retained in history/N0.

| Feature | Current native status | Remaining |
|---|---|---|
| C++20/CMake/core120Hz | Implemented, regression tests pass | Gameplay trajectories N3 |
| SDL3/Vulkan triangle shell | Linux compiled, headless tested | Actual graphics execution unverified |
| Native data/mesh format and loader | Implemented/tested | GPU consumption N2 |
| Four maps | Actual authored/compiled data and indexed art baked/loaded | Helios rendering N2; other map integration N4 |
| Stacked floors/ramps/cavities | Data, links, bounds/grids preserved and validated | Native collision N3 |
| Lightmaps | Four exact layout/hash matches, atlas PNG and secondary UVs | Native shaders/sample orientation N2/N10 |
| Materials/textures/sky/portals/effects | Source data and geometry/pixels exported | Native rendering behavior N2/N10 |
| 16 weapons | Meshes/textures and source constants exported | Firing/viewmodel/reload N5 |
| 20 enemy types | Meshes/textures, type/health/flyer data exported | AI/waves/native animation N6 |
| Cat Marine | Original GLB21 joints/7 clips/hitbox data extracted | Native skinning/animation/hit volumes N6 |
| Navigation | Authored links/regions/walkable slabs/spatial metadata | Runtime graph/pathfinding N3/N6 |
| Audio | 48 source SFX and original embedded music extracted | Playback/mixer/rights reconciliation N7 |
| Gothic UI/settings/pause/HUD | Original source retained | Native UI N7 |
| Four-player multiplayer | Matching relay unchanged | Native client and actual2/4-client sessions N8 |
| Android lifecycle/ARM64 APK | Not implemented | N9 |
| Shipping Windows EXE/Android APK | Not built | N11 |
