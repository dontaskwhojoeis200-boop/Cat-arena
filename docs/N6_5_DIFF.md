# N6.5 change list (relative to the N6 package)

New
- `.github/workflows/windows-build.yml` — Windows x64 build, package, validate, zip, upload, GPU-free tests.
- `N6_5_BUILD_REPORT.md`, `README_WINDOWS.md` (also the package's README.txt), `README_N6_5.md`.
- `native/cmake/cat_vulkan_import_library.cmake` — vulkan-1.lib from the import allowlist.
- `native/tools/package/third_party_licenses.cmake`, `validate_package.cmake`, `zip_package.cmake`.
- `native/include/cat/utf8_path.hpp` — UTF-8 text <-> std::filesystem::path.
- `native/tests/vulkan_import_library_check.cmake`.
- `native/include/cat/js_math.hpp`, `include/cat/generated/js_math_tables.inc`,
  `tools/js_math/gen_tables.py`: correctly rounded sin / cos / tan / atan2 / pow (found by the Windows CI).
- `native/tests/js_math_tests.cpp`, `tests/fixtures/js_math/chromium_math.bin`,
  `tools/js_math/record_chromium_math.cjs`: test `js_math_browser` against 31,127 Chromium results.
- `native/third_party_notices/fdlibm/NOTICE.txt` (in THIRD_PARTY_LICENSES.txt).
- `docs/DEVELOPMENT_LOG_N6_5.md`, `docs/N6_5_DIFF.md`, `docs/n6_5-evidence/`, `docs/history/N6/`.

Changed
- `native/CMakeLists.txt`: static MSVC runtime, MSVC flags and stack, generated vulkan-1.lib on
  Windows, `CAT_FETCH_GLSLANG`, `CAT_WINDOWS_GUI`, `CAT_MILESTONE`, `CAT_GAME_DIR`; CatArena.exe
  (GUI subsystem, delay-loaded vulkan-1.dll); install layout (game folder with SDL3 runtime, shaders,
  assets, README.txt, THIRD_PARTY_LICENSES.txt, $ORIGIN rpath on Linux); tests
  `windows_vulkan_import_library`, `window_input_selftest`, `window_controls_selftest`; label `gpu` on
  every test that needs a Vulkan device or a window.
- `native/CMakePresets.json`: `windows-release` configure / build / test presets, `windows-release-gpu`.
- `native/src/main.cpp`: UTF-8 paths; `--log-file`, CatArena.log on Windows; Vulkan loader check;
  error message box; pause while the mouse is released, F5 restart, F11 fullscreen, pause panel;
  `--controls-selftest`.
- `native/src/core.cpp`, `include/cat/core.hpp`: `set_log_file`.
- `native/src/hud.cpp`, `include/cat/hud.hpp`: pause panel.
- `native/src/assets.cpp`, `image_codec.cpp`, `scene.cpp`, `renderer.cpp`: UTF-8 path texts; rooted
  asset references rejected.
- `native/tests/core_tests.cpp`: UTF-8 path round trip.
- `native/src/main.cpp` (after the first Windows run): `SDL_main.h` included after the code that uses
  the `main` members of `fx::Frame` / `cat::Overlay` (MSVC C2039).
- `native/src/player.cpp`, `combat.cpp`, `enemies.cpp`, `weapon_fx.cpp`, `enemy_fx.cpp` (after the
  second Windows run): `std::sin / cos / tan / atan2 / pow` replaced by `cat::jsmath::` (same values as
  Chromium; native replay hashes changed from N6 because more ticks now equal the legacy).
- Trackers: `docs/PROGRESS.md`, `TEST_MATRIX.md`, `KNOWN_LIMITATIONS.md`, `BUILD.md`.

Unchanged: gameplay logic, rendering code paths; `legacy/`, `reference/`, assets, existing fixtures.
