# N1 content formats, schema version 1

All file paths are relative to `assets/n1`, except glTF URIs which are relative
to their containing .gltf. No JavaScript is loaded or executed by native code.

## CATDATA1 (.cadata)

| Byte offset | Type | Meaning |
|---|---|---|
| 0 | 8 ASCII bytes | CATDATA1 |
| 8 | uint32 little-endian | version = 1 |
| 12 | uint32 little-endian | payload byte length |
| 16 | uint32 little-endian | CRC32/IEEE of payload |
| 20 | tagged value | root object |

Value tags: 0=null, 1=false, 2=true, 3=finite IEEE754 float64 LE, 4=UTF8 string
(uint32 byte count then bytes), 5=array (uint32 element count then values),
6=object (uint32 pair count then string key/value pairs). Object keys are sorted
by the baker. Integer-like metadata is represented as exact doubles within the
safe integer range. There are no native pointers, executable callbacks or JS
runtime data structures in the file. The human-readable .json sidecar is tested
for exact semantic equality with the binary by an independent Python decoder.

C++ decoding rejects wrong magic/version/length/CRC, unknown tags, truncated or
trailing bytes, nonfinite numbers, duplicate keys, type errors and exhausted
budgets. File budget256MiB, value-node budget4Mi nodes, nesting limit64. Relative
asset paths reject traversal, absolute paths, Windows drive/backslash forms and
symlink escape. These limits are explicit engineering bounds, not a security
certification. The generic string decoder does not independently validate UTF8;
packaged strings are generated/validated as UTF8 by the offline tools.

## CATMESH1 (.cmesh)

| Byte offset | Type | Meaning |
|---|---|---|
| 0 | 8 ASCII bytes | CATMESH1 |
| 8 | uint32 LE | version = 1 |
| 12 | uint32 LE | floats per vertex: 12, 14 or 18 |
| 16 | uint32 LE | unique vertex count |
| 20 | uint32 LE | index count; multiple of three |
| 24 | uint32 LE | CRC32/IEEE of vertex+index payload |
| 28 | float32 LE array | vertexCount × stride values |
| following | uint32 LE array | indexCount indices |

| Layout | Attribute order |
|---|---|
| 12 | xyz, normal xyz, RGB, UV, original numeric material ID |
| 14 (world) | the 12 above, then lightmap UV |
| 18 (material) | xyz, normal xyz, RGB, alpha, UV, lightmap UV, sprite center xyz, packed corner/half-size |

Deduplication uses all original float32 bit patterns; triangles remain in their
original order. Source normals decide winding through the original CPU routines.
Empty material streams (e.g. Reactor Bastion) are valid empty meshes, not missing
geometry. Index and multiplication bounds are checked before allocation; every
vertex scalar must be finite. C++ encode/decode tests cover round trips and corrupt
headers/payloads. No GPU vertex format is assumed beyond these documented layouts.

## Map schema

`maps/<id>/map.cadata` contains id/wireId/build plus:

- `authored`: actual WorldKit arrays after executing the original builder methods.
  Doorway cutters, frame closures, patch tessellation/collision decomposition,
  stair geometry and kit placements are evaluated by their source CPU functions.
- `compiled`: actual compiler output with spatial grids, collision segments,
  slab planes/thickness, solids/cavities and bounds. References (`src`, `solidRef`,
  `source`) become explicit `{"$ref":"authored.slabs[0]"}`-style index paths.
  Index-based links preserve identity even when legacy IDs are duplicated.
- `level`: original LEVEL compatibility metadata, connections and region data.
- `builderMethodsConsumed`: explicit list of source methods evaluated offline.
  Unknown function-valued fields fail conversion rather than disappearing in JSON.
- `render`: indexed world14/sky12/material18 meshes. `materials` contains contiguous
  index ranges, shader name, original surface/kit/portal identity, and patch
  lightmap parameters. Render mesh collision must not be inferred from these ranges.
- `materialDefinitions`: parsed source material stage/deform/blend/UV/sky/portal
  descriptions. Original .shader scripts also remain under materials/.
- `lightmap`: extracted PNG path, shipped hash/dimensions, recomputed hash/dimensions
  and exact compatibility boolean. All four match in this package. Bind the matched
  atlas with generated secondary UVs; do not substitute world-projected UVs.
- `navigation`: authored connectivity/regions, walkable slab indices and compiled
  spatial metadata. The dynamic A* node/edge graph is explicitly NOT baked.

Static world/lightmap layout uses curveDetail1 (source tolerance0.06m), original
world UV behavior and source shader routing. Material vertices keep source sprite
and deform parameters for later native shader work. Per-image texture coordinates
retain the original convention: CPU image bytes are top-left; legacy WebGL paths
flip unpacked images. N2 must account for this in Vulkan sampling/upload.

## Catalog and reusable assets

`catalog.cadata`/`.json` index all assets; `gameplay/constants.cadata`/`.json`
contain actual WEAPONS, enemy type/health/flyer constants and movement declarations.
These data do not implement their gameplay rules.

Kit position/normal/UV/index binary buffers are byte-preserved from fitted source
payloads (float32/uint16). glTF2.0 inspection exports reference these buffers and
textures; a proper -90° X rotation maps Z-up into glTF Y-up. They are fitted game
assets, not the upstream Meshy GLBs referenced by legacy provenance hashes.
Native material definitions remain authoritative for vertex light/emissive behavior;
the glTF inspection export uses diffuse materials only.

Original Marine GLB and hitbox JSON are byte-extracted from the supplied archive;
no rig, clip, bone or hit-volume redesign occurred. Animated MD3 binaries and part
rigs are separately preserved. PNG/JPEG/WebP/Ogg/MP3 payloads keep their formats;
procedural textures/glow masks are RGBA8. No alternate soundtrack merge occurs.

## Reproducibility and compatibility

The primary HTML SHA256 must match its supplied handoff hash before conversion.
Every inline script hash is recorded. Source module selection is explicit; AST
selection extracts named original declarations/functions. Only offline CPU modules
and deterministic Canvas painters run. GL upload calls are intercepted, never
mistaken for rendering. Source warnings fail the bake. The VM is a compatibility
harness for this trusted, pinned project; it is not a security sandbox for arbitrary
third-party JS.

Clock-dependent conversion diagnostics use a fixed clock and are not benchmark
measurements. Original bake dates/statistics remain provenance metadata. Each output
has a SHA256 in conversion-manifest.json; the manifest excludes itself. Two clean
bakes were compared byte-for-byte on Node24.22.1, acorn8.15.0, canvas0.1.100,
Python3.12.14 (see environment evidence for actual interpreter). Cross-platform
Canvas rasterization bit equality has not been tested.
