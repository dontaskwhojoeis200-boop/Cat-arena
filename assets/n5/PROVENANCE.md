# assets/n5 — weapon models and effect atlas

Written by `tools/n5/legacy_weapon_assets.cjs` from the unmodified `legacy/index.html`
(SHA-256 e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393) running in headless
Chromium. Nothing is authored here.

| File | Source in the legacy page | How |
|---|---|---|
| `weapons/NN_name.gun` | `MESHY_ASSETS[NN].vertices`, `.muzzle` | base64 decoded; "CATGUN01", u32 vertex count, f32 muzzle[3], 12 f32 per vertex (position, normal, colour, uv, material id) |
| `weapons/NN_name_base.jpg`, `_mr.jpg`, `_normal.jpg` | `MESHY_ASSETS[NN].textures[0..2]` | the JPEG bytes inside the `data:` URLs, copied unchanged |
| `fx-atlas.png` | `FX_ATLAS.canvas` (painted procedurally at page load) | `getImageData`, written as PNG (1024 x 1024, 8 x 8 tiles) |
| `weapon-assets.json` | — | names, triangle counts, muzzle points, SHA-256 of every file |

The same run writes `native/include/cat/generated/weapon_fx_tables.inc`.
The weapon index is the legacy `WEAPONS` index (0 NAILGUN … 15 QUAD SHOTGUN).
