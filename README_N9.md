# Cat Arena: Feral Claw — Windows platform integration

N9 builds on the N8 Windows multiplayer checkpoint. The game exposes display
resolution and fullscreen refresh in **Settings → HUD / UI**. The default
fullscreen mode uses the desktop. The game checks the active monitor's advertised
modes for a selected resolution and refresh. An unavailable selection falls
back to desktop fullscreen and appears in the status line and log. F11 returns
to the saved window size. Windowed sizes fit inside the monitor's usable area.

**Settings → Controls** includes WASD, ZQSD and arrow-key movement, mouse
sensitivity, vertical inversion, raw relative input and a left/right fire
button. With ZQSD, use Tab to switch weapons. Menu navigation continues to use
arrows, Enter, Escape and Tab. Losing focus clears held gameplay input and
releases capture. In multiplayer, the match continues. SDL opens the default
playback device and retries when the active device disappears or a playback
device arrives while audio is unavailable.

## Build and package

On Windows x64 with Visual Studio 2022 C++ Build Tools, the Windows SDK,
CMake 3.24+, and Git:

```powershell
cd native
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N9_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N9_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
```

The optional original Node.js relay tests use `-DCAT_NODE_WS_DIR=...`, where
the value points to the directory containing the `ws` module. Run
`ctest --preset windows-release-gpu` on a GPU-equipped Windows PC.

## Windows hardware checks

1. Extract the package to a folder with spaces and launch `CatArena.exe` from
   Explorer. Check that the executable finds the adjacent assets and SDL3.
2. Select a supported 1920×1080 refresh rate, toggle F11 twice, then try a mode
   unavailable on that monitor. Check the selected and actual values in
   Settings and `CatArena.log`. Repeat with the game window on a second monitor
   where available.
3. Hold movement and fire while switching focus. Return, resume, and verify
   that capture and input work. Check keyboard and mouse settings, reload
   them on a second launch, then test Settings by keyboard and mouse.
4. Start music and gun effects. Unplug the active playback device and
   reconnect it or select another default device.
5. Run `CatArena.exe --vk-info` and keep its output on the GTX 660. The
   baseline is Vulkan 1.1. Use `--vk-baseline` to force that path.

Physical Windows display switching, mouse behavior, audio-device changes and
legacy GPU performance need hardware testing. The Windows-only roadmap and
480 MiB application-memory target are in [docs/WINDOWS_ROADMAP.md](docs/WINDOWS_ROADMAP.md).
