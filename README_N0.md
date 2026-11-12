# Cat Arena: Feral Claw — N0 native foundation

N0 implementation, 2026-11-09. Start here for the new code; `README_START_HERE.md`
and the three CLAUDE specification files remain unchanged as source authority.

**This is an engine foundation and triangle shell, not a playable native Cat Arena.**
Linux C++20 core and SDL3/Vulkan desktop shell compiled and linked; shaders compiled.
Headless tests passed. Visible rendering is NOT VERIFIED: graphical startup reported
`No available video device`. No Windows EXE or Android APK was built in N0.

From `native/`:

```sh
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug
```

For the desktop build, install the Vulkan SDK and SDL3 development prerequisites,
then use `desktop-debug`. See [docs/BUILD.md](docs/BUILD.md) for commands, exact tested
versions, dependency setup and provisional Android core cross-compilation.

- `native/include/cat`, `native/src`: independent core and SDL/Vulkan shell.
- `native/tests`: deterministic timing/settings/math/RNG/input/ownership/path tests.
- `native/shaders`: actual vertex/fragment shaders compiled to SPIR-V by CMake.
- `native/tools/inventory_sources.py`: read-only source/ZIP-member hashing.
- `docs/PROGRESS.md`, `TEST_MATRIX.md`, `KNOWN_LIMITATIONS.md`: current status.
- `docs/n0-evidence`: actual command results, including unsuccessful checks.
- `docs/ORIGIN_MAP.md`: source locations and constraints for N1.
- `legacy/`, `reference/`: unchanged game, assets, relay and historical source.
- `reference/history/CAT_ARENA_MIGRATION_STEP_02_REFERENCE.zip`: entire attached
  Step 02 package, retained byte-for-byte so no historical files disappear.
- `docs/history/pre-N0`: original handoff documents before tracker updates.

No web game or reference binary has been modified or replaced. Root
`FILE_MANIFEST.json` remains the original handoff manifest; the current package
manifest is `N0_FILE_MANIFEST.json`. Next milestone: **N1 — extract and bake real
map/asset data**, as specified in `CLAUDE_PHASE_PROMPTS.md`.
