# N0 -> N1 changes

No bytes changed in legacy/ or reference/. The original reference inventory still
verifies. Existing N0 code remains; only CMake adds the new asset targets and
.gitignore excludes offline dependency/cache folders. Pre-N0 and N0 documents,
N0 tests/evidence and N0_FILE_MANIFEST.json are retained as historical records.

Added:
- native/tools/bake.cjs: source hash gate, explicit CPU module execution, AST-selected
  helpers, generated geometry/material/chart capture, typed containers and catalogs.
- native/tools/bake_assets.py: one-command orchestration, original Marine extraction,
  fitted-kit glTF exports, data catalog, inventory/schematic previews, final manifest.
- native/tools/validate_assets.py: independent container decoding, all-file hash
  verification, map/catalog cross-checks, stacked surfaces, lightmap dimension/hash
  checks, glTF reference bounds and two-clean-bake comparison.
- native/tools/package.json + package-lock.json: pinned offline-only dependencies.
- native/include/cat/assets.hpp + src/assets.cpp: bounded C++20 data/mesh codecs,
  source link/grid/world validation and safe asset path resolution.
- native/src/asset_check.cpp and tests/asset_tests.cpp: actual native loader CLI and
  corrupt/truncated/version/overflow/nonfinite/index/path/schema checks.
- assets/n1/:578 generated files,577 manifest-tracked content files, including four
  maps and complete extraction inventory. No runtime JS dependency is added.
- N1 docs, model/tool dependency notices, evidence, preview and package manifest.

Extraction transformations are explicit: evaluate builders/cutters and patch CPU
routines; normalize callback-free data; replace object identity references with
index paths; convert base64 payloads to real files; retain float32 vertex attributes
and deduplicate exact records into indices; generate native binary+JSON sidecars;
run original lightmap chart preparation and verify shipped hashes; create optional
glTF diffuse inspection views. No collision/geometry simplification, source material
stage removal, source asset replacement or gameplay value tuning is performed.

No N0 native game feature is removed. This milestone does not connect loaded maps
to the Vulkan renderer or implement movement, AI, UI, audio or networking.
