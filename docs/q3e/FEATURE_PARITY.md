# Feature parity — N10 vs Quake3e (N10-Q3E)

The N-series parity table against the original browser game is `docs/FEATURE_PARITY.md`. This
matrix compares the finished N10 game with what Quake3e provides, and records the migration status
of each feature on branch `migration/quake3e-n10`.
Q3E status: Implemented / Compiled / Executed / Tested / Unverified / Blocked; "—" = not started.

| Feature | N10 (authoritative) | Quake3e provides | Missing in Quake3e | Plan | Q3E status |
|---|---|---|---|---|---|
| Executable, Windows x64 | CatArena.exe, icon, version, manifest, crash reports | Win32 client + dedicated server, MSVC / MinGW builds | Cat Arena name, icon, crash minidumps | Adapt (B) | Engine Compiled (Windows CI), Executed (Linux) |
| Vulkan renderer | Own Vulkan 1.1 renderer, lightmapped world, effects | `renderervk`: Vulkan 1.0, BSP / PVS, lightmaps, VBO world, shaders, MD3 / IQM, flares, MSAA / SSAA / HDR / bloom | Cat Arena effect shaders | Reuse (B) | Executed on lavapipe (no game content) |
| Window, input, display modes | SDL3; raw mouse, layouts, sensitivity, fullscreen / refresh, focus | Win32 raw input, `r_mode` / custom modes, `in_minimize` | Cat Arena control settings UI | Reuse + ui (B/F) | — |
| Audio output | SDL3 + Web Audio-matched mixer | DMA mixer, WASAPI / SDL | Cat Arena mix rules | Reuse engine mixer; rules in cgame (D) | — |
| File system / packaging | `.cadata` assets next to the EXE | pk3 search paths, `fs_basegame` | Cat Arena pk3 | Convert (B) | Base folder without id data Executed |
| Console / cvars / config | settings file | console, cvars, cfg files | Cat Arena settings as cvars | Reuse (F) | — |
| Movement | Legacy rules bit-exact (sprint, crouch, slide, coyote, buffer, slopes, pads) | none in engine; Q3 `bg_pmove` in GPL game code | everything Cat Arena-specific | Adapt into shared C pmove (D1) | — |
| Collision | 2.5-D slabs / walls / solids | BSP brush traces, PVS | Cat Arena maps as BSP | Convert maps (C/D) | Slab convexity audited (all convex) |
| Maps | Helios Crucible, Bastion Keep, Reactor Bastion, The Arsenal | BSP loader | the maps | Convert, Reactor Bastion first (D) | — |
| Weapons (16) | bit-exact to legacy tapes, 4 maps | weapon infrastructure in Q3 game code only | Cat Arena roster | Port N10 code into qagame / cgame (C/D) | — |
| Enemies (20 types), waves, AI, routes | bit-exact to legacy tapes, 4 maps | botlib (deathmatch bots; not suitable) | all | Port N10 code into qagame (C/D) | — |
| Animation | runtime posing, Marine Cats | MD3 vertex animation, IQM skeletal | Cat Arena posing | Decide format in D | — |
| HUD, menus, settings | legacy layout, 65 settings | 2-D drawing primitives, fonts via shaders | all Cat Arena UI | Port into cgame / ui (D/F) | — |
| Multiplayer, 4-player co-op | JSON relay protocol, browser cross-play, Render relay, revive | UDP client / server, delta snapshots, prediction | co-op rules, revive, Cat Arena entities | Q3 networking + N10 rules (E); D2 | — |
| Online via Render relay | yes (wss://) | no WebSocket transport | — | D2 | — |
| Crash reports | minidump + text | Win32 exception filter turns a crash into an engine error naming the exception and address (`win32/win_main.c` ~700–765, 810); no minidump or report file | minidump, report file | Re-add N9 code (B) | — |
| Memory instrumentation | working set, private bytes, Vulkan sum | `\meminfo` (hunk / zone), `\vkinfo` | per-category GPU, CPU budget | Add (F) | Reservations read (MEMORY_BUDGET.md) |
