# assets/n6 — creature meshes, skeletons, Marine Cat and light volumes

Written by `tools/n6/legacy_enemy_assets.cjs` from the unmodified `legacy/index.html`
(SHA-256 e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393) running in headless
Chromium, plus `assets/n1/models/character/cat_marine_rigged.glb` and `cat_marine_hitboxes.json`
(the N1 copies of the game's files). Nothing is authored here.

| File | Source | How |
|---|---|---|
| `enemies/NN_name.emesh` (20) | `makeEnemyTemplate(type)` = `meshEnemyData` (13020) for the 20 `ENEMY_TYPES`; `ENEMY_RIGS[type]` (`enemyRigFromMesh` 10591) | the Float32Array the page uploads (12 floats per vertex; the fraction of the material id is the limb tag) and the 12 skeleton values the page measured |
| `marine.emesh` | `buildMarineCat()` (13272), `MARINE_RIG` (10614); the GLB skin | runtime mesh as the page draws it; per vertex the dominant GLB joint (JOINTS_0 / WEIGHTS_0) and the hit part of `cat_marine_hitboxes.json` (vertices matched to the GLB, worst distance 0) |
| `light-volume/<map>.png` (4) | the 624 x 234 RGBA texture `bakeStaticLightmap` (5434) uploads for each map | captured from the `texImage2D` call (no flip), written as PNG |
| `enemy-assets.json` | — | settings of the page, per type triangles / bounds / limb counts / skeleton / texture hashes, Marine Cat GLB report (joints, the seven clips and their length, comparison with the runtime mesh, the statement that the runtime does not sample the clips), light-volume bake info, SHA-256 of every file |

The creature textures are not copied: they are `assets/n1/models/enemies/<name>-texture-{0,1,2}.jpg`,
which the tool checks to be byte-identical to the `data:` URLs of `MESHY_ENEMIES`.
The same run writes `native/include/cat/generated/enemy_tables.inc`.

`.emesh`: "CATENM01", u32 vertex count, u32 flags (1 = per-vertex joint and part bytes follow),
f32 rig[16] {hip, knee, shoulder, elbow, neck, spine, wingRoot, height, shoulderX, wingTip, depth,
width, 0, 0, 0, 0}, f32[vertexCount * 12], then (flags & 1) u8 joint[vertexCount], u8 part[vertexCount].
