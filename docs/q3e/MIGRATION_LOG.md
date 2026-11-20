# Migration log — Cat Arena onto Quake3e (N10-Q3E)

The N0–N10 log (JavaScript → C++20 / Vulkan) is `docs/MIGRATION_LOG.md`.

| Date | Stage | What happened | Record |
|---|---|---|---|
| 2026-11-19 | Q3E-A | Handoff "N10-Q3E: Native Engine Migration and Modernization" received. N10 finished first and preserved (branch `n10`, f406358). Branch `migration/quake3e-n10` created from it. Quake3e dec793de imported unmodified (`engine/quake3e`, commit 53aca6b). Audit: Quake3e is engine-only (game code from GPL Q3 sources), runs without id data from its own base folder, loads native game DLLs, Vulkan 1.0 renderer with one required feature. Built on Windows x64 (MSVC, CI) and Linux; started on lavapipe; GPL `ui` module loaded. Owner decisions D1–D3 opened | SOURCE_AUDIT.md, ENGINE_MIGRATION.md |
| 2026-11-19 | Q3E-A | Owner decisions: D1 stock Quake III movement with Cat Arena's sprint / crouch / slide added; D2 UDP now, WebSocket tunnel for the Render relay in Q3E-E; D3 code under GPL v2, assets under the owner's terms | ENGINE_MIGRATION.md |
