# N2 native map renderer — design and legacy mapping

N2 renders the real baked `helios-crucible` map (N1 `assets/n1`) with Vulkan. It is a static-scene
milestone: no movement physics, collision, enemies, weapons, effects, HUD or audio. The interactive
window has a debug fly camera only.

## Module layout

| Module | Depends on | Responsibility |
|---|---|---|
| `cat_arena_core` | C++ std | N0 settings, clock, input, logging (unchanged except two input actions) |
| `cat_arena_assets` | C++ std | N1 CATDATA1/CATMESH1 codecs and map validation (unchanged) |
| `cat_image_codec` | stb (vendored) | PNG/JPEG decode, PNG encode; isolated from `-Werror` |
| `cat_arena_scene` | assets, image codec | GPU-independent scene preparation: material model, camera, clusters, textures, legacy vertex light |
| `cat_arena_render` | scene, SDL3, Vulkan | Vulkan renderer: window (swapchain) and offscreen (readback) targets |
| `cat_arena` | render | Application: offscreen captures, interactive window |

Simulation code does not include Vulkan or SDL. The renderer consumes an immutable `MapScene`
(uploaded once) and a per-frame `FrameView {camera, time, features}`.

## Frame order (legacy `render()`, index.html 13572-13662)

1. Clear colour = `THEMES[0].sky` (Helios is loaded with mapIndex `(0+3)%3 = 0`), depth 1.
2. **Sky geometry** (`sky.cmesh`, the legacy `sunMesh`) drawn from the fixed sky eye
   `(WORLD.meta.center, z=2)` with the current view rotation, then depth is cleared.
   The legacy bakes this geometry once into a 512 px cube map from that same eye and draws the
   box at the far plane; drawing the geometry directly is the same image without cube-map
   resampling (sharper; see limitations). Sky colour is the raw vertex colour (`isGun==6`).
3. **World stream** (`world.cmesh`, layout 14) with the legacy main shader's static branch:
   16-layer world atlas (`texture(atlas, vec3(uv, material))`, alpha test 0.36), per-pixel lightmap
   `colorShift(lm.rgb*1.90*lmIntensity*mapOverbright*1.19)/1.19`, emissive/hum for materials 4/5,
   exterior-weighted atmospheric fog, 55-level posterize. Cull back, depth LESS.
4. **Portal clusters** then **opaque material clusters** (Q3MAT `drawStatic('opaque')`): clusters are
   rebuilt exactly as `buildStatic` (material + 10x10x6 m centroid cell, portal origins own
   clusters, stable sort by `(sort, id)`), consecutive same-material clusters share state and
   contiguous index runs merge.
5. **Sky cloud layers** (`sky/reactor_bastion` stages) on the legacy 12x24 dome with
   `setSkyDomeColors` vertex colours and Q3 cloud projection, at the far plane, depth LEQUAL.
6. **Translucent clusters**, stable-sorted by `(sort, farthest first)`.

Clusters are culled with an exact port of the legacy `inView` cone test. PVS (`Q3VIS`) is not used
in N2 (it only removes hidden geometry; output is unchanged, cost is higher).

## Material stages (Q3MAT, index.html 3643-4462)

`materialDefinitions` are parsed into a typed model (`scene::Material/Stage`) covering every field
present in all four maps: blend codes 0-10, alphaFunc GT0/LT128/GE128, depthWrite/EQUAL,
rgbGen identity/identityLighting/const/wave/entity/vertex/exactVertex/oneMinusVertex/lightingDiffuse,
alphaGen identity/const/wave/vertex/oneMinusVertex/lightingSpecular/portal, tcGen
base/lightmap/environment/vector, tcMod scroll/scale/rotate/stretch/transform/turb, deforms
wave/bulge/move/normal/autosprite/autosprite2, animMap, detail stages, sky parameters.
Unknown values fail loading. Stage collapse (`canCollapse`), fog modes (`fogModeFor`), pipeline
state (`applyState`), the per-frame CPU evaluation of tcMod matrices and rgbGen/alphaGen
constants (`stageTexMatrixInto`, `stageColorInto`) and the GLSL stage evaluation are direct ports.
Texture sources: 256 px images in the material array, 512 px in the kit array
(`registerKitImage`), `$world:N` world-atlas layers, `$lightmap`, `$whiteimage`,
`$vertexlight` (kit models: lightmap texel per vertex x lightScale 2.508).

## Projection and orientation

Legacy GL: `gl_Position = (dot(r,right)/(tanHalf*aspect), dot(r,up)/tanHalf, depth*1.00025003-0.1000125, depth)`
(near 0.05, far 400). Native Vulkan uses `z = depth*1.000125015-0.05000625`, which yields the
identical window depth, and negates Y. The legacy camera basis is kept exactly
(`right = (-sin a, cos a, 0)`), so the image orientation matches WebGL. The legacy culls back faces
with `frontFace(CW)`; with negated Y and Vulkan's facing formula this is `VK_FRONT_FACE_CLOCKWISE`.
(The first N2 build used COUNTER_CLOCKWISE; the normals-based winding test caught every floor top
being culled. See DEVELOPMENT_LOG_N2.md.)

## Texture orientation

GL and Vulkan both address the first uploaded row at t=0, so native uploads reproduce the legacy
byte order: world atlas layers are row-flipped per layer (`rebuildWorld`), the lightmap, material
and kit arrays are uploaded unflipped (`UNPACK_FLIP_Y=false`). Formats are UNORM (no sRGB decode),
and the swapchain prefers `B8G8R8A8_UNORM`, because the WebGL canvas stores shader output
unconverted; an `_SRGB` swapchain would wash colours out.

## Data correction: runtime world atlas

The legacy world samples the texture array built once at startup (`Q3R.applyTextures()`, line 2996)
from the original RETROTECH tiles. `paintWorldMaterials()` (line 8980) repaints ten tiles of
`texCanvas` later but never re-uploads the array. N1 baked the repainted canvas, so N2 loads
`assets/n2/world-atlas-runtime.png` (read back from the running legacy; see assets/n2/PROVENANCE.md)
via `SceneOptions::runtime_world_atlas`. Without it the loader warns and the floors/walls differ.

## Data correction: legacy world vertex lighting

The legacy `bakeMap()` calls `lightVertexColor(g)` on the world stream (index.html 5226-5262)
before lightmap charting. N1 captured the stream before that call. N2 reproduces it exactly from
baked collision data: `mapCell` uses `levelWalkable = worldInFootprint && !worldInMassive` on the
117x117 grid, `buildings` is empty for WorldKit maps. On Helios 13,520 of 13,788 world vertices
change (factor 0.6241-1.0). Values are recomputed from float32 vertex data, so results can differ
from the double-precision original by at most one float ulp per channel.

## Capability profile

Superseded by N2.1: see docs/N2_1_VULKAN_COMPATIBILITY.md. Vulkan **1.2** device (was 1.1), graphics
queue (plus present + swapchain for the window), a depth format from `D32_SFLOAT, D32_SFLOAT_S8,
X8_D24, D24_S8, D16` (warning if only D16), `R8G8B8A8_UNORM` sampled 2D/2D-array images with optimal
tiling. Optional: `samplerAnisotropy` (normal preset 4x, else 1x) and the five N2.1 features
(dynamic rendering, synchronization2, extended dynamic state, push descriptors, host image copy).
Mip chains are generated on the CPU (deterministic). Everything described above in this document
is identical on every Vulkan path.
