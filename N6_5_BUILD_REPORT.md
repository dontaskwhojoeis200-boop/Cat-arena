# N6.5 build report — Windows x64 build, packaging and verification

Date: 2026-11-11 (CI times are UTC, 2026-11-12). Repository: https://github.com/dontaskwhojoeis200-boop/Cat-arena
(branch `main`). Workflow: `.github/workflows/windows-build.yml` on GitHub-hosted `windows-2022` runners.

## Summary

| Requirement | Status |
|---|---|
| Windows x64 executable compiled and linked | **Done by GitHub Actions** (MSVC 19.44, VS 2022 17.14, Windows SDK 10.0.26100). Successful runs: [43169039455](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43169039455) and [RUN4_ID](RUN4_URL) (final, section 6). |
| `CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip` | **Produced and uploaded** as the workflow artifact `CAT_ARENA_FERAL_CLAW_N6_WINDOWS` (RUN4_ARTIFACT). GitHub serves it as `CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip`; it contains the game folder (CatArena.exe, SDL3.dll, assets with the 21 shaders, README.txt, THIRD_PARTY_LICENSES.txt). Kept 30 days (workflow setting). |
| Package checks on Windows | PASS in CI: required files present, no development files; `dumpbin`: x64, no VCRUNTIME / MSVCP / UCRT import (static runtime), SDL3.dll imported, vulkan-1.dll delay-loaded. |
| Tests on Windows | PASS in CI: all 34 GPU-free tests (unit tests and every legacy-tape replay; sections 6-7). The 28 GPU / window tests cannot run on the GPU-less runner. |
| Start on Windows | Checked in CI without a GPU: `CatArena.exe --headless-test` exit 0; `--vk-info` stops cleanly with "Vulkan is not available ... install or update the GPU driver" (exit 1, no crash). |
| No Vulkan SDK / no VC++ Redistributable needed | Done: generated `vulkan-1.lib`, glslang built from source, static MSVC runtime (verified with dumpbin above). |
| Gameplay preserved | Yes, and closer to the legacy than N6: the simulation now uses correctly rounded sin / cos / tan / atan2 / pow like Chromium (section 3.2); the legacy-tape replays are bit-exact on more ticks on Linux and pass on Windows. |
| Rendering, input and performance on Windows with a GPU | **UNVERIFIED** — no GPU on hosted runners and no Windows PC here (section 9). |

## 1. Build environments

| | Windows (GitHub Actions, final run) | Linux (development machine) |
|---|---|---|
| OS / image | windows-2022 runner image 20261101.353.1 (Windows Server 2022, 10.0.20348) | Ubuntu 24.04 userland, kernel 6.18 |
| Compiler | MSVC 19.44.35229 (VS 2022 Enterprise 17.14, MSBuild 17.14.60) | GCC 13.3.0, clang 18.1.3 |
| SDK / CMake | Windows SDK 10.0.26100.0, CMake 3.31.6 | CMake 3.28.3, Ninja 1.11.1 |
| Vulkan | none on the runner (no GPU) | Mesa lavapipe (software), loader 1.4.309, validation layers |

## 2. Dependencies

| Dependency | Version | How it is obtained | Shipped |
|---|---|---|---|
| SDL3 | 3.2.10 | `CAT_FETCH_SDL`: tarball, SHA-256 pinned (or an installed SDL3 >= 3.2.10) | SDL3.dll / libSDL3.so.0 |
| Vulkan headers | 1.4.309 | vendored, `native/third_party/vulkan-headers` | no (compiled in) |
| Vulkan loader | any (driver-installed) | Windows: `vulkan-1.lib` generated from `tests/vulkan_baseline_imports.txt`; Linux: system libvulkan | **no** (comes with the GPU driver) |
| glslang | 15.2.0 (tag vulkan-sdk-1.4.309.0, commit 7200bc12) | installed glslangValidator, or `CAT_FETCH_GLSLANG` (git, pinned commit) | no (build tool) |
| stb_image / stb_image_write | vendored | `native/third_party/stb` | no (compiled in) |
| fdlibm 5.3 (reduction of huge trig arguments only) | — | ported into `include/cat/js_math.hpp` | no (compiled in; notice in THIRD_PARTY_LICENSES.txt) |
| MSVC runtime | VS 2022 | static (`MultiThreaded`) | no (linked in) |

