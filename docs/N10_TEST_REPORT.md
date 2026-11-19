# N10 gameplay checkpoint test report

The latest source is `9b340fa56d622f3652372951f72d23da98e746cc` on
`n10-gameplay`. It follows the first N10 build,
`80428e7b8d02825a3072536b4c59ca52396b28a9`. The four maps, weapon models,
and creature assets use the same files as N9. The 762 asset and shader files in
the Windows packages match in size and ZIP CRC.

| Status | Scope | Result |
| --- | --- | --- |
| Implemented | Co-op combat | Host-authoritative explosive flight and detonations, shooter-only elimination credit, guest health feedback, and world-space muzzle flashes, tracers and beams |
| Implemented | Interactive play | Damage numbers, incoming hit feedback, downed status and configurable key hints. Fog, lightmap intensity, overbright, effects detail, marks, brass, blood, gibs and rail trail duration take effect. |
| Compiled | Linux x64 desktop and Windows x64 Release | C++20 engine and 23 offline SPIR-V shaders built on both platforms. |
| Tested | Windows x64 Release CI | [Run 43460222820](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43460222820) passed 42 of 42 GPU-free tests, including WinHTTP and four-player loopback through each relay. |
| Executed | Packaged Windows x64 executable | Headless self-test returned 0. Vulkan detection returned the expected missing-loader error on the GPU-free runner. The 766-file package includes SDL3, all 23 shaders, four maps, 16 weapon models, 20 creature meshes and audio. |
| Tested | Linux GPU-free tests | 41 of 41 passed, including simulation tapes, four-player combat through both relay implementations, UI and audio. |
| Tested | Linux AddressSanitizer and UBSan | Weapon effects and four-player co-op through each relay passed. Leak detection was disabled for this run. |
| Tested | Linux software Vulkan | 30 of 30 passed in the full suite, including the render-path matrix, lighting options, image regressions, simulated Vulkan 1.1, menus and co-op. The synthetic input test missed crouch in an earlier run and missed slide on the second repetition in a follow-up. The deterministic traversal fixtures passed. |
| Executed | Installed Linux package | Ran from a separate working directory. The headless test passed and the Vulkan 1.1 window test exercised Reactor Bastion hosting, three guests, three name tags, guest muzzle flash on the host, downed/revive, pause, disconnect and shutdown. Vulkan validation reported zero warnings and errors. |
| Tested | Reactor Bastion combat | 1,084 simulation ticks, starting at wave 5 with 20 active enemies at peak, 11 volleys, 6 detonations, 1 elimination, 1 death and a reset to wave 1. The report and five captures match the first N10 run byte for byte. Vulkan validation reported zero warnings and errors. |
| Unverified | Physical Windows and legacy PC | Graphics, display switching, audio output and hotplug, and input on Windows hardware. FPS, frame-time lows and the combined 480 MiB target require a hardware run. |
| Unverified | Multiplayer across machines | WAN/TLS, the hosted public service, and multi-PC play under sustained waves. |

The co-op test verifies host rocket flight, detonation and removal on a
spectator guest. It also covers hitscan and explosive kill attribution, positioned
remote volleys, health replication, downing, a four-second revive and squad wipe.
The weapon effects test verifies that remote fire draws world-space flashes,
lights and traces, respects trail duration and blood settings, and leaves the
simulation hash unchanged. The window test checks that guest fire appears in the
host's rendered effect batch. Linux window tests use SDL offscreen video and
dummy audio. Reactor Bastion used software Vulkan at
640×360. The captured run exercises gameplay and does not yield comparable GPU
performance data. Remaining feature work is in [the parity tracker](FEATURE_PARITY.md).

The first Windows CI run of the co-op effects revision caught an SDL `main`
macro collision in the new window self-test. The test now reads the effects
batch through `Frame::world_effects()`. In the following run the original
relay protocol passed all 78 checks, then the test harness failed during
cleanup because the relay process still held its working directory open.
The harness now waits for the process to exit before removing that directory.
Both original relay tests passed eight consecutive Linux runs after the fix.
Before the fix the two Linux four-player relay tests also passed ten runs
each. The standalone relay process on Linux passed 33 checks with four game
clients over TCP loopback.

The final Windows co-op fixture sent 194 snapshot rounds at 81.1 KiB/s total
to three guests over 17.7 simulated seconds. The first N10 checkpoint sent
78.9 KiB/s over the same script before it included crouch offsets in player
snapshots. N9 sent 159 rounds at 80.4 KiB/s over 14.5 simulated seconds. The
N10 script adds an explosive test and a longer frozen-wave segment. See
`n10-evidence` for the test outputs.

Windows artifact ID `14055269382` has SHA-256
`2aaf2263bc8b1bb60ce4cebd13438dc6d1b98752a0e88360bf620a3f67fcece5`.
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

Linux testing used Mesa llvmpipe with LLVM 19.1.7 and Vulkan loader 1.4.309.
The original relay compatibility test requires Node.js and the `ws` module
via the `CAT_NODE_WS_DIR` CMake option.

## Arsenal and input follow-up

Code revision `9a7979895667814c9842e4cf98995080da5b9186` loads the three original
Arsenal placements. The flag, two turret bases, two heads and two barrels total
502 submitted triangles before material passes. Instances share four MD3 files.
The native path interpolates vertices and attachment tags, samples baked ambient
light and uses the original multistage materials. Turret idle phases are
deterministic and tracking uses the local player's position in gameplay.

Linux passed 41 GPU-free checks and 30 software-Vulkan checks. The tests verify
asset decoding, winding, tag attachment, turret tracking, and a change in
rendered pixels during flag animation. A 960×540 flag capture was byte-identical
on Vulkan 1.1 and the automatic Vulkan 1.4 path. Both runs had zero validation
warnings and errors. The source archive contains the capture and test logs in
`docs/n10-arsenal-evidence`.

The SDL input fixture now delivers scheduled SDL events at simulation ticks.
Its revised sequence exercises sprint, slide, jump, crouch, fire, reload and swap.

[Windows run 43543747717](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43543747717)
compiled and packaged Windows x64, passed all 42 GPU-free checks and ran the
packaged headless test with exit code 0. The runner reports a missing Vulkan
loader, as expected for that environment. Physical Windows GPU play remains open.

Windows artifact `14093536125`, SHA-256
`d72a9f9461b2ee6640d803c4da8131c7de3884a127cb6983a43eeb81105a4c89`.
The downloaded ZIP passed decompression and package validation.
