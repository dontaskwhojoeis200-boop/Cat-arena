# Windows development roadmap

The N7 source archive is the preserved baseline. Its archive SHA-256 is
`c8d76021a2c5cc3fab7aa763beb3a43c5ec5ab90651a680d0eb2f2ce6df4a746`.
Historical build reports, Android sources, fixtures, and reference assets remain
available for comparison. New development and release acceptance target Windows
10/11 x64. Linux can serve as a build and test host.

| Tier | Goal |
| --- | --- |
| N8 | Four-player multiplayer, browser, authoritative combat, and revive |
| N9 | Windows input, display, audio and standalone executable validation |
| N10 | Gameplay, maps and engine feature completion |
| N11 | Downloadable, measured Windows baseline |
| N12 | Reproducible per-system timing and memory accounting |
| N13 | Reactor Bastion visual prototype and hardware benchmark |
| N14 | Renderer and asset-pipeline efficiency |
| N15 | Modernize remaining assets and control resource lifetimes |
| N16 | Profile and improve rendering submission and GPU work |
| N17 | Optimize physics, AI and animation under large waves |
| N18 | Optimize first-run and warmed-up frame pacing and HDD streaming |
| N19 | Benchmark and tune on Pentium G2020 and GTX 660 |
| N20 | Validate and package the Windows release |

N7's actual device floor is Vulkan 1.2, with shaders targeting Vulkan 1.1.
Preserve the working capability paths during N8. Verify the installed driver and
device API on the GTX 660. A Vulkan 1.1 device path needs implementation and
validation before it can be advertised. Optional features require capability
checks and fallbacks. N6 reaching a 144 FPS cap on the legacy rig is a user
observation. Sustained FPS and frame-time percentiles are pending measurement.

## Memory acceptance target

Maximum intended presentation targets 480 MiB combined application memory.
The preliminary allocation is 224 MiB system memory, 208 MiB GPU memory and
48 MiB headroom. Target 380–430 MiB during ordinary play. Use 1 MiB = 1,048,576
bytes. Report private committed bytes and working set separately. Break out
tracked engine allocations, GPU allocations and budget, swapchain estimates,
attachments, staging, textures, meshes, animation and audio. Treat working set
and engine allocations as diagnostic views of process memory. Count distinct
physical CPU/GPU backing separately and shared backing once when identified.
Tag uncertain driver/shared accounting and outside-process allocations. Report
simultaneous combined peaks, cold and warmed runs, load and map transitions,
large waves, and four-player sessions. This is an application target. Windows
and GPU drivers retain control of their own allocations.

Use baked light, restrained dynamic lighting, compressed shared materials and
modular geometry to establish the intended late-2000s FPS presentation at N13.
Start with 512–1024 textures, selected 2048 centerpieces, 3,000–5,000 triangles
for most enemies and weapons, and roughly 6,000 for the third-person player.
Keep the first-person viewmodel separate. Preserve movement and gameplay and
measure visible geometry, GPU time, memory, and frame pacing before tuning.
