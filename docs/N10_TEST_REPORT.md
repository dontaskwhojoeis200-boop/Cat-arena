# N10 gameplay checkpoint test report

The build under test is `80428e7b8d02825a3072536b4c59ca52396b28a9`
on `n10-gameplay`. The comparison build is N9. The four maps, weapon models,
and creature assets use the same files as N9.

| Status | Scope | Result |
| --- | --- | --- |
| Implemented | Co-op combat | Host-authoritative explosive flight and detonations, shooter-only elimination credit, and guest health feedback |
| Implemented | Interactive play | Damage numbers, incoming hit feedback, downed status and configurable key hints. Fog strength, lightmap intensity and overbright take effect. |
| Compiled | Linux x64 desktop and Windows x64 Release | C++20 engine and 23 offline SPIR-V shaders built on both platforms. |
| Tested | Windows x64 Release CI | [Run 43455364840](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43455364840) passed 42 of 42 GPU-free tests, including WinHTTP and four-player loopback through each relay. |
| Executed | Packaged Windows x64 executable | Headless self-test returned 0. Vulkan detection returned the expected missing-loader error on the GPU-free runner. The 766-file package includes SDL3, all 23 shaders, four maps, 16 weapon models, 20 creature meshes and audio. |
| Tested | Linux GPU-free tests | 41 of 41 passed, including simulation tapes, four-player combat through both relay implementations, UI and audio. |
| Tested | Linux software Vulkan | 30 of 30 passed, including the render-path matrix, lighting options, image regressions, simulated Vulkan 1.1, window input, menus and co-op. |
| Executed | Installed Linux package | Ran from a separate empty working directory. The headless test passed and the Vulkan 1.1 window test exercised Reactor Bastion hosting, three guests, three name tags, downed/revive, pause, disconnect and shutdown. Vulkan validation reported zero warnings and errors. |
| Tested | Reactor Bastion combat | 1,084 simulation ticks, starting at wave 5 with 20 active enemies at peak, 11 volleys, 6 detonations, 1 elimination, 1 death and a reset to wave 1. Five captures, with zero Vulkan validation warnings and errors. |
| Unverified | Physical Windows and legacy PC | Graphics, display switching, audio output and hotplug, and input on Windows hardware. FPS, frame-time lows and the combined 480 MiB target require a hardware run. |
| Unverified | Multiplayer across machines | WAN/TLS, the hosted public service, and multi-PC play under sustained waves. |

The co-op test verifies host rocket flight, detonation and removal on a
spectator guest. It also covers hitscan and explosive kill attribution, health
replication, downing, a four-second revive and squad wipe. Linux window tests use
SDL offscreen video and dummy audio. Reactor Bastion used software Vulkan at
640×360. The captured run exercises gameplay and does not yield comparable GPU
performance data. Remaining feature work is in [the parity tracker](FEATURE_PARITY.md).

The Windows N10 loopback fixture sent 194 snapshot rounds at 78.9 KiB/s total
to three guests over 17.7 simulated seconds. N9 sent 159 rounds at 80.4 KiB/s
over 14.5 simulated seconds. N10 adds an explosive test and a longer frozen-wave
segment, so these results describe the two fixtures and do not measure a
bandwidth improvement. See `n10-evidence` for the test outputs.

Windows artifact ID `14053029168` has SHA-256
`49581f7cbb612e3df7c8a6b8d5b932b4a864d79b8608191b29ef6513e60d1c6f`.
The downloaded ZIP passed decompression and package validation. The packaged
`CatArena.exe` is a Windows x64 PE executable.

## Reproduction

The [N10 build instructions](../README_N10.md) reproduce the Windows package.
Run `ctest --preset windows-release-gpu` from `native` on a Vulkan-equipped
Windows PC. For Linux, with `build` configured for the desktop target and a
Vulkan driver supporting SDL offscreen windows, run:

```sh
ctest --test-dir build -LE gpu --output-on-failure
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ctest --test-dir build -L gpu -j 1 --output-on-failure
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build/cat_arena --coop-selftest --vk-baseline
./build/cat_arena --offscreen 640x360 --map reactor-bastion --vk-baseline \
  --seed 10 --wave 5 --weapons 4,2 \
  --walk 'w+shift:120,w+jump+fire@2:120,w+slide+fire@-2:120,swap:2,fire@3:240,reload:2,a+fire@-2:240,fire@2:240' \
  --capture-every 360 --capture-dir n10-reactor --report n10-reactor.json
```

Linux testing used Mesa llvmpipe 19.1.7 LLVM and Vulkan loader 1.4.309.
The original relay compatibility test requires Node.js and the `ws` module
via the `CAT_NODE_WS_DIR` CMake option.
