# assets/n2 — N2 runtime-parity corrections

## world-atlas-runtime.png

The 16-material world texture array the legacy game actually samples for the static world.

- Legacy behaviour: `Q3R.applyTextures()` runs at script load (legacy/index.html line 2996) and
  `rebuildWorld()` caches `layerCache` from `texCanvas` once. `paintWorldMaterials()` (line 8980)
  later repaints tiles 1,2,3,4,5,6,9,10,14,15 of `texCanvas` and calls `q3RebuildWorldTextures()`,
  but because the array is already built and the texture bit depth is unchanged, it is never
  re-uploaded. The repainted tiles only reach the unused `atlasTexture` (TEXTURE_2D).
- N1 baked the repainted `texCanvas` (`textures/world-atlas.png`). Those ten tiles therefore do not
  match the running game; the other six do.
- Capture: `tools/n2/legacy_texture_dump.cjs` (raw RGBA transfer) ran the unmodified legacy HTML (SHA-256
  e3c914bc...0393) in headless Chromium (WebGL2 via ANGLE on Mesa lavapipe), deployed Helios
  Crucible and read level 0 of every layer of `Q3R.worldArray` with `readPixels`.
  `tools/n2/audit_textures.py` un-flipped each layer back to canvas orientation, so this file has
  the same layout as N1 `world-atlas.png` (4x4 tiles of 128 px) and is loaded with the same rule.
- The pixels are the legacy's own canvas painter output (1196-1440, axis-aligned `fillRect`s with
  smoothing off) as rasterised by Chromium.
- See texture_audit.json for per-layer differences against N1 and the file's SHA-256.

## texture_audit.json

Full comparison (all four channels) of every texture the legacy samples while rendering Helios (world array, 45
material-array textures, 30 kit textures, lightmap) with the N1 bake. Summary:
- Kit textures: 30/30 byte-identical to N1 (decoded with PIL; the native stb decoder differs from
  PIL by mean 0.0097, max 3 levels).
- Material textures: 43/45 byte-identical. The two Helios cloud layers (sky/rb_clouds_a/b) differ
  (the legacy builds them through a premultiplied 2D canvas at runtime); the alpha-weighted error is
  about 0.3 levels, so N2 keeps the N1 files.
- Lightmap: byte-identical to the N1 PNG.

Tooling note: a first revision of the dump tool encoded read-back pixels through a 2D canvas, which
premultiplies alpha and corrupted every translucent texel. That briefly suggested a "Chromium
lightmap decode" effect that does not exist; the frame comparison (exact PNG bytes: mean error 0.06
levels) exposed it. The tool now transfers raw bytes.

These are corrections to how N1 data must be interpreted for parity; the N1 bake itself is unchanged.
