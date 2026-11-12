# N6.5 build report — Windows x64 build, packaging and verification

Date: 2026-11-11. Source: `CAT_ARENA_FERAL_CLAW_NATIVE_N6_5_SOURCE.zip` (N6 + the changes below).

## Summary

| Requirement | Status |
|---|---|
| Windows x64 executable compiled and linked | **NOT DONE here.** This build machine (Linux) has no MSVC, no Windows SDK and no MinGW, and package installs are blocked. GitHub is not linked to this session, so the workflow could not be run. **No CatArena.exe exists yet; no Windows ZIP was produced.** |
| `CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip` | **Not produced.** The GitHub Actions workflow in the source produces it as a downloadable artifact on its first run. |
| Source ported for MSVC / Windows | Done (section 3). Compiles and passes all tests with GCC 13.3 and clang 18.1; **not compiled with MSVC** (no MSVC here). |
| Reproducible Windows build | Written: `native/CMakePresets.json` preset `windows-release` + `.github/workflows/windows-build.yml`. **Not executed.** |
| No Vulkan SDK / no VC++ Redistributable needed | Implemented: generated `vulkan-1.lib`, glslang built from source, static MSVC runtime. The generated import library was checked with llvm-lib here; the MSVC path itself not run. |
| Packaging (install, validate, zip) | Done and run on Linux: the same CMake install / validate / zip steps made a Linux package that was extracted into an empty folder and played from there. |
| Minimum play controls (start, pause, restart, maps, exit, fullscreen) | Done; checked through SDL events under Xvfb (`window_controls_selftest`). |
| Gameplay preserved | Yes: every replay hash (combat, effects, enemies, animation, trajectories) is identical to N6. |
| Runtime on Windows, Windows GPU tests, performance on Windows | **UNVERIFIED** — needs the workflow run and a Windows PC with a Vulkan GPU (section 7). |

## 1. Build environment used here

| Tool | Version |
|---|---|
| OS | Linux x86_64 (kernel 6.18), Ubuntu 24.04 userland |
| CMake / Ninja | 3.28.3 / 1.11.1 |
| Compilers | GCC 13.3.0, clang 18.1.3 (C++20) |
| Vulkan | Mesa lavapipe (software) via the system loader 1.4.309, validation layers built from source |
| Display | Xvfb 1280x720 |
| LLVM tools | llvm-lib, llvm-nm, lld-link (no Windows headers / CRT, so no Windows linking) |

Windows build environment (as written, not run): GitHub `windows-2022` runner, Visual Studio 2022
(MSVC, preinstalled), Windows 10/11 SDK (preinstalled), CMake from the runner image, Git, Python 3.

## 2. Dependencies

| Dependency | Version | How it is obtained | Shipped |
|---|---|---|---|
| SDL3 | 3.2.10 | `CAT_FETCH_SDL`: tarball, SHA-256 pinned (or an installed SDL3 >= 3.2.10) | SDL3.dll / libSDL3.so.0 |
| Vulkan headers | 1.4.309 | vendored, `native/third_party/vulkan-headers` | no (compiled in) |
| Vulkan loader | any (driver-installed) | Windows: `vulkan-1.lib` generated from `tests/vulkan_baseline_imports.txt`; Linux: system libvulkan | **no** (comes with the GPU driver) |
| glslang | 15.2.0 (tag vulkan-sdk-1.4.309.0, commit 7200bc12) | installed glslc / glslangValidator, or `CAT_FETCH_GLSLANG` (git, pinned commit) | no (build tool) |
| stb_image / stb_image_write | vendored | `native/third_party/stb` | no (compiled in) |
| MSVC runtime | VS 2022 | static (`MultiThreaded`) | no (linked in) |
| GLM, Vulkan Memory Allocator | — | not used by the project | — |

## 3. Changes for Windows (source)

- **UTF-8 paths** (`include/cat/utf8_path.hpp`): SDL returns UTF-8, MSVC's narrow paths use the ANSI
  code page and `path::string()` throws for characters it cannot represent. Paths are now built
  with path operations and printed through `path_utf8`. A folder such as `C:\Users\Zoë\Downloads`
  would otherwise have stopped the game at start. New unit test (`core_foundation`): non-ASCII
  folder and file names round trip, write, read and list.
- Asset references starting with "/" are rejected explicitly (not absolute on Windows).
- `CatArena.exe`: GUI subsystem (no console), `vulkan-1.dll` delay-loaded and checked first with
  `SDL_LoadObject` ("Vulkan is not available ... install or update the GPU driver"), fatal errors in
  a message box, log in `CatArena.log` next to the executable (`--log-file` elsewhere).
- MSVC flags: `/W4 /permissive- /utf-8 /Zc:__cplusplus /bigobj`, `_CRT_SECURE_NO_WARNINGS`,
  `NOMINMAX`, 8 MiB main-thread stack (as Linux), `/fp:precise` for the simulation libraries
  (already in N6). Warnings are not turned into errors on MSVC (they are on GCC / clang).
- Controls for a double-clicked game: pause while the mouse is released (help panel), F5 restart,
  F11 fullscreen; maps 1-4, Esc / Esc-again quit were already there. Automated runs never pause.
