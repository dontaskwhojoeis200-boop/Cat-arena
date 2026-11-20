# Source audit — N10-Q3E-A (2026-11-19)

The N0 audit of the original browser game is `docs/SOURCE_AUDIT.md`; this one covers the Quake3e
migration. All Q3E documents are in `docs/q3e/`.

Scope: the N10-Q3E handoff ("Native Engine Migration and Modernization", 2026-11-19) asks to move
Cat Arena: Feral Claw onto the Quake3e engine. This audit records what each code base actually
contains, measured or read from the source; nothing here is a plan (see ENGINE_MIGRATION.md in this folder).
Status words: Implemented / Compiled / Executed / Tested / Unverified / Blocked.

## 1. Source revisions

| Source | Revision | Where | Licence |
|---|---|---|---|
| Cat Arena N10 (authoritative game reference) | `f406358` on branch `n10` (2026-11-19) | this repository | none stated (see LICENSING.md) |
| Quake3e (engine foundation) | `dec793de9677bc9c16c7027e8905c2d4f4643a19` (2026-11-15) | `engine/quake3e`, imported unmodified in commit `53aca6b` | GPL v2 |
| ioquake3 (GPL game / cgame / ui code, reference) | `52088b9eae247d8e321c8dc1fb49a879652854f9` (2026-11-04) | not imported; cloned for the audit | GPL v2 |
| id Software Quake III Arena source release | `dbe4ddb10315479fc00086f08e25d968b4b43c49` (2012-01-31) | not imported; cloned for the audit | GPL v2 |

The migration branch `migration/quake3e-n10` starts from N10 `f406358`. Branch `n10` and the N10
source zip stay the independently buildable reference (Linux 78/78, Windows CI run 44560790833).

## 2. Quake3e, as found

Read from `engine/quake3e` at dec793de unless stated otherwise.

**What it is.** An engine only. "This repository does not contain any game content" (README.md).
`code/game`, `code/cgame` and `code/ui` contain only the interface headers (`g_public.h`,
`bg_public.h`, `cg_public.h`, `ui_public.h`): no game logic, no player movement (`bg_pmove.c`),
no weapons, no bots' game side. Those come from the GPL Quake III game source (id release or
ioquake3), which Quake3e is compatible with (original 1.32 module API; the two differ only in a
few math syscalls in `g_public.h` / `cg_public.h`).

**Game modules.** Loaded as QVM bytecode or as native libraries (`qcommon/vm.c` 1864–1890:
`vm_game` / `vm_cgame` / `vm_ui` 0 = native DLL via `dllEntry` + `vmMain`, falls back to QVM;
refused only when `fs_restrict` is set). Native modules can be written in C++ behind those two C
entry points. Executed: ioquake3's GPL `ui` built as a native library loaded and ran in this
Quake3e build (§5).

**Standalone use without id data.** `fs_basegame` selects the base folder (`files.c` 4763). The
pak0 checksum check (`FS_CheckIdPaks`) only looks at `baseq3/pak0…8` and `demoq3/pak0`; the only
hard requirement at start is a `default.cfg` (`files.c` 5512–5527). Executed: started with an
empty `catarena` folder holding only `default.cfg` (§5).

**Vulkan renderer** (`code/renderervk`, derived from Quake-III-Arena-Kenny-Edition).
- Instance API version requested: 1.0 in release builds, 1.1 in debug builds (`vk.c` 1346–1350).
- Required device feature: `fillModeNonSolid` only — device creation fails without it (`vk.c`
  1767). Optional when present: `wideLines`, stores and atomics, `samplerAnisotropy`
  (`shaderInt64` in debug builds only).
- Device extensions: swapchain; debug-only extras (timeline semaphore, memory model, buffer
  device address, 8-bit storage) inside `#ifdef _DEBUG`.
- Static world in VBOs (`vk_vbo.c`), merged lightmap atlases, multiple command buffers, reversed
  depth, optional offscreen rendering (`r_fbo`) for MSAA / supersampling / HDR / bloom / gamma,
  flares (`vk_flares.c`), MD3 / MDR / IQM models (`tr_model.c`, `tr_model_iqm.c`).
