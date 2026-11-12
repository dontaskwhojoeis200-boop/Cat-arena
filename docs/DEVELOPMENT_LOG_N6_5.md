# Development log — N6.5 (Windows x64 build, packaging, CI)

2026-11-11. Runner: Linux x64 (as N6). Goal: a reproducible Windows x64 package of the N6 game.

## 1. What this runner can and cannot do

- No Windows toolchain is available here: no MSVC, no Windows SDK, no MinGW-w64; package installs
  are blocked. clang, lld-link and llvm-lib exist, but without Windows headers and C runtime libraries
  nothing can be compiled for Windows. Wine is not installed.
- GitHub is reachable only for repositories attached to the session; no GitHub account is linked, so
  the workflow could not be pushed or run.
- Consequence: no CatArena.exe was built or run in N6.5 here. Everything that does not need Windows
  was done and checked on Linux with the same CMake code; the Windows-only parts are written for
  windows-2022 GitHub runners and documented as not yet executed.

## 2. Review of the N6 tree for Windows

- Already portable: SDL3 3.2.10 (pinned fetch), vendored Vulkan headers 1.4.309, Vulkan 1.0 + WSI
  link surface (all newer commands resolved at run time), `SDL_main.h` (WinMain with UTF-8 argv),
  assets located next to the executable (`SDL_GetBasePath`), binary file I/O everywhere, the
  Linux-only test layer guarded, `-ffp-contract=off` only for non-MSVC (MSVC: `/fp:precise`).
- Fixed:
  - Paths: SDL hands out UTF-8, but on Windows `std::filesystem::path(std::string)` and
    `path::string()` use the ANSI code page, and `string()` throws for characters it cannot represent.
    A user folder such as `C:\Users\Zoë\Downloads` would have stopped the game at start. New
    `cat/utf8_path.hpp` (`utf8_path`, `path_utf8`); all file names built through path operations;
    log and error texts through `path_utf8`. Identity on Linux.
  - Asset references beginning with "/" are rejected explicitly (on Windows they are not absolute).
  - Main-thread stack 8 MiB as on Linux (`/STACK`), `/utf-8`, `/bigobj`, `_CRT_SECURE_NO_WARNINGS`, `NOMINMAX`.
- Build without the Vulkan SDK: `vulkan-1.lib` is generated from the existing import allowlist (151
  Vulkan 1.0 + WSI commands; the game uses 90 of them). Checked here with llvm-lib + llvm-nm.
- Shader compiler without the SDK: `CAT_FETCH_GLSLANG` builds glslang at the commit of tag
  vulkan-sdk-1.4.309.0 (the glslang 15.2.0 all Linux builds used). Checked here from the local copy of
  that commit: 21 SPIR-V files byte-identical to the previous ones.
- Static MSVC runtime for everything (no Visual C++ Redistributable).
- CatArena.exe: GUI subsystem (no console window), `vulkan-1.dll` delay-loaded and checked at start
  with `SDL_LoadObject` (a PC without a Vulkan driver gets an explanation), CatArena.log next to the
  executable, fatal errors in a message box.

## 3. Minimum play controls

The N6 window already had mouse capture, WASD / sprint / jump / crouch / slide, fire, reload, swap,
maps 1-4 and Esc. Added for a double-clicked game: pause while the mouse is released (no game time
passes; panel with the controls), F5 restart at wave 1, F11 borderless fullscreen. Automated runs
(--frames, self-tests, offscreen) never pause. A new `--controls-selftest` drives them through SDL
events: Esc pauses (game time stops), click resumes, F11 twice, F5 restarts, Esc twice quits. Under
Xvfb it passes; the fullscreen request is not granted by Xvfb (no window manager) and is reported so.
The legacy game has no mouse-wheel weapon switch, so none was added.

## 4. Packaging

- `cmake --install` builds the game folder; `tools/package/validate_package.cmake` checks mandatory
  files (executable, SDL3 runtime, 21 shaders, 4 maps, 16 weapon models, 20 creature meshes,
  4 light volumes, README.txt, THIRD_PARTY_LICENSES.txt) and rejects development files (lib, pdb,
  obj, sources, a bundled vulkan-1.dll, layers, glslang); `tools/package/zip_package.cmake` zips it.
  The validator was checked against a deliberately broken copy (missing shader and mesh, a .pdb and a
  vulkan-1.dll): it failed with all four findings.
- The same steps made a Linux package; it was extracted into an empty folder and played from an
  unrelated working directory (see docs/n6_5-evidence/linux-package).

## 5. Continuous integration

`.github/workflows/windows-build.yml`: windows-2022, MSVC, preset `windows-release`, build, install,
validate (incl. `dumpbin /dependents`: no VCRUNTIME / MSVCP / UCRT, SDL3.dll imported, vulkan-1.dll
delay-loaded, x64), zip, upload artifact, GPU-free CTest, a start without GPU (expects a clean error),
a summary that states that rendering on Windows is unverified. Not executed (section 1).
