# N11 Windows baseline candidate

This candidate records frame intervals and memory during ordinary gameplay through
the menu, input, audio and renderer paths. The primary acceptance machine is the
Pentium G2020 with GTX 660, 8 GiB DDR3 and a 5400 RPM drive. Hardware acceptance
is open. Keep the N11 executable and original captures for comparison with
future milestones.

## Build and package

On Windows x64, install Visual Studio 2022 C++ Build Tools with the Windows SDK,
CMake 3.24 or newer, and Git. From the source directory run:

```powershell
cd native
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N11_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N11_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
```

The installed folder contains CatArena.exe, SDL3, assets and offline SPIR-V.
It runs without developer tools. Run `ctest --preset windows-release-gpu` on a
Vulkan-equipped Windows PC for the rendering and window test suite. The
[Windows acceptance checklist](N10_ACCEPTANCE.md) covers direct play.

## Capture on the reference PC

Extract the Windows ZIP to the target drive and open PowerShell in the game
folder. Start the game with a fresh, named benchmark profile so the benchmark
does not alter your everyday settings.

```powershell
.\CatArena.exe --map reactor-bastion --seed 10 --wave 5 --weapons 4,2 --no-validation --benchmark-csv reactor-wave5.csv --log-file reactor-wave5.log --game-settings n11-settings.txt
```

1. Configure the benchmark profile using the in-game settings before the
   measured launch. Select Normal graphics and 1920×1080. Exit to save the
   profile. Record fullscreen or windowed mode, refresh, VSync, and cap.
   Keep those settings and the driver version with the capture.
2. Start the measured launch, enter the arena, and play for at least 60 seconds.
   Exit through the menu and keep the raw CSV and log. For a separate uncapped run,
   use `--no-vsync --frame-cap 0` and check the reported present mode.
3. Keep the cold-launch log, which reports map and asset loading phases. Repeat
   the same encounter after restarting the map for the warmed result. Capture
   a four-player session with separate PCs, identifying the host and each
   guest. Keep each machine's CSV.
4. From the source directory, use Python 3.10 or later to summarize the copied
   CSV. Analysis is optional and happens outside the game.

```powershell
python native/tools/n11/summarize.py reactor-wave5.csv --output reactor-wave5-summary.json
python native/tools/n11/summarize.py reactor-wave5.csv --warmup-seconds 10 --output reactor-wave5-warm-summary.json
```

## Measurement definitions

| Field | Meaning |
| --- | --- |
| Frame interval | Wall time between submitted gameplay frames, including pacing and waits. The first row of each segment has no interval. Menus, lost focus, and swapchain interruptions delimit segments. |
| Average FPS | Submitted-frame intervals divided by their summed duration. |
| 1% and 0.1% lows | Reciprocal of the mean interval in the slowest 1% or 0.1%. Requires at least 100 or 1,000 intervals, respectively. |
| CPU frame time | Application work and Vulkan command recording measured on the main thread. Excludes frame cap sleep, acquire/fence waits, submit and present. |
| GPU frame time | GPU timestamp interval for the previous submitted frame, where supported. |
| Draws and triangles | Commands and triangles submitted, including all passes, instances, effects and UI. Counts are before rasterization and occlusion. |
| Working set | Resident process bytes sampled about four times per second, plus the OS lifetime peak. |
| Private commit | Windows process private committed bytes sampled alongside working set. |
| Vulkan allocated bytes | Sum of live allocations made by the renderer through Vulkan. The metric includes host-visible allocations and excludes swapchain-owned images and other opaque driver allocations. |

Keep cold and warmed captures. The raw CSV retains long frame intervals,
including first-use stalls during gameplay. The current counters do not define
a combined RAM and VRAM total. N12 must identify backing and memory heaps,
distinguish owned and estimated allocations, and measure simultaneous peaks.

## Local verification

The installed Linux package ran from an empty working directory. A four-player
loopback smoke test exercised Reactor Bastion, guest fire, downed status, revive
and disconnect. It used the Vulkan 1.1 compatibility path on Mesa llvmpipe,
SDL offscreen video and dummy audio. Validation reported zero errors and warnings.
The raw capture and log are in [n11-evidence](n11-evidence).

| Software-Vulkan smoke-test metric | Result |
| --- | ---: |
| Resolution and present mode | 1280×720 FIFO |
| Measured duration and intervals | 7.85 s, 234 |
| Average FPS and 1% low | 29.79, 17.02 |
| 0.1% low | Insufficient intervals |
| Mean CPU and GPU time | 1.73 ms, 30.03 ms |
| Average draw calls and submitted triangles | 12.22, 30,275 |
| Peak active enemies | 10 |
| Peak process working set | 425.48 MiB |
| Peak tracked Vulkan allocations | 104.04 MiB |
| Windows private commit | Unavailable in this Linux run |

Software Vulkan backs its resources in system memory. The memory columns can
overlap. This run validates capture and analysis and supplies no GTX 660
performance or combined-memory acceptance result. Linux also passed 41
GPU-free tests and 30 software-Vulkan tests.

A separate software-Vulkan run started at wave 5 and reached 20 active enemies.
The fixture then freezes the wave to check revive and disconnection. It is a
short validation workload with an active-enemy peak, not a sustained
large-wave benchmark.
