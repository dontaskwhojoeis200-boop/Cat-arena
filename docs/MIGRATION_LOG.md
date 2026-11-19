# Migration log (JavaScript / WebGL → C++20 / Vulkan)

One line per milestone: what moved, how it was checked, where the record is. Details in the
milestone documents and `docs/PROGRESS.md`.

| Milestone | Moved to native | Checked by | Record |
|---|---|---|---|
| N0 | Core, timing, settings, input, SDL3 + Vulkan shell | unit tests, window self-tests | docs/history |
| N1 | Map / asset bake and loaders (`.cadata`) | hashes against the legacy data | README_N1 |
| N2 / N2.1 | Helios rendered; Vulkan 1.2 baseline with optional 1.3 / 1.4 paths | picture comparisons, validation layer | README_N2, README_N2_1 |
| N3 | Player physics and traversal | legacy movement tapes | README_N3 |
| N4 | Four maps, navigation | legacy topology, pictures | README_N4 |
| N5 / N5.1 | 16 weapons, projectiles, damage, pickups, viewmodel, effects, HUD; dynamic lights, medkits | legacy tapes, pictures | README_N5, README_N5_1 |
| N6 | Enemies, waves, AI, animation, Marine Cats | legacy tapes on four maps | docs/N6_ENEMIES.md |
| N6.5 | Windows build, package and GPU-free tests in GitHub Actions | CI runs | N6_5_BUILD_REPORT.md |
| N7 | Menus, settings, HUD, audio | recorded legacy UI, Web Audio graphs, self-tests | docs/N7_UI_AUDIO.md |
| N8 | Four-player co-op: relay (server.js), client, host-authoritative world, revive, server browser, name tags | 4-client tests, original server.js, original browser game (cross-play), soak, window self-test, Windows CI | docs/N8_MULTIPLAYER.md |
| N9 | Windows platform: display modes, refresh, controls, focus, audio recovery, Vulkan 1.1 baseline, crash reports, EXE icon / version / manifest | Linux suites, simulated Vulkan 1.1 device, crash tests, Windows CI (no GPU) | README_N9.md, docs/DEVELOPMENT_LOG_N9.md |

Direction changes
- 2026-11-12: Windows-only from N8 (Android removed from the requirements, code kept).
- 2026-11-16: memory target 192 MiB combined (128 MiB CPU + 64 MiB GPU) replaces 480 MiB;
  Quake III Arena-inspired artistic direction; Vulkan 1.1 as the intended device baseline
  (docs/ROADMAP_N8_N20.md).
