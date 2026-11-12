# Development log — N6.5 (Windows x64 build, packaging, CI)

2026-11-11. Runner: Linux x64 (as N6). Goal: a reproducible Windows x64 package of the N6 game.

## 1. What this runner can and cannot do

- No Windows toolchain is available here: no MSVC, no Windows SDK, no MinGW-w64; package installs
  are blocked. clang, lld-link and llvm-lib exist, but without Windows headers and C runtime libraries
  nothing can be compiled for Windows. Wine is not installed.
- At first no GitHub account was linked, so everything that does not need Windows was done and
  checked on Linux with the same CMake code, and the Windows parts were written for windows-2022
  GitHub runners. After GitHub was connected, the source went to
  https://github.com/dontaskwhojoeis200-boop/Cat-arena and the workflow built Windows there
  (sections 6-8). No Windows PC with a GPU was available at any point.

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
a summary that states that rendering on Windows is unverified. Runs: sections 6-8.

## 6. First Windows run: one MSVC error

Repository created with the N6.5 source (legacy/ and reference/ included, unmodified), pushed;
run 43164340554. Configure (VS 2022, MSVC 19.44, SDK 10.0.26100, glslang and SDL3 fetched) passed;
the build failed in main.cpp with C2039 "'SDL_main' is not a member of 'cat::fx::Frame'" (and of
`cat::Overlay`). `SDL_main.h` does `#define main SDL_main` on Windows, which also renames struct
members called `main`. Fix: include it after the code that uses those members (it only has to
precede `int main`). Every other target, including the renderer and all tests, had compiled and
linked; the only warnings (2 x C5286) are in SDL's sources.

## 7. Second run: a determinism failure, and what the browser computes

Run 43164827201: build, install, package check, dumpbin checks, ZIP and artifact upload passed;
start without GPU passed; CTest 32/33. `physics_legacy_trajectories` (Helios) failed: scenario
random_3_z-4 max error 1.30e-9 at tick 2111 against a tolerance of 1e-9; bit-exact on 15,413 of
21,820 ticks (Linux: 21,725). All enemy, combat, effect, animation and navigation replays passed.

The tolerance was not raised. Investigation:

- MSVC's CRT `sin` / `cos` / `atan2` / `pow` differ in the last bit from glibc's on some arguments;
  a one-bit difference in a heading or a bob phase grows over thousands of 120 Hz ticks.
- A first fix ported fdlibm (V8's `src/base/ieee754.cc`). Checked against Node 22 (V8 12.4) on
  2.4 million arguments it matched bit for bit (after taking V8's version of pow, whose last step
  differs from fdlibm's: `r = z*t1 / ((t1-two) - (w+z*w))`). But against Playwright's Chromium 153,
  which recorded the legacy fixtures, fdlibm differed on 2-8 % of the arguments. V8 builds with
  clang use other implementations (`v8_use_libm_trig_functions = is_clang`, and newer ones).
- Measured Chromium 153 on 200,000 arguments per function: glibc differs from Chromium on 126 sin,
  122 cos, 244 tan, 113 atan2 and 0 pow results. On every one of those 605 sin / cos / tan / atan2
  results Chromium has the **correctly rounded** value (checked with 60-digit arithmetic). Its pow is
  glibc's (correctly rounded except about 1 in 30,000 arguments).
- So the platform-independent target is the correctly rounded result. `include/cat/js_math.hpp`
  computes sin, cos, tan, atan2 and pow in double-double arithmetic: Cody-Waite reduction with six
  33-bit pieces of pi/2 (fdlibm's Payne-Hanek only above 2^19 pi/2), tables of sin / cos / atan of
  j/64, ln(1 + j/64) and 2^(j/64), short Taylor / atanh series, one final rounding. Only IEEE double
  +, *, / are used (Dekker products, no FMA), so MSVC, GCC and clang compute identical bits.
  Constants and tables come from `tools/js_math/gen_tables.py` (Python decimal, 400 digits).
- Results against Chromium: sin, cos, tan, atan2 bit-exact on all 200,000 arguments each; pow
  differs on 7 of 200,000, all of them cases where Chromium (glibc) is 1 ulp off and js_math is
  correctly rounded. The new CTest `js_math_browser` keeps 31,127 Chromium results (special values,
  angles from 1e-6 to 1e22, near multiples of pi/2, the game's pow arguments) and also reports how
  often the platform's C runtime differs from the browser.
- Replays on Linux after the change: trajectories (4 maps), weapon effects, combat and enemy
  animation (4 maps) bit-exact on every tick; enemies bit-exact on Bastion, Reactor, Q3 and on 18,410
  of 19,200 Helios ticks (was 18,273; worst 1.8e-15). The native replay hashes therefore changed
  from N6 (they now agree with the legacy on more ticks).
- Cost: about 0.2 µs per call; +0.02 ms per tick in the weapon-effect replay.

## 8. Third and later runs

Run 43169039455 (commit cd65a5a): every step passed, CTest 34/34 including `js_math_browser` on
MSVC, artifact `CAT_ARENA_FERAL_CLAW_N6_WINDOWS` uploaded (ZIP 29,987,525 bytes). The next commit
added the fdlibm notice to THIRD_PARTY_LICENSES.txt, a replay summary in the job log (bit-exact
counts and hashes on Windows) and these documents; its run is the final N6.5 build
(N6_5_BUILD_REPORT.md section 6). The Linux matrix (7 configurations) was run again on the final
source (docs/n6_5-evidence).
