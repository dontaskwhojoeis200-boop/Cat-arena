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

**Crash reports.** If the game crashes it shows where it saved
`CatArena-crash-<date>-<time>.txt` (build, reason, faulting module and offset,
Windows version, last 64 log lines) and a `.dmp` minidump of the same name,
in `CrashReports` next to `CatArena.log`. The log and reports go next to
`CatArena.exe` when that folder is writable, otherwise to
`%APPDATA%\CatArena\FeralClaw`. `CatArena.exe --crash-test access` crashes
on purpose to check this.

**The executable** has an icon (original artwork, `tools/n9/make_icon.py`),
version information (Explorer → Properties → Details) and an application
manifest (Windows 10/11, long paths). The device baseline is Vulkan 1.1; newer
features stay behind runtime capability checks.

N9 was built in two parts: the display, input, focus, audio and Vulkan 1.1
work on branch `n9-windows` (2026-11-13), then, on branch `n9`, the final N8
merged in, crash reporting, the EXE resources and the log-folder fallback
(2026-11-18). Details: [docs/DEVELOPMENT_LOG_N9.md](docs/DEVELOPMENT_LOG_N9.md).

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

6. Run `CatArena.exe --crash-test access`: a message box names the crash
   report; check that the `.txt` and `.dmp` files are there.

Physical Windows display switching, mouse behavior, audio-device changes and
legacy GPU performance need hardware testing. The roadmap (Windows-only,
192 MiB combined memory target, Quake III Arena-inspired art) is
[docs/ROADMAP_N8_N20.md](docs/ROADMAP_N8_N20.md).

## Development evidence

**Status of this package (branch `n9`):** Linux 75/75 tests; Windows GitHub Actions run
https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/44469465569 — build, package,
45/45 GPU-free tests, crash reports with minidumps, 15 rounds of the WinHTTP (`wss://`) tests; game
download: artifact `CAT_ARENA_FERAL_CLAW_N9_WINDOWS`
(https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/44469465569/artifacts/14504490142).
Not yet run on a Windows PC with a GPU.

This package also fixes an intermittent hang when leaving a server over `wss://` (WinHTTP), the
connection the game uses with the Render relay.

Results of the `n9` branch (this package) are in
[docs/DEVELOPMENT_LOG_N9.md](docs/DEVELOPMENT_LOG_N9.md) and
[docs/TEST_MATRIX.md](docs/TEST_MATRIX.md). Recorded on `n9-windows` before the merge:

- **Implemented** — desktop display controls and fallback, focus-safe
  relative input, configurable movement and fire button, and audio recovery.
  The render-pass compatibility path now accepts Vulkan 1.1. Vulkan 1.2–1.4
  capability detection remains active on those devices.
- **Tested on Linux** — 41 GPU-free tests passed, including the native
  and original Node.js relays. The software-Vulkan suite passed 29 of 30
  tests on the first run. The synthetic input test missed the slide at that
  run's timing. It passed in an isolated rerun, as did the N8 comparison.
  Window tests use SDL's offscreen video and dummy audio drivers.
- **Unverified** — physical Windows launch with a GPU, exclusive fullscreen
  and refresh switching, raw mouse behavior on physical devices, and WASAPI
  hotplug. N10–N20 hardware and gameplay acceptance remain pending.

Reactor Bastion retains the existing N4 collision and navigation data, covered
by the traversal, navigation and enemy replay checks. N10 can use this package
to test outstanding gameplay and visual parity items on the legacy rig.
