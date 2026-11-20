# Memory budget — Cat Arena on Quake3e

Target: 192 MiB combined (128 MiB CPU + 64 MiB GPU), 480 MiB as the secondary ceiling. Method
as in docs/ROADMAP_N8_N20.md (private committed CPU memory + engine-made device-local GPU
allocations; host-visible counted once; driver / OS memory reported separately).

**Nothing has been measured on Quake3e yet.** Measurements start in Q3E-B (first map load) and
become the report in Q3E-F.

## Fixed reservations found in the engine source (dec793de)

| Item | Default | Minimum / range | Where |
|---|---|---|---|
| Hunk (client) | 128 MiB, one `calloc` at start (committed on Windows) | `com_hunkMegs` ≥ 64 | `qcommon/common.c` 44–56, ~2188 |
| Hunk (dedicated server) | 56 MiB | ≥ 48 | same |
| Zone | 12 MiB + extra segments on demand | `com_zoneMegs` | same |
| Vulkan vertex buffer | 4 MiB (8 MiB "high") | grows for big maps | `renderervk/vk.h` |
| Vulkan staging buffer | 2 MiB (24 MiB "high") | — | same |
| Vulkan image memory | 32 MiB chunks | — | same |
| Swapchain at 1920×1080 | 3 × 7.9 MiB colour + depth / stencil | — | measured on lavapipe: 3 images, D24S8 |

Consequence: with defaults the client commits about 140 MiB of CPU memory for hunk + zone before
loading any Cat Arena data, more than the 128 MiB CPU budget. Q3E-F measures the real hunk
high-water mark per map (`\meminfo`) and sets `com_hunkMegs` from it.

## Reference: N10 engine (for the comparison in Q3E-F)

N8 measurements on lavapipe: renderer Vulkan allocations 104–150 MiB, working set 437–535 MiB
(docs/N8_MULTIPLAYER.md §4). No N10 measurement on Windows hardware exists.
