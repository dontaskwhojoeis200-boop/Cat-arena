# Cat Arena: Feral Claw — native rewrite, N6.5 (Windows build and packaging)

Complete source tree after N6.5. Start with `N6_5_BUILD_REPORT.md`; players read `README_WINDOWS.md`.

- Windows x64: CMake preset `windows-release` (Visual Studio 2022) and the GitHub Actions workflow
  `.github/workflows/windows-build.yml`, which builds `CatArena.exe` and uploads the game folder
  `CAT_ARENA_FERAL_CLAW_N6_WINDOWS` as a ZIP. No Vulkan SDK and no Visual C++ Redistributable are needed.
- **Status: the Windows executable has not been built yet.** The build machine for this milestone has
  no Windows toolchain and no GitHub access; run the workflow (or the preset on a Windows PC) to
  produce it. Everything else was checked on Linux with the same CMake code (docs/TEST_MATRIX.md).
- New in the game: pause while the mouse is released (with a controls panel), F5 restart, F11
  fullscreen, CatArena.log, an error box instead of a silent exit.
