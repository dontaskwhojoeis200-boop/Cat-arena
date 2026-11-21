# Feature parity — N10 vs Quake3e (N10-Q3E)

The N-series parity table against the original browser game is `docs/FEATURE_PARITY.md`. This
matrix compares the finished N10 game with what Quake3e provides, and records the migration status
of each feature on branch `migration/quake3e-n10`.
Q3E status: Implemented / Compiled / Executed / Tested / Unverified / Blocked; "—" = not started.

| Feature | N10 (authoritative) | Quake3e provides | Missing in Quake3e | Plan | Q3E status |
|---|---|---|---|---|---|
| Executable, Windows x64 | CatArena.exe, icon, version, manifest, crash reports | Win32 client + dedicated server, MSVC / MinGW builds | Cat Arena name, icon, crash minidumps | Adapt (B) | **B:** branded (name, icon, base folder, own home folder); Compiled (Windows CI), Executed (Linux client, Windows dedicated server) |
| Vulkan renderer | Own Vulkan 1.1 renderer, lightmapped world, effects | `renderervk`: Vulkan 1.0, BSP / PVS, lightmaps, VBO world, shaders, MD3 / IQM, flares, MSAA / SSAA / HDR / bloom | Cat Arena effect shaders | Reuse (B) | **B:** test arena with lightmaps Executed on lavapipe |
| Window, input, display modes | SDL3; raw mouse, layouts, sensitivity, fullscreen / refresh, focus | Win32 raw input, `r_mode` / custom modes, `in_minimize` | Cat Arena control settings UI | Reuse + ui (B/F) | — |
| Audio output | SDL3 + Web Audio-matched mixer | DMA mixer, WASAPI / SDL | Cat Arena mix rules | Reuse engine mixer; rules in cgame (D) | — |
| File system / packaging | `.cadata` assets next to the EXE | pk3 search paths, `fs_basegame` | Cat Arena pk3 | Convert (B) | **B:** `pak0.pk3` built reproducibly by `tools/q3e/package.py`; Executed on Linux and Windows |
| Console / cvars / config | settings file | console, cvars, cfg files | Cat Arena settings as cvars | Reuse (F) | — |
| Movement | Legacy rules bit-exact (sprint, crouch, slide, coyote, buffer, slopes, pads) | none in engine; Q3 `bg_pmove` in GPL game code | everything Cat Arena-specific | D1: stock Quake III `bg_pmove`; Cat Arena sprint / slide added in C | **C:** walk 320, sprint 480 u/s, slide (N10 duration, boost, friction ratio, cooldown) Tested (Linux); jump pads, coyote time, jump buffer, slope rules not ported |
| Collision | 2.5-D slabs / walls / solids | BSP brush traces, PVS | Cat Arena maps as BSP | Convert maps (C/D) | Slab convexity audited (all convex); **B:** q3map2 pipeline Executed on the generated test arena |
| Maps | Helios Crucible, Bastion Keep, Reactor Bastion, The Arsenal | BSP loader | the maps | Convert, Reactor Bastion first (D) | **D1:** Reactor Bastion converted (`ca_reactor`); floors, ramps, pads Tested against N10 data; the other three maps — |
| Weapons (16) | bit-exact to legacy tapes, 4 maps | weapon infrastructure in Q3 game code only | Cat Arena roster | Port N10 code into qagame / cgame (C/D) | **C:** NAILGUN Tested. **D3:** all 16 Tested on Linux (N10 table, rate, magazines, two slots and switch, frag reroll, random spawn loadout, hitscan spread / range / head hits, rocket / grenade / rift projectiles, blast damage and launch, shotgun recoil and mid-air lift; 16 MD3 viewmodels; tracers, beams, projectile glows, detonations). Effects approximate N10's WFX; weapon sounds — |
| Enemies (20 types), waves, AI, routes | bit-exact to legacy tapes, 4 maps | botlib (deathmatch bots; not suitable) | all | Port N10 code into qagame (C/D) | **C:** fiend and N10 wave rules Tested; **D2:** N10 navigation graph reused in qagame, Tested on Reactor Bastion (scheduler simplified); **D4:** all 20 types Tested on Linux (N10 stats, spawn rules, ranged attacks with the four projectile kinds and burn, slam / melee, flyers with N10 steering); voices — |
| Animation | runtime posing, Marine Cats | MD3 vertex animation, IQM skeletal | Cat Arena posing | **Decided (D4):** N10's posing and skeleton ported to C++20 in cgame, posed meshes drawn as polygons | **D4:** creatures (walk / run / fly, look, attack, pain, death, gibs) Executed on Linux; Marine Cats — (stage E) |
| HUD, menus, settings | legacy layout, 65 settings | 2-D drawing primitives, fonts via shaders | all Cat Arena UI | Port into cgame / ui (D/F) | **B:** Cat Arena menu module (C++20): main / in-game menu, loading screen Executed (Linux) |
| Multiplayer, 4-player co-op | JSON relay protocol, browser cross-play, Render relay, revive | UDP client / server, delta snapshots, prediction | co-op rules, revive, Cat Arena entities | Q3 networking + N10 rules (E); D2 | — |
| Online via Render relay | yes (wss://) | no WebSocket transport | — | D2 | — |
| Crash reports | minidump + text | Win32 exception filter turns a crash into an engine error naming the exception and address (`win32/win_main.c` ~700–765, 810); no minidump or report file | minidump, report file | Re-add N9 code (B) | — |
| Memory instrumentation | working set, private bytes, Vulkan sum | `\meminfo` (hunk / zone), `\vkinfo` | per-category GPU, CPU budget | Add (F) | Reservations read (MEMORY_BUDGET.md) |