- Default buffer sizes: vertex buffer 4 MiB (8 MiB high), staging 2 MiB (24 MiB high), image
  memory in 32 MiB chunks (`vk.h`).
See VULKAN_COMPATIBILITY.md.

**Engine memory.** Hunk `com_hunkMegs` default 128 MiB for the client (minimum 64), 56 MiB for the
dedicated server; zone 12 MiB with extra segments on demand (`common.c` 44–56). The hunk is one
`calloc` at start (`common.c` ~2188): on Windows that is committed (private) memory whether
touched or not. This matters for the 192 MiB target (MEMORY_BUDGET.md).

**Networking.** UDP client / server (default port 27960), delta-compressed snapshots,
`MAX_CLIENTS` 64, `MAX_GENTITIES` 1024 (`GENTITYNUM_BITS` 10), `MAX_SNAPSHOT_ENTITIES` 256 per
snapshot, `MAX_MSGLEN` 16384, `PACKET_BACKUP` 32 (`q_shared.h` 1128–1137, `qcommon.h` 148–229).
No WebSocket transport.

**Build systems.** Makefile (Linux, MinGW, macOS), CMake, MSVC 2017+ solution
(`code/win32/msvc2017`). Windows dependencies bundled (SDL2, libcurl, libjpeg, ogg / vorbis).
Upstream's own CI builds Windows with MSVC and MSYS2 (`.github/workflows/build.yml`).

**Other subsystems present:** zone / hunk allocators, pk3 file system, cvars and console, BSP
collision and traces (`qcommon/cm_*.c`), SDL2 or native Win32 / X11 input and video, DMA sound
mixer with WASAPI / SDL back ends, botlib (Q3 deathmatch bot navigation, AAS files).

## 3. Cat Arena N10, as found

Authoritative for gameplay. Measured with the N10 build and probes in this audit.

| Area | N10 implementation | Evidence |
|---|---|---|
| Simulation | C++20, `double` math reproducing the legacy JavaScript bit for bit at 120 Hz; fixed tick 1/120 s | 16 replay / rule lines, weapon tapes 72,880 ticks on four maps |
| Movement | Quake-style acceleration / friction model in metres: walk 7.6 m/s, ground accel 17.5, air accel 1.85, friction 6, stop speed 2.9, jump 7.65 m/s, gravity 18 m/s²; plus sprint ×1.5, crouch, slide (1.12 s, boost ×1.085), jump buffer .115 s, coyote time .09 s, slope penalty, skill-speed cap 20.5 m/s (`physics.hpp` 22–35) | N3 / N4 movement tapes |
| Collision | 2.5-D "slab" world: convex floor polygons with thickness (some with a ramp plane), wall segments with height range, solid prisms, jump pads; capsule player; shots sampled every 0.19 m | `physics.hpp`; legacy tapes |
| Maps | Four maps baked from the legacy game (`assets/n1/maps`): render mesh + materials + lightmaps + compiled collision | table below |
| Weapons | 16 weapons, projectiles, splash, recoil, pickups, medkits, reroll on frag | weapon tapes |
| Enemies | 20 types, waves, routes (navigation graph, sliced searches), flyers, enemy shots; at most 20 enemies and 105 enemy shots alive (`enemies.hpp` 48) | 24 legacy tapes on four maps |
| Animation | Legacy runtime posing of enemy models and Marine Cats (GLB-derived meshes, six hit parts) | 441,990 poses match |
| Multiplayer | 4-player co-op over the legacy WebSocket relay protocol (server.js / native relay / Render), host-authoritative JSON snapshots every 90 ms, revive (hold 4 s), squad wipe | cross-play 13/13 with the browser game, 1 h soak (host upload 117 KiB/s) |
| UI / audio | Legacy menus, 65 settings (48 act), HUD, Web Audio-matched mixer | UI and audio tests |
| Renderer | Own Vulkan 1.1 renderer (SDL3), lightmapped world, effects, MD3 props | picture comparisons, Windows CI without GPU |
| Platform | Windows x64 package, crash reports, EXE resources | Windows CI |

