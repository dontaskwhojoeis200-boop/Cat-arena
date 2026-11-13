# N7 — Menus, settings, HUD and audio

Milestone request: "Implement black/white pointed Gothic Cat Arena UI, subtle grime/parallax,
title/menu/map selection + authentic previews, pause/return, HUD, volume, FOV,
graphics/resolution/AA/culling, perf stats and mobile/custom HUD controls. Mouse/keyboard fully
navigate settings without touch; Android pointer capture gets explicit tests. Port and
provenance-check audio assets, effects and music branches. Preserve responsive safe area and
gamepad/KBM/touch modes."

Reference: the unmodified legacy page `legacy/index.html` (SHA-256 e3c914bc…0393) and the v0.18
fix branch notes in `reference/patches` (Android / DeX mouse, six visible settings tabs, scrolling
settings, keyboard tab switching). Everything below was compiled and executed on Linux (GCC 13,
Mesa lavapipe, Xvfb); the Windows build is produced by GitHub Actions (BUILD.md); Android is N9.

## 1. Origin map

| Legacy (index.html) | Native |
|---|---|
| 11604-11800 audio graph: master / weapon / enemy / music buses, DynamicsCompressor, AudioBufferSource + StereoPanner + biquads, oscillator beeps, `audiostart()` on a gesture | `cat::audio::Mixer` (src/audio_mixer.cpp), `cat::audio::Device` (SDL3, src/audio_device.cpp) |
| 824 `VOID_V318_SAMPLE_BANK` (48 base64 Ogg Vorbis takes), 807-814 ASHEN DRIVE MP3 | `assets/n7/audio` (tools/n7/extract_n7_assets.py), stb_vorbis / dr_mp3 decoders (src/audio_codec.cpp) |
| playGunSound, playExplosionSound, playBodySound (+ synthNoiseBuffer "step"), playEnemyVoice limits / cooldowns / family tones, beep(), ensureThemeMusic / syncThemePlayback | `cat::audio::GameAudio` (src/audio_game.cpp) fed by simulation events |
| 16529-16698 "Original Cat Arena presentation and explicit menu routing": home, maps page with the live map view, multiplayer, settings, exit dialog; catBack | `cat::ui::Menu` (src/menu.cpp) |
| entry.js loading screen (7 stages, ENTER THE ARENA) | `Menu::loading`, staged loading in src/main_menus.inc |
| mobile-hud.js: pause overlay, HUD editor (HUD_ITEMS, hudRecords), touch controls, control-mode detection | `Menu::pause`, `Menu::hud_editor`, `cat::ui::build_hud` (src/ui_hud.cpp), `cat::TouchControls` / `InputModeTracker` / `PointerCapture` |
| 627 HUD DOM + CSS restyle (525-625, 16700+), setCrosshair (14869), #perfOverlay, recordFrameInterval (14684-14740) | src/ui_hud.cpp, `cat::PerfMeter` (include/cat/app_controls.hpp) |
| graphics-menu.js `Q3R_PRESETS`, the 65 settings controls, savedControls / localStorage | `cat::GameSettings` (src/game_settings.cpp), saved as `id=value` lines under SDL_GetPrefPath |
| 14996-15001, 16371-16393 touch joystick / look / FIRE / JUMP / SWAP / SPRINT / SLIDE / RELOAD, sprintRequested() | `cat::TouchControls`, `cat::SprintLatch`; `TickInput::stick_x / stick_y` (touchStick in update()) |

## 2. Audio

**Assets.** `tools/n7/extract_n7_assets.py` decodes the 48 sample takes and the soundtrack from the
legacy page byte for byte (file hashes and the source line of every file in
`assets/n7/N7_ASSET_MANIFEST.json`). Provenance as the legacy states it: "v3.18: offline-rendered
original sampled audio assets. No external URLs." and "An actual produced alt-metal soundtrack
replaces the old WebAudio sequencer" (ASHEN DRIVE, "original instrumental theme"). These are the
owner's statements in the legacy source; this milestone did not verify rights independently.
**Not imported:** the unmerged v0.18 "SOUND BREAK" branch (`reference/alternate_branches/
CAT_ARENA_FERAL_CLAW_v0_18_SOUND_BREAK_PLAYABLE.html`) is catalogued only: it is a separate branch
with its own soundtrack, never merged into the game the native port follows.

**Parity with the browser.** The test `audio_webaudio_parity` compares the native mixer with
Chromium 153's Web Audio, recorded by `tools/n7/record_webaudio.cjs` (fixtures in
`native/tests/fixtures/n7`):