## 3. Source changes for Windows

### 3.1 Port (before the first CI run)

- **UTF-8 paths** (`include/cat/utf8_path.hpp`): SDL returns UTF-8, MSVC's narrow paths use the ANSI
  code page and `path::string()` throws for characters it cannot represent; a folder such as
  `C:\Users\Zoë\Downloads` would otherwise have stopped the game at start. Unit test in `core_foundation`.
- Asset references starting with "/" are rejected explicitly (not absolute on Windows).
- `CatArena.exe`: GUI subsystem (no console), `vulkan-1.dll` delay-loaded and checked first with
  `SDL_LoadObject`, fatal errors in a message box, log in `CatArena.log` next to the executable.
- MSVC flags: `/W4 /permissive- /utf-8 /Zc:__cplusplus /bigobj`, `_CRT_SECURE_NO_WARNINGS`,
  `NOMINMAX`, 8 MiB main-thread stack, `/fp:precise` for the simulation libraries.
- Controls for a double-clicked game: pause while the mouse is released (help panel), F5 restart,
  F11 fullscreen; maps 1-4, Esc / Esc-again quit were already there.

### 3.2 Fixes found by the Windows CI runs

1. **MSVC compile error** (run 1): `SDL_main.h` defines `main` as `SDL_main` on Windows, which also
   renamed the `main` members of `fx::Frame` and `cat::Overlay` (C2039 in main.cpp). The include now
   comes after the code that uses those members. Everything else (all libraries, the renderer, the
   tests) compiled under MSVC without changes.
