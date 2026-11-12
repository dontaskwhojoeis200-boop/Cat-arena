# CLAUDE SEQUENTIAL MILESTONE PROMPTS

Use `CLAUDE_MASTER_PROMPT.md` as the standing specification. Copy the relevant block below into the next Claude turn only after reviewing the previous milestone's produced source/ZIP. Maintain a single evolving repo. Never assert unexecuted tests passed. These stages are acceptance **targets**, not claims that the work already exists.

## N0 — Native platform and Vulkan boot
Implement the precise "START NOW: MILESTONE N0" section of the master prompt. Build C++20 core + SDL3 Vulkan desktop render shell + headless tests; package actual sources and logs. Do not write only a design document.

## N1 — Extract and bake Cat Arena data
Inspect `legacy/index.html`, model archive and material/WorldKit generators. Write a reproducible offline extractor that reads the original data/asset payloads and produces stable, versioned native binary/JSON/glTF-friendly outputs: textures, meshes, baked maps, spawns, triggers, lightmaps, materials, hit volumes and nav metadata. Preserve source hashes and conversion manifests. Do not simply JSON.stringify WorldKit objects because callbacks and linked geometry are lost. Implement read/write/validation tests and native loader. Deliver preview inventory and diffs. Do not silently synthesize missing data.

## N2 — Render Helios Crucible as a native map
Load the real extracted `helios-crucible` map using native renderer; camera, depth, UV/textures, world coordinate transforms and perspective, per-map floors -4/0/7/12, visible bridges, ramps, chambers and landmark features. Basic lightmaps, fog, sky and material stages should be progressively enabled and individually verified. Record representative frames only if actually executed. Do not substitute a generic test room and claim map parity. Optimize indexed geometry/batches and retain a capability-specific fallback. Maintain CMake/test status.

## N3 — Physics and first-person traversal
Implement deterministic 120Hz simulation, world geometry collision including stacked floors and bridge undersides, step/slope mechanics, player capsule/hitbox, walk/sprint/crouch/slide/jump, air control and camera/gun bob. Port measured constants from the authoritative 5161+, 8461+, and 12111+ source ranges, with explicit note about effective crouch collider discrepancy. Add headless trajectory fixtures, slope/ceiling/step collision tests and keyboard/mouse capture on Windows.

## N4 — All four maps and navigation topology
Port Bastion Keep (-4/0/7/11), Reactor Bastion (-4/0/5/10.4) and The Arsenal/q3-gallery (-3/0/4.5/10), with original geometry, materials, props, lightmaps, triggers, separate render and collision geometry, stacked world navigation, room/sector visibility and map selection. Verify each map separately for floor/bridge collision and camera vistas; don't collapse heights or recycle demo map. Preserve internal map IDs and user-facing Cat Arena branding.

## N5 — Weapons, projectiles, damage and HUD
Implement all 16 source-defined weapons with exact individual ammo/cadence/reload/hitscan/projectile rules, muzzle effects, recoil, impacts, splash, rocket/grenade jump and first-person viewmodel. Connect ammo/health/HUD and pickups. Establish deterministic test fixtures for projectile travel, hitscan ray tests, splash falloff and self-damage/impulse. Do not invent a universal weapon behavior or use first-person marine body for gun view.

## N6 — Enemy AI, waves, animation and hitboxes
Add 20 source-defined types, ground and flying navigation, animation state, attack modes, damage, spawn/waves, scaling, final-five highlight, death/loot. Import Cat Marine's real GLB skin and preserve third-person visibility, proper local viewmodel exclusion, per-part hit metadata and actual sampled animation behavior. Add 120Hz/30Hz AI tick/route scheduling and seeded wave test fixtures. Do not claim match to seven GLB clips without verifying source runtime use.

## N7 — Native menus, full settings and audio
Implement black/white pointed Gothic Cat Arena UI, subtle grime/parallax, title/menu/map selection + authentic previews, pause/return, HUD, volume, FOV, graphics/resolution/AA/culling, perf stats and mobile/custom HUD controls. Mouse/keyboard fully navigate settings without touch; Android pointer capture gets explicit tests. Port and provenance-check audio assets, effects and music branches. Preserve responsive safe area and gamepad/KBM/touch modes.

## N8 — Multiplayer interoperability
Build a native WebSocket JSON client interoperable with `reference/multiplayer/server.js` and its documented build/map checks. Preserve `/rooms`, `/ws`, 4 players, hosted room browser, host authoritative snapshots, revive hold 4s, disconnect and map validation. Use existing relay until protocol versioning requires change. Produce packet fixtures and automated relay protocol tests; verify two and four real clients on local/Render deployment before claiming playable multiplayer.

## N9 — Android Vulkan native app
Create real ARM64 NDK/Gradle `libcat_arena.so`-backed APK, life-cycle management, native Vulkan Android surface, touch/controller/KBM detection, density/fullscreen, audio, pointer capture, background/resume, safe-area layout. Provide build instructions and actual APK only if compiled. Distinguish supported Vulkan features from 1.4 desktop target and avoid assuming Android SDK/API versions imply GPU Vulkan 1.4 capability. Retain original APK separately for comparison.

## N10 — Graphics fidelity, optimization and regression
Audit/implement material stages, portal/skies, normal/light maps, transparent sorting, FX, decals, particles, quality toggles and performance instrumentation. Profile real hardware to decide batching, culling, depth prepass, indirect draw, memory, pipeline cache, shader variants. Preserve authored detail. Compare deterministic scenes and gameplay to legacy; report actual average FPS, 1% and 0.1% lows with sampling method, GPU timestamp availability, CPU frametimes and hardware specs. Do not present guessed benchmark numbers.

## N11 — Shipping and verification
Create reproducible Release Windows x64 EXE and Android ARM64 APK/Gradle project with symbols separately, versioned content package, licenses, tests, 4-map/16-weapon/20-enemy feature parity matrix, graphics/device limits, two/four-player test evidence and integrity hashes. Include Linux build if feasible. Never include older debug APK signing keys in a public repo. Report remaining unverified features instead of claiming perfect parity.

## EVERY MILESTONE: required output
- Concrete source modifications, not just pseudocode.
- Updated complete native source ZIP/repository state and build scripts.
- Actual command output where compiled/tested, clearly marked `PASS` / `FAIL` / `NOT_RUN`.
- Updated `docs/PROGRESS.md`, `docs/FEATURE_PARITY.md`, `docs/TEST_MATRIX.md`, `docs/BUILD.md`, `docs/KNOWN_LIMITATIONS.md`.
- One-paragraph handoff for the next milestone and specific unresolved blockers.
