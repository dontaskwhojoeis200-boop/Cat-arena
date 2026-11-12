# Cat Arena: Feral Claw — N1 asset/data milestone

N1 implemented and tested, 2026-11-09. Continue from this complete project.
N0 source, tests, renderer and original references remain included.

**This is a validated content bake and native loader, not yet a playable native game.**
The N0 window still draws its triangle. N2 connects Helios Crucible to Vulkan.

## What is included

- All four source map builders executed offline, with explicit authored and
  compiled collision data, spatial grids, source-object references, stacked slabs,
  ramps, cavities, spawns, pickups, jump pads, lights and navigation metadata.
- Actual source-generated indexed visual meshes, separate from collision;
  material stage definitions, portal/MD3/kit placements and secondary lightmap UVs.
- **All four lightmap layout hashes exactly match the shipped atlases.**
- 16 weapon meshes, 20 enemy meshes, 40 fitted kit meshes with glTF inspection
  exports, original Cat Marine GLB/hitboxes/textures (21 joints, seven clips).
- Embedded/procedural textures, emissive masks, effects atlases, 48 Ogg SFX,
  original embedded music, four lightmap PNGs and four visibility sets.
- Native-readable gameplay constants, a versioned binary format, C++20 readers
  and writers, a map-validation CLI and malformed-file tests.
- 578 output files including the conversion manifest. Two clean bakes produced
  identical bytes for the 577 files it tracks.

## Start

```sh
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core
ctest --test-dir build-core --output-on-failure
./build-core/cat_arena_asset_check assets/n1
```

Prebaked data is included: these commands do not need Node, npm, a browser or GPU.
See docs/BUILD.md to rebuild data; docs/N1_FORMAT.md defines the binary layouts.

Verification: Debug 4/4, Release 4/4, ASan/UBSan 4/4 with leak checking disabled,
Linux desktop build and 5/5 headless CTest targets. No Windows/Android build or
native graphical/map gameplay verification is claimed. See docs/n1-evidence.

Preview inventory: docs/N1_INVENTORY.md and docs/N1_MAP_PREVIEW.png. The preview is
an exact geometry schematic, not a native-rendered screenshot. Source/conversion
changes and reproducibility details: docs/N1_DIFF.md and DEVELOPMENT_LOG_N1.md.

Next: **N2 — render the extracted Helios Crucible map in Vulkan**, preserving
world/material mesh layouts, per-surface UVs, textures, depth and Z-up transforms.
Do not restart from the old reference ZIP or substitute a generic room.
