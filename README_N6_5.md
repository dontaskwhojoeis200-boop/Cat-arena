# Cat Arena: Feral Claw — native rewrite, N6.5 (Windows build and packaging)

Complete source tree after N6.5. Start with `N6_5_BUILD_REPORT.md`; players read `README_WINDOWS.md`.

- Windows x64: CMake preset `windows-release` (Visual Studio 2022) and the GitHub Actions workflow
  `.github/workflows/windows-build.yml`, which builds `CatArena.exe` and uploads the game folder
  `CAT_ARENA_FERAL_CLAW_N6_WINDOWS` as a ZIP. No Vulkan SDK and no Visual C++ Redistributable are needed.
- **Status: built by GitHub Actions** (https://github.com/dontaskwhojoeis200-boop/Cat-arena, workflow
  windows-build): MSVC build, package checks and all 34 GPU-free tests pass on Windows; the game
  folder is the run's artifact `CAT_ARENA_FERAL_CLAW_N6_WINDOWS`. Not yet run on Windows with a GPU.
- Cross-platform determinism: the simulation uses correctly rounded sin / cos / tan / atan2 / pow
  (`include/cat/js_math.hpp`), like Chromium, so Windows and Linux replay the legacy recordings
  with the same bits.
- New in the game: pause while the mouse is released (with a controls panel), F5 restart, F11
  fullscreen, CatArena.log, an error box instead of a silent exit.
