# N7 change list (relative to N6.5, commit 2300dab)

New
- `native/src/menu.cpp`, `include/cat/menu.hpp` — home, maps, multiplayer placeholder, settings,
  exit dialog, loading, pause, HUD editor; keyboard / pointer / touch / wheel handling.
- `native/src/game_settings.cpp`, `include/cat/game_settings.hpp` — the 65 legacy controls,
  presets, validation, `id=value` persistence, HUD placements.
- `native/src/ui_hud.cpp`, `include/cat/ui_hud.hpp` — legacy HUD layout (keyboard / touch), crosshairs,
  event box, performance overlay, editor outlines.
- `native/src/ui_font.cpp`, `ui_canvas.cpp`, `include/cat/ui.hpp` — SDF font atlas and UI drawing.
- `native/shaders/ui.vert`, `ui.frag` — UI pipeline (shader count 23).
- `native/src/audio_codec.cpp`, `audio_mixer.cpp`, `audio_game.cpp`, `include/cat/audio.hpp` — decoding,
  Web Audio–equivalent mixer, legacy sound rules.
- `native/src/audio_device.cpp`, `include/cat/audio_device.hpp` — SDL3 output.
- `native/src/platform_info.cpp`, `include/cat/platform_info.hpp` — process memory.
- `native/include/cat/app_controls.hpp` — PerfMeter, SprintLatch, TouchControls;
  `include/cat/input_modes.hpp` — InputModeTracker, PointerCapture.
- `native/src/main_menus.inc` — the interactive menu shell (included by main.cpp).
- `native/tests/audio_tests.cpp`, `ui_tests.cpp`, `mini_json.hpp`; fixtures `native/tests/fixtures/n7`.
- `native/third_party/stb/stb_vorbis.c`, `stb_truetype.h`, `third_party/dr_libs/dr_mp3.h` (+ licences,
  provenance); `third_party_notices/dr_libs`, `third_party_notices/fonts`.
- `assets/n7` — 48 sample takes, soundtrack, backdrop, fonts, manifests.
- Tools: `native/tools/n7/*`, `tools/n7/*` (section BUILD.md N7).
- Docs: `docs/N7_UI_AUDIO.md`, `DEVELOPMENT_LOG_N7.md`, `N7_DIFF.md`, `README_N7.md`,
  `docs/n7-evidence/`, `docs/history/N6_5/`.

Changed
- `native/src/main.cpp`: N7 options (`--menu-selftest`, `--game-settings`, `--ui-capture`, `--touch`),
  interactive runs open the menus; map loading split into stages; offscreen UI capture.
- `native/src/renderer.cpp`, `include/cat/renderer.hpp`: UI pipeline (destination alpha kept), `draw_ui`,
  GPU timestamps, tracked Vulkan allocations, `set_present_mode`, `Overlay::world`.
- `native/include/cat/physics.hpp`, `src/player.cpp`: body sound events; `TickInput::stick_x / stick_y`
  (legacy touchStick term, zero for keyboard play); `clear_body_events`, `set_stick`.
- `native/include/cat/combat.hpp`, `src/combat.cpp`, `src/enemies.cpp`: voice and player-hit events
  (no new random draws); stick and body events passed through `Arena::step`.
- `native/include/cat/weapon_fx.hpp`, `src/weapon_fx.cpp`, `tests/fx_tests.cpp`: viewmodel calibration.
- `native/CMakeLists.txt`: new libraries / tests, `window_menu_selftest`, assets/n7 installed,
  `CAT_MILESTONE` N7.
- `native/tools/package/validate_package.cmake`, `third_party_licenses.cmake`: N7 files and notices.
- `.github/workflows/windows-build.yml`: default milestone N7. `README_WINDOWS.md`: menus, controls.
- Trackers: `docs/PROGRESS.md`, `FEATURE_PARITY.md`, `TEST_MATRIX.md`, `KNOWN_LIMITATIONS.md`,
  `BUILD.md`, `ORIGIN_MAP.md`.

Unchanged: `legacy/`, `reference/`, earlier assets and fixtures; every legacy replay result.
