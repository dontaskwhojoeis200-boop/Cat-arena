# Roadmap N8–N20 (revised handoff, 2026-11-16)

Source: the revised N8–N20 handoff ("192 MiB Combined Memory Target | Quake III Arena Artistic
Fidelity"), which replaces the handoff of 2026-11-12 (480 MiB, Xbox 360-era direction). This file
records the direction and how it maps onto the existing native code; `docs/PROGRESS.md` stays the
record of what is actually done. Nothing here is a result.

## State at the start of N8 (verified)

- N0–N7 implemented; N7 tested: Linux matrix (7 configurations), Windows build / package / GPU-free
  tests in GitHub Actions (run 43336140592). Baseline preserved as branch `n7` (commit 5a220aa;
  creating a git tag is refused from the development session, so the branch is the baseline).
- The N6 build has run on the reference PC (Pentium G2020, GTX 660, 8 GB DDR3, 5400 rpm HDD):
  up to 144 FPS (monitor limit) with occasional stutter (owner's report; no average, 1 % / 0.1 %
  lows or full-combat measurements exist).
- Open from earlier milestones: MD3 prop models, portal / mirror feeds, PVS, 25 settings stored but
  not applied (scene resolution, AA, culling modes, texture / effect detail …).

## Platform and engine

- **Windows 10/11 x64 only.** No Android, APK, NDK, mobile rendering, touch interfaces or Android
  asset formats from N8 on. Existing code is preserved (touch controls, safe area, the touch HUD
  layout, `reference/legacy_android`) and keeps compiling; it is not extended or tested further.
- Linux remains the development / CI host (same code, lavapipe); it is not a product platform.
- Separation kept: simulation (`cat_arena_physics`: physics, combat, enemies — no Vulkan),
  rendering (`cat_arena_render`, `cat_arena_fx`), networking (`cat_arena_net`, `cat_arena_coop`),
  audio (`cat_arena_audio`), assets, platform / shell (`main.cpp`, `platform_info`).
- **Vulkan baseline.** Vulkan 1.1 is the device baseline since N9 (the N8 renderer still rejected
  devices below 1.2). Newer features — dynamic rendering, synchronization2 and the others — stay
  behind runtime capability checks with render-pass / classic-barrier fallbacks, and the Vulkan 1.2
  driver properties are only queried on a negotiated 1.2+ path. Tested with the API capped at 1.1
  and with simulated older devices on lavapipe; a physical Vulkan 1.1 driver and the GTX 660 are
  unverified (N19).
- **GLM / Vulkan Memory Allocator.** Named by the handoff as part of the stack, but *not used by the
  existing code*: the simulation uses double math that reproduces the legacy JavaScript bit for
  bit (solo replays), the renderer has its own small math and one `vkAllocateMemory` per resource
  with exact byte tracking. Decision: GLM only where renderer math benefits (never in the
  simulation, which would break replay parity); VMA evaluated in N14 / N15 when sub-allocation and
  budget handling are implemented, measured against the current allocator.

## The 192 MiB objective (replaces 480 MiB)

| Category | Provisional budget |
|---|---|
| CPU-side application memory | 128 MiB |
| GPU memory | 64 MiB |
| Combined (maximum) | 192 MiB |
| Preferred gameplay range | 144–176 MiB combined |

The budget covers the full presentation (waves, maps, weapons, UI, audio, multiplayer), not a
low-quality mode. It may be re-split after measurement; the total stays 192 MiB.

**Accounting methodology (draft; becomes official in N12).**
- Measured and reported separately: process private committed memory (Windows `PrivateUsage`),
  working set and peak working set; engine-controlled CPU allocations (by subsystem, N12);
  Vulkan device allocations by category — swapchain images, depth / other attachments, textures,
  geometry, uniform / staging buffers, host-visible vs device-local; driver budget and usage
  (`VK_EXT_memory_budget` where present); audio; temporary loading allocations; peaks of each.
- Combined metric = private committed CPU memory + device-local GPU allocations made by the
  engine. Host-visible allocations are counted once (with GPU memory) and subtracted from the CPU
  side where the driver maps them into the process; integrated / software devices (lavapipe, where
  "GPU" memory is process memory) report the working set alone and are labelled as such.
- Driver, OS and compositor memory outside the engine's control is reported separately where
  visible and never presented as enforced by the game. The game cannot guarantee a physical
  memory ceiling; the target is a measured, reproducible figure.
- Present today: working set and peak (Windows `PROCESS_MEMORY_COUNTERS`, Linux `/proc`), private
  bytes (since N8: Windows `PrivateUsage`, Linux `RssAnon` as the closest figure), exact sum of the
  renderer's Vulkan allocations. Missing: per-category GPU accounting, budgets, CPU allocation
  tracking by subsystem, loading peaks.
- Known pressure point: at 1920×1080 three RGBA8 swapchain images + one D32 depth image are about
  31.6 MiB, half the GPU budget. The windowed renderer draws straight into the swapchain (no
  intermediate scene target, no MSAA) with one depth image, and asks for `minImageCount + 1`
  swapchain images (typically 3); N12 measures this, N15 / N16 decide on image count and formats
  without lowering the output resolution.

## Artistic direction (replaces the Xbox 360-era direction)

Quake III Arena-inspired, original Cat Arena artwork (no Quake III assets): strong silhouettes,
Gothic-industrial architecture, high-contrast baked lighting, detailed hand-authored textures,
readable enemies, distinctive weapons, sprite / billboard effects, animated (scrolling, pulsing)
materials. Reactor Bastion keeps its layout and named areas (Reactor Atrium, Command Spine,
Security Control, Barracks, Medical Wing, Loading Dock, Machine Shop, Armory, Communications,
Maintenance Tunnels, Upper Observation, Exterior Breach) and its multi-level geometry.

Production targets (starting points, validated against real assets): enemies 400–3,500 triangles
by class (standard 800–1,500), players 1,000–2,000, weapons 500–2,500, medium map 50k–180k static
triangles (10k–40k visible). Textures 256–512 (hero assets up to 1024), BC1 / BC3 / BC5 / BC7 with
format queries and fallbacks, active texture set investigated at 8–20 MiB. Lean material system:
albedo, vertex colour, lightmaps, optional normal / emissive / limited specular. Forward renderer,
baked lighting, few dynamic lights, light fog. No asset replacement before N13.

## Tiers

| Tier | Goal | Notes for this codebase |
|---|---|---|
| N8 | Multiplayer completion | Port of the legacy v3.24 co-op (relay protocol, host-authoritative world, 4 players, revive, server browser, name tags, disconnects); networking memory tracked from here |
| N9 | Windows platform completion | EXE packaging (icon, version, manifest), fullscreen / resolution / refresh rate, raw mouse, keyboard layouts, audio device recovery, focus, writable log folder, crash reports; Vulkan 1.1 device baseline — implemented, Windows hardware checks open |
| N10 | Gameplay feature parity | Verify movement, weapons, enemies, AI, physics, explosions, waves, health, animation, maps, UI, audio, multiplayer; fix before graphics work — implemented: weapon tapes on all four maps, co-op fixes, MD3 props, 8 settings applied; the 17 remaining renderer settings and portal / mirror feeds moved to N13 / N14 / N16 (they cost memory measured from N12) |
| N11 | Stable baseline | Preserved Windows build + report: average FPS, 1 % / 0.1 % lows, CPU / GPU frame time, RAM, VRAM, draws, triangles |
| N12 | Instrumentation | CPU / Vulkan memory tracking by category, budgets, peaks, per-resource accounting, CPU subsystem and GPU timing; official 192 MiB methodology; baseline memory report |
| N13 | Quake III fidelity prototype | One Reactor Bastion benchmark area with the new textures, lightmaps, enemy and weapon models; measured RAM / VRAM; cost-benefit of each feature |
| N14 | Asset pipeline + rendering optimisation | Compact meshes, BC compression, batching, sharing, pipeline cache, descriptors, visibility |
| N15 | Major memory optimisation | Arenas (persistent / level / frame), pools, texture residency, geometry, collision, animation, audio streaming; toward 128 + 64 MiB with before / after measurements |
| N16 | Advanced Vulkan optimisation | Draw-call reduction, instancing, indirect where it pays, sub-allocation, synchronisation; benchmarked, kept only when measured better |
| N17 | Gameplay / AI / animation optimisation | Large waves (10 / 25 / 50 / 75 / 100 enemies), AI scheduling, collision, navigation, skinning, projectiles, multiplayer overhead |
| N18 | Loading / streaming / frame pacing | Shader and pipeline stutter, HDD loading (cold vs warm), upload stalls, allocation spikes |
| N19 | Legacy hardware validation | G2020 + GTX 660: full map, waves, projectiles, multiplayer, transitions; FPS, lows, peak memory |
| N20 | Release | Final Windows edition, reproducible build, final reports |

Performance targets (G2020 + GTX 660, 1080p): stable 60 FPS primary, 90–120 secondary, 120+
stretch — development targets, none measured yet. Results are classified as Implemented /
Compiled / Executed / Tested / Unverified / Blocked; a target is never reported as met without a
run on the stated hardware.
