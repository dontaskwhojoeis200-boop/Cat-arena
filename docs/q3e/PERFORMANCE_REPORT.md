# Performance report — Cat Arena on Quake3e

**No performance measurement exists yet.** No Cat Arena content runs on Quake3e; the engine has
not been run on the reference PC. Nothing here may be quoted as a result.

Reference hardware (owner's PC): Pentium G2020, GTX 660, 8 GB DDR3, 5400 rpm HDD, 1920×1080.
Targets: 60 FPS sustained minimum, 120+ FPS stretch.

Planned scenarios (Q3E-F; owner runs them on the reference PC, Claude prepares the build and the
`timedemo` / benchmark scripts): empty map, small / medium / large wave, heavy projectile combat,
four-player co-op. Measured per scenario: average FPS, 1 % and 0.1 % lows, CPU and GPU frame time,
draw calls, visible triangles, active enemies, memory (MEMORY_BUDGET.md), loading time,
frame-time spikes.

Baseline for comparison: the N10 Windows build (CI run 44560790833). Its only hardware report is
the owner's N6 observation (up to 144 FPS, occasional stutter, no lows recorded); it is context,
not a benchmark.