| Check | Result |
|---|---|
| Decoding of the 49 files (48 Ogg + MP3) | worst RMS difference 2.4e-7; MP3 4,364,800 frames in both. Chromium's FFmpeg drops 11-192 end-padding frames of the Ogg takes: the measured browser lengths are stored in `bank.txt` (third column) and the native bank plays exactly that many frames |
| playbackRate source | relative RMS 2.6e-4 |
| filters + panner, stereo panner | 2.9e-4, 6.1e-8 |
| sine / triangle / sawtooth beeps | 9.4e-4, 5.8e-3, 4.9e-2 (band-limited oscillators; the browser uses wavetables) |
| DynamicsCompressor (threshold -10, knee 12, ratio 3.1, attack .008, release .16) | 3.8e-4 (Chromium's compressor kernel ported) |
| Legacy sound rules | gun takes and levels, explosion level, body sounds, voice range 31 m / 3 voices (5 with deaths) / per-enemy cooldowns / 0.095 s spacing / attenuation and pan, beeps, mix settings, music sync: PASS |

The sound code has its own random stream: unlike the legacy (whose sound calls draw from
`Math.random()` and so shift the gameplay sequence once audio has started) sound never touches the
simulation. The simulation only reports events (shots, blasts, body sounds, enemy voices, player
hits); the existing gameplay draws (hurt voice `< .52`, idle voice cooldown) stay where they were.
All legacy replays are bit-identical to N6.5 (TEST_MATRIX.md).

**Output.** SDL3 audio stream, stereo float at 48 kHz, pulled by the device thread. Without a
device the game runs silent and logs it (seen on the build machine: no sound card; with
`SDL_AUDIO_DRIVER=dummy` the device path mixed 1,176,576 frames in the menu self-test). The
soundtrack plays only in a game (returnMenu pauses it), pauses while the window is minimized /
hidden (document.hidden), and starts with the first deliberate action (PLAY, ENTER, TEST AUDIO).

## 3. Menus and HUD

Drawn by one UI pipeline (ui.vert / ui.frag) after the world and the N5 overlay, or alone
(`Renderer::draw_ui`) on pages that cover the screen. Text: signed-distance-field glyphs built at
start from the shipped fonts (544 glyphs, 4 faces, 0 missing code points). The legacy names
Noto Serif / Georgia / Times New Roman, Arial and monospace and lets the browser pick; the native
game ships the metric-compatible Liberation fonts (Serif Regular for weight 400, Serif Bold for 900,
Sans Bold, Mono Bold) and DejaVu Sans Bold for the symbols they lack (`assets/n7/fonts/
FONTS_MANIFEST.json`). Small text: four sub-pixel samples and a 0.12 px dilation keep hairlines
(crossbars of "A", arrow shafts) as a browser rasterizes them.

Pages (native captures in `docs/n7-evidence/native-ui`, legacy captures in
`docs/n7-evidence/legacy-ui`, both 1280 x 720):
- **Home**: --feral-bg backdrop with pointer parallax, sigil, CAT ARENA title, PLAY 01 /
  MULTIPLAYER 02 / SETTINGS 03 / EXIT 04 in the legacy positions (buttons from y 356, cut corners),
  right-hand marks. Esc on the home page opens the exit dialog (catBack).
- **Maps**: four map cards (title, tag line, description from the legacy card table) and the live
  view of the chosen map through the preview frame, from the legacy preview cameras (Helios
  78,42 yaw 2.45; Bastion 78,38 yaw 2.36; Reactor 78.5,60.5 yaw 3.14; Arsenal 42,46 yaw .73).
  Choosing a card loads that map's render scene only.
- **Loading**: the seven legacy stages, each run in its own frame (scene, GPU upload, collision,
  navigation, models / light volume, session, warm-up), percentage, errors shown on the page, then
  ENTER THE ARENA (the user gesture that captures the mouse, and switches to fullscreen when
  "Fullscreen when launching" is on).
- **Pause** (Esc, the Ⅱ PAUSE button in touch mode, focus loss): resume, settings (opens on the
  legacy's tab), customize HUD, choose another map, return to main menu. The simulation stops while
  any menu is open.
- **Settings**: six tabs, all visible (v0.18 fix: 6 / 3 / 2 columns by width), scrolling list,
  the 65 legacy controls with their legacy defaults, ranges, options and labels
  (`native/tests/fixtures/n7/legacy_settings.json`, recorded from the running page).
- **HUD editor**: every HUD_ITEMS element outlined, drag to move, size and opacity, reset, saved
  as hudRecords.
- **HUD**: brand, score, wave box, final-five banner, health / speed, weapon box, stance (with the
  uphill / downhill tag), mouse status, MAP ↻ (next map) and ⚙ SETTINGS · V buttons, notices in
  the legacy event box, burning, crosshair (the 8 legacy shapes and 5 colours), hit marker, damage
  flash, control tips, performance overlay; touch layout with joystick, FIRE, JUMP, SWAP, SPRINT,
  SLIDE, RELOAD, MAP, SETTINGS and PAUSE. Positions are the legacy element boxes measured at
  1280 x 720, anchored to the safe area (notches, gesture bars: SDL_GetWindowSafeArea).

## 4. Settings: what each one does natively

`applied` = the native game does what the legacy does; `partial` = applied with a stated
difference; `stored` = shown, saved and restored, no effect yet (N10). The settings page shows the
note of every partial / stored control.

- Applied (32 controls, plus their action buttons): FOV, crosshair style and colour, viewmodel direction / size / height and reset,
  motion strength (bob, roll, landing dip, step smoothing, weapon sway), FPS / CPU / GPU / VRAM
  overlay rows, light grid on models, dynamic lights, shadows, VSync (FIFO vs mailbox / immediate),
  control mode, look sensitivity, touch button size / opacity, invert look, sprint behaviour
  (hold / toggle / auto), the four volumes, music on / off, TEST AUDIO, HUD size, score, hit
  feedback, control tips, stance, HUD editor, reset HUD, fullscreen when launching, fullscreen now,
  restore normal graphics.
- Partial (8): graphics preset (writes the legacy preset values; only the applied ones change the
  picture), lighting (lightmap only), high-quality sky (off hides the clouds), PVS toggle (switches
  the renderer's cluster culling), graphics debug view (F9 cycles), fog quality (off / on), frame
  rate limit (at least 10 FPS so menus stay usable), RAM rows (process memory instead of the
  JavaScript heap).
- Stored (25, N10): overbright, flares, gamma, texture quality / bits / filter / anisotropy, effects
  detail, marks, brass, blood, gibs, rail trail, lightmap filter and intensity, curve detail,
  detail textures, mirrors, portal resolution, world visibility mode, **scene resolution,
  multisample AA, FXAA, adaptive resolution**, adaptive FPS target. The native renderer draws at
  the window's resolution without anti-aliasing; render scaling and MSAA / FXAA need an offscreen
  scene target and are scheduled with the renderer work of N10.

## 5. Input

- Keyboard / mouse: every page and every settings control is reachable and adjustable without a
  pointer (arrows, Enter / Space, Esc / Backspace, Tab / Shift+Tab, Ctrl+Tab and PageUp / PageDown
  for tabs, Home / End; Shift+arrow = 5 steps). Mouse: hover, click, slider drag, wheel.
- Touch: menus by the first finger (tap, drag sliders, drag-scroll the settings list); in a game the
  legacy handlers (joystick within 0.75 of the pad width, 47 % deflection = full, look 1.5 x
  sensitivity per CSS pixel, FIRE / JUMP held, SWAP tap, SPRINT hold or toggle, SLIDE = crouch held
  + slide, RELOAD tap). SDL's finger-to-mouse emulation is off in the menu shell.
- Control mode AUTO follows the last input; a mouse or keyboard on a touch device switches to
  keyboard & mouse (v0.18 Android / DeX rule).
- Pointer capture: requested only by a deliberate action (ENTER THE ARENA, resume, a click in the
  game), released when any menu opens, on focus loss and in touch mode, re-requested on resume
  (`cat::PointerCapture`, SDL relative mouse mode = Android pointer capture). The rules have unit
  tests; **on an Android device they are NOT_RUN** (N9 builds the APK).
- Gamepad (native addition; the legacy has none): D-pad / A / B / bumpers in menus; sticks, triggers
  and buttons in the game (README_WINDOWS.md).

## 6. Performance overlay

The legacy formulas (recordFrameInterval / updateFpsDistribution): intervals of 0-500 ms in a 60 s
history (at most 16,384), a longer stall clears it; FPS AVG = frames / time; 1 % LOW = mean of the
slowest ceil(1 %) frames from 100 frames on; 0.1 % LOW from 1,000 frames on; HITCHES = intervals of
25 ms or more; FPS LIVE and CPU over windows of at least 800 ms. CPU = time from the start of the
frame to the draw call (events, simulation, building the frame); GPU = Vulkan timestamps of the
previous frame ("N/A (unsupported)" without timestamp support); PROCESS RAM / PEAK RAM = resident
set (Linux, Android) or working set (Windows); GPU ALLOC~ = exact sum of the renderer's Vulkan
allocations.

## 7. Tests

- `audio_webaudio_parity`, `ui_menus_settings_hud` (936 checks: settings against the legacy
  recording, presets, persistence, font atlas, keyboard-only navigation of all 47 enabled controls,
  mouse / touch navigation, HUD on five screen sizes including a notched phone, capture rules, FPS
  statistics, sprint modes, touch joystick / look / buttons).
- `window_menu_selftest` (desktop, GPU label): synthetic SDL key events drive home → maps (live
  preview) → loading → ENTER THE ARENA → fire (gun sound counted) → Esc (time stops, capture
  released) → settings: Ctrl+Shift+Tab to VIEW, FOV +5 → PageDown to AUDIO, master volume -10 → Esc
  → return to main menu → Esc → exit. Checks the FOV reached the renderer and both values were
  written to the settings file.
- `cat_arena --offscreen WxH --ui-capture <dir> [--touch]` writes every page, every settings tab
  and the HUD pages (used for docs/n7-evidence/native-ui).
