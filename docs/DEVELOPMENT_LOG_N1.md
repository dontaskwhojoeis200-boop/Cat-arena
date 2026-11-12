# N1 development log — 2026-11-09

1. Continued the existing N0 source tree and read N1 acceptance requirements.
   Inspected WorldKit/compiler, map builders, visual/mesh routines, shader parser,
   texture painters, kit placement helpers and lightmap chart construction.
2. Implemented a hash-pinned offline JS conversion harness. It executes actual
   source CPU routines and AST-selected helpers, not copied hand-written geometry.
   Builder callbacks are consumed; unknown function fields fail; compiled identity
   links become explicit index references. Browser/GPU upload paths are not executed.
3. Built binary data/mesh formats and bounded C++20 codecs, validators and a native
   CLI. Added meaningful malformed/truncated/version/overflow/path/index tests.
   Strict compiler warnings caught compressed statement formatting; corrected it
   and compiled successfully without lowering warning settings.
4. Iterative bake probes exposed missing source helper dependencies in the offline
   harness (slab/walkability queries); imported original functions rather than
   stubbing them. Texture validation exposed data-URI prefixes in embedded model
   images; decoded the actual payload and detected the real image type.
5. Baked original kit glow masks and material scripts, retained source material
   routing and all vertex attributes, and ran the original CPU lightmap chart
   preparation. Helios/Bastion/Reactor/Arsenal all matched their shipped hashes.
6. Extracted original Marine GLB/textures/hitboxes unchanged; checked21 joints and
   seven clips; built diffuse glTF inspection exports from40 fitted kit buffers.
   Extracted16 weapon/20 enemy meshes,48 SFX, textures/PVS/lightmaps and gameplay
   declarations. Kept branch provenance; no alternate soundtrack merge.
7. Ran independent Python validation and C++ checks across all maps. Added real
   overlapping-surface assertions and image decoding checks. Created labeled
   schematic previews from exported geometry, not simulated gameplay screenshots.
8. Ran two clean full conversions with the same pinned tools. All577 generated
   content files and final manifest were byte-identical. Kept output hashes and
   source/module/character-archive provenance.
9. Ran Debug4/4, Release4/4, ASan/UBSan4/4 (leaks disabled due to prior runner issue),
   Linux desktop compilation and headless5/5. Source inventory still verifies.
   No Windows/Android/GPU execution was claimed. Updated complete source/docs/data
   package for N2; N0 source and prior docs/evidence remain preserved.