Map sizes (N10 data, measured with a probe against `cat_arena_physics` / `cat_arena_assets`):

| Map | Render triangles | Slabs (all convex) | Ramp slabs | Wall segments | Jump pads | Spawns |
|---|---|---|---|---|---|---|
| Helios Crucible | 7,308 | 167 | 8 | 2,479 | 2 | 28 |
| Bastion Keep | 5,462 | 156 | 10 | 2,652 | 2 | 12 |
| Reactor Bastion | 24,398 | 343 | 20 | 3,789 | 4 | 12 |
| The Arsenal (`q3-gallery`) | 2,398 | 988 | 931 | 3,083 | 1 | 7 |

Every slab polygon on all four maps is convex, so each slab can become one Quake III brush; wall
segments become thin box brushes.

## 4. Reuse / replace / adapt

| Subsystem | Decision | Reason |
|---|---|---|
| Renderer | **Replace** with Quake3e `renderervk` | Handoff requirement; BSP / PVS / lightmaps / VBO world are what it is built for. N10 renderer kept on `n10` |
| Platform layer (window, input, audio output, file system, console, cvars) | **Replace** with Quake3e | Provided and mature; N9 extras (crash reports, EXE resources) are re-added on top |
| Network transport and snapshots | **Replace** with Quake3e UDP client / server | Handoff requirement; delta snapshots instead of JSON. Consequence: no cross-play with the browser game and no Render WebSocket relay (decision D2) |
| Collision | **Replace** with Quake3e BSP traces (`cm_*`) on converted brushes | Needed for the BSP pipeline and client prediction; changes movement results at the edges (D1) |
| Player movement | **Adapt**: a shared C `bg_pmove`-style module running Cat Arena's movement on BSP traces (D1) | Must run on server and predicting client |
| Weapons, enemies, waves, navigation, revive, scoring | **Adapt** the N10 C++20 code into the game module (`qagame`), behind a C ABI | Authoritative Cat Arena behaviour; already tested against the legacy |
| Cosmetic effects, HUD, menus | **Adapt** into `cgame` / `ui` (Quake III shaders and 2-D drawing) | The engine has no Cat Arena UI |
| Enemy animation | **Adapt**: export poses to MD3 / IQM (engine-native) or a small skeletal extension; decide in Q3E-D | Quake3e renders MD3 and IQM |
| Maps | **Convert**: generated `.map` (brushes from slabs / walls / solids + visual mesh as `misc_model`) compiled by q3map2 into BSP with PVS | Tool to build; keeps layouts exactly |
| Assets (textures, sounds, music, models) | **Reuse**, packed into `catarena/*.pk3` | Original Cat Arena data; formats the engine reads (TGA / JPG / PNG via patch, WAV / OGG, MD3) |
| Quake III game rules, weapons, items, bots | **Not used** | Handoff: keep Cat Arena gameplay; botlib not used for PvE enemies |

## 5. Build and run evidence (this audit)

| Check | Result | Status |
|---|---|---|
| Quake3e Linux build, `make USE_SDL=1 USE_CURL=0` (SDL2 2.30 built from source, OpenGL headers from libglvnd — packages cannot be installed here) | client, dedicated server, OpenGL and Vulkan renderers built | Compiled |
| Start with no id data (`fs_basegame catarena`, only `default.cfg`), Vulkan on lavapipe | renderer init, device selection, swapchain (IMMEDIATE, 3 images), shaders; stops at "VM_Create on UI failed" as expected without a UI module | Executed |
| ioquake3 GPL `qagame` / `cgame` / `ui` built as native x86-64 libraries | built | Compiled |
| Same start with ioquake3's native `ui` | loads (`VM_LoadDll(ui) succeeded`), menu drawn with default textures (no id art), clean quit | Executed |
| Dedicated server start / `+quit` with no id data (Linux) | exit 0 | Executed |
| Windows x64 MSVC build of `engine/quake3e` (workflow `q3e-windows`) | see TEST_MATRIX.md | — |
| GTX 660, Windows PC | not run | Unverified |