2. **Cross-platform determinism** (run 2): one replay test failed on Windows —
   `physics_legacy_trajectories`, Helios scenario random_3_z-4: max error 1.30e-9 at tick 2111
   (tolerance 1e-9); bit-exact on 15,413 of 21,820 ticks vs 21,725 on Linux. Cause: MSVC's CRT
   `sin` / `cos` / `atan2` differ in the last bit from glibc's, and the difference grows over thousands
   of ticks. Investigation (docs/DEVELOPMENT_LOG_N6_5.md §7):
   - The legacy fixtures come from Playwright's Chromium 153. Its `Math.sin`, `cos`, `tan` and
     `atan2` are **correctly rounded** (on 200,000 arguments each, every one of the 605 results where
     glibc differs from Chromium is the correctly rounded value); its `Math.pow` equals glibc's on
     all 200,000 (glibc's pow is correctly rounded except about 1 in 30,000 arguments).
   - Node (built with GCC) uses fdlibm instead; a port of fdlibm was tried first and matched Node but
     not Chromium (2-8 % of results differ), so it was not used.
   - New `include/cat/js_math.hpp`: sin, cos, tan, atan2 and pow evaluated in double-double
     arithmetic (about 2^-100 relative error) and rounded once, using only IEEE +, *, / (no FMA, no C
     runtime transcendental functions). Every platform therefore computes the same bits, and they are
     the correctly rounded ones. Tables generated by `tools/js_math/gen_tables.py`. The simulation
     and effect libraries (player, combat, enemies, weapon and enemy effects) use it.
   - New test `js_math_browser`: 31,127 results recorded from Chromium
     (`tools/js_math/record_chromium_math.cjs`): sin / cos / tan / atan2 bit-exact on every record;
     pow bit-exact except 3 records where Chromium (glibc) is 1 ulp off the correctly rounded value.
   - Cost: about 0.2 µs per call instead of 0.01 µs; the weapon-effect replay takes 1.26 s instead of
     0.85 s for 18,220 ticks (+0.02 ms per tick), well inside the 8.3 ms of a 120 Hz tick.

No other gameplay code changed.

## 4. Build configuration

- Windows: `cmake --preset windows-release` (Visual Studio 17 2022, x64, Release), options
  `CAT_BUILD_DESKTOP=ON CAT_FETCH_SDL=ON CAT_FETCH_GLSLANG=ON CAT_WINDOWS_GUI=ON CAT_MILESTONE=N6`.
- Linux (as tested): Ninja, Debug / Release, GCC and clang, ASan + UBSan.

## 5. Build commands

Windows (developer PC or CI):

```bat
cd native
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS -DZIP=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip -P tools/package/zip_package.cmake
```

GitHub: every push to `main` touching `native/`, `assets/`, `README_WINDOWS.md` or the workflow runs
it; Actions > windows-build > Run workflow starts it by hand. The game folder is the artifact
`CAT_ARENA_FERAL_CLAW_N6_WINDOWS`; build logs are the artifact `windows-build-logs-N6`.

## 6. Windows CI runs

| Run | Commit | Result |
|---|---|---|
| [43164340554](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43164340554) | d52f51c (N6.5 source + workflow) | **failed at Build**: C2039 in main.cpp (section 3.2.1). All other targets compiled and linked. |
| [43164827201](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43164827201) | 046326b (SDL_main.h fix) | build, package, validation, ZIP and upload **passed**; tests 32/33 (`physics_legacy_trajectories` 1.30e-9 > 1e-9, section 3.2.2); start without GPU passed. Run marked failed. |
| [43169039455](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43169039455) | cd65a5a (correctly rounded math) | **success**: every step; tests 34/34; artifact 13921267822 (ZIP 29,987,525 bytes, SHA-256 4024a11f…bc180). |
| [RUN4_ID](RUN4_URL) | RUN4_SHA (fdlibm notice in the licences, replay summary in the job log, docs) | RUN4_RESULT |

Final run details (RUN4_ID):

- Build: 0 errors; MSVC warnings: RUN4_WARNINGS (all in SDL's own sources, none in project code).
- Package: RUN4_PACKAGE.
- `dumpbin /dependents CatArena.exe`: SDL3.dll, KERNEL32.dll; delay-load: vulkan-1.dll; x64.
  SDL3.dll imports only Windows system DLLs. No Visual C++ runtime DLL anywhere.
- ZIP: RUN4_ZIP.
- Tests: RUN4_TESTS.
- Start without GPU: `--headless-test` exit 0; `--vk-info` exit 1 with the "Vulkan is not available"
  message (expected on a runner without a GPU driver).

## 7. Replay results (legacy tapes), Windows and Linux

RUN4_REPLAY_TABLE

## 8. Known issues and risks (Windows)

- **Not run on Windows with a GPU.** Rendering, swapchain, window, mouse capture, fullscreen and
  performance on Windows are unverified; the hosted runner has no GPU. (The same renderer passes all
  28 GPU tests on Linux / lavapipe, including pictures compared with the legacy game.)
- The Enemies replay on Helios is bit-exact on 18,410 of 19,200 ticks (worst 1.8e-15); the
  remaining tiny differences (player yaw from tick 2,331 of stand_still, velocities in fight_spin)
  are within the tolerance and not yet traced. They are the same on Windows and Linux.
- Windows SmartScreen will warn about an unsigned executable.
- F11 fullscreen could not be confirmed (no window manager under Xvfb, no GPU on the runner).
- Mouse-wheel weapon switching does not exist in the original game and was not added.
- GitHub keeps the artifacts for 30 days; rerun the workflow to produce them again.

## 9. Platform requirements (end user)

Windows 10 / 11 x64; a GPU and driver with Vulkan 1.2 or newer; about 100 MB of disk. Nothing else
(no Vulkan SDK, no Visual C++ Redistributable, no development tools).

## 10. Testing on a physical Windows PC (to close the unverified items)

1. Download the artifact, extract it, double-click CatArena.exe; check: the window opens with the
   help panel; click captures the mouse; mouse-look, WASD, Shift, Space, Ctrl, C; firing, R, Q;
   enemies spawn, attack and die; waves advance; Esc pauses, click resumes; F5 restarts; 1-4
   change maps; F11; Esc twice quits. Note FPS (`CatArena.log` lists device and driver).
2. From the source: `ctest --preset windows-release` (GPU-free) and `ctest --preset
   windows-release-gpu` (rendering, pictures compared with the legacy game, window self-tests).
3. Report CatArena.log and the CTest output.