- No gameplay code changed.

## 4. Build configuration

- Windows: `cmake --preset windows-release` (Visual Studio 17 2022, x64, Release), options
  `CAT_BUILD_DESKTOP=ON CAT_FETCH_SDL=ON CAT_FETCH_GLSLANG=ON CAT_WINDOWS_GUI=ON CAT_MILESTONE=N6`.
- Linux (as tested): Ninja, Debug / Release, GCC and clang, ASan + UBSan; plus a package build with
  SDL3 and glslang taken through FetchContent (from local copies of the pinned sources, because this
  machine cannot download).

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

GitHub: put the contents of the source ZIP's top folder at the repository root (so that
`.github/workflows/windows-build.yml`, `native/` and `assets/` are at the top), push, then
Actions > windows-build > Run workflow (milestone `N6`). The artifact
`CAT_ARENA_FERAL_CLAW_N6_WINDOWS` downloads as `CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip`. Build logs
are a second artifact. The tree is about 360 MB; its largest file (reference/history/CAT_ARENA_MIGRATION_STEP_02_REFERENCE.zip,
69 MB) is under GitHub's 100 MB file limit (GitHub prints a warning above 50 MB), so Git LFS is not required.

## 6. Results on this machine (docs/n6_5-evidence)

| Test | Result |
|---|---|
| Clean build + CTest, 7 configurations (GCC Debug / Release, clang Release core; GCC Debug / Release, ASan+UBSan, clang Debug desktop) | PASS: 33/33 core x3, 63/63 desktop x4; 0 warnings in project code; no sanitizer report |
| UTF-8 path test added after the matrix, rebuilt and run in the 7 build trees | PASS x7 |
| Replay hashes (combat, effects, 4 trajectories, enemies and animation on 4 maps) | identical to N6 in 3 builds checked |
| Package build like CI (Release, SDL3 + glslang through FetchContent) | configure / build / install OK; 21 SPIR-V files byte-identical to the SDK-built ones; 3 warnings, all inside glslang |
| Package check | PASS: executable, SDL3 runtime, 21 shaders, 4 maps, 16 weapon models, 20 creature meshes, README, licences; 703 files, 91 MiB; no development files. A broken copy was rejected with all 4 planted faults |
| CTest in the package build | PASS: 35/35 without GPU, 28/28 GPU / window tests on lavapipe |
| Extracted ZIP, run from an empty working directory | offscreen fight from wave 3 (19 volleys, 2 kills, 1 gib, 48 enemy projectile impacts, 533 damage taken, 5 player deaths with wave restarts, 171 enemy routes); input self-test, controls self-test, all four maps + resize: PASS, validation 0 |
| `vulkan-1.lib` generation (same CMake function, llvm-lib) | PASS: 151 commands imported from vulkan-1.dll |
| Workflow YAML | parses (PyYAML); not run |
| MSVC compilation, Windows link, CatArena.exe start, Windows GPU tests, Windows performance | **NOT RUN** |

## 7. Known issues and risks (Windows)

- **Not yet compiled with MSVC.** The code compiles warning-free with GCC and clang under
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`, and a scan found no GCC-only
  constructs, but MSVC may report errors that only a real build shows. The first workflow run
  will tell; the build log is uploaded as an artifact.
- **Replay tests on MSVC may differ in the last bits.** The legacy-tape tests were verified with
  glibc's math library; MSVC's `sin`, `cos`, `atan2`, `pow` can differ in the last bit, and over
  thousands of ticks that can exceed the 1e-9 tolerance. If the Windows CI reports such failures,
  they are a determinism finding to fix (e.g. own implementations of those functions), not a broken
  build. The workflow uploads the package before the tests for this reason.
- The hosted runner has no GPU: rendering, windows and input on Windows are only verified on real
  hardware. The workflow also starts the game without a GPU and expects a clean error message.
- Windows SmartScreen will warn about an unsigned executable.
- F11 fullscreen could not be confirmed here (Xvfb has no window manager); the request is made with
  SDL's borderless desktop fullscreen.
- Mouse-wheel weapon switching does not exist in the original game and was not added.

## 8. Platform requirements (end user)

Windows 10 / 11 x64; a GPU and driver with Vulkan 1.2 or newer; about 100 MB of disk. Nothing else
(no Vulkan SDK, no Visual C++ Redistributable, no development tools).

## 9. Testing on a physical Windows PC (to close the unverified items)

1. Download the artifact, extract it, double-click CatArena.exe; check: the window opens with the
   help panel; click captures the mouse; mouse-look, WASD, Shift, Space, Ctrl, C; firing, R, Q;
   enemies spawn, attack and die; waves advance; Esc pauses, click resumes; F5 restarts; 1-4
   change maps; F11; Esc twice quits. Note FPS (`CatArena.log` lists device and driver).
2. From the source: `ctest --preset windows-release` (GPU-free) and `ctest --preset
   windows-release-gpu` (rendering, pictures compared with the legacy game, window self-tests).
3. Report CatArena.log and the CTest output.
