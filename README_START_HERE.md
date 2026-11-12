# Cat Arena: Feral Claw — C++20 / Vulkan rewrite handoff

**Read `CLAUDE_MASTER_PROMPT.md` first and give that file to Claude with this folder.** The latest *audited* executable game reference is `legacy/index.html` (v0.17 Cat Marine build). All essential code is inside the HTML's 55 inline `<script>` blocks; it is not missing just because there is no current unbundled source directory. `reference/multiplayer/server.js` is the matching multiplayer relay. `reference/character/` contains the model archive. `reference/patches/` contains the still-unapplied mouse/settings fix branch. A separate older by timestamp `v0.18` soundtrack branch is deliberately kept separate.

This is a **private source handoff**, not a playable native build. No native C++/Vulkan executable exists yet. The older migration Step 02 browser test was blocked by its sandbox; this does not stop writing C++ or compiling headless tests. The main prompt explicitly starts N0 code immediately, then the phase-prompts file walks through N1-N11.

Use these files in Claude Projects, a local Claude Code project, or another code-capable work environment. Unzip **before** starting to ensure full source/asset visibility. For source-intensive steps, provide Claude the relevant docs and paths; the main HTML is ~18 MB, so prefer targeted file reading/index-based access rather than pasting all its lines into one chat. Keep the original `legacy` and `reference` folders untouched. Use `native/` for new implementation.

## Included current and reference files
- `legacy/index.html`: authoritative, complete 18,320,927-byte game source including inline textures/material scripts, UI, maps and gameplay.
- `reference/legacy_android/*.apk`: WebView-based current Cat Marine APK for reference only (NOT native).
- `reference/character/CAT_MARINE_MODEL_AND_SOURCE.zip`: glTF skeleton/model/textures/hitbox source.
- `reference/multiplayer/`: matching current four-player server, deployment config, protocol smoke test and original Render ZIP.
- `reference/patches/`: v0.18 mouse and settings build-project patch, not an applied game successor.
- `reference/alternate_branches/`: separate music branch (do not overwrite Cat Marine).
- `reference/older_source/`: archived v0.15 tools, inspect before reuse; may contain old development signing material, do not publish.
- `docs/`: verified source audit, script index, parity, baseline reference and hashes.
- `tools/baseline/`: unverified Playwright capture driver and reference config.

**Additional large historical sources:** the preexisting `CAT_ARENA_MIGRATION_STEP_02_REFERENCE.zip` contains older modular and multiplayer archives, which are useful for archaeology but not authoritative. That ZIP is provided separately alongside this source-focused handoff.

**Known gap:** v0.18 mouse/settings branch is unapplied to the v0.17 HTML baseline, and no latest combined game was proven from supplied files. Preserve these as distinct inputs. The older QA environment never successfully rendered a game. The native rewrite must not claim screenshots or benchmarks it has not measured.

**Protection:** this handoff may contain historical signing material or third-party music references. Keep private; inspect distribution rights and exclude signing material from public release packages.
