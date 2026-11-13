# N7 development log — menus, settings, HUD, audio (2026-11-12)

Chronological; every result below was produced by a command that ran in this session.

1. **Legacy reference recorded.** `tools/n7/legacy_ui_capture.cjs` drove the unmodified legacy page
   in headless Chromium 153 (Playwright, lavapipe): screenshots of home, maps, multiplayer, the six
   settings tabs, exit dialog, loading, keyboard and touch HUD, pause, plus the rectangle and computed
   style of every UI element (`docs/n7-evidence/legacy-ui`). `tools/n7/legacy_settings_defaults.cjs`
   recorded the 65 settings controls (type, default, range, step, options with labels).
   The control-mode select was not settable by clicking in the headless page; the script sets the
   value and dispatches input / change events instead.
2. **Assets.** `tools/n7/extract_n7_assets.py` extracts the 48 sample takes (Ogg Vorbis), the
   soundtrack (MP3, 32 kHz stereo, 136.44 s) and the menu backdrop (WebP, converted to PNG) with
   hashes and line numbers. A first version carried a mistyped legacy SHA-256; replaced by the
   recorded hash, the extractor now refuses a different legacy file.
3. **Decoders.** stb_vorbis 1.22 and dr_mp3 0.7.4 (pinned commit), compiled in a target without the
   strict warning flags. Chromium's decode of the Ogg takes is 11-192 frames shorter (its FFmpeg drops
   end padding): `record_webaudio.cjs` measured the browser lengths; `set_browser_lengths.py` wrote
   them into `bank.txt`; the native bank plays exactly those frames. Decoded samples agree to 2.4e-7.
4. **Mixer vs Web Audio.** Seven graphs recorded from Chromium at 48 kHz (rate, filters + pan, stereo
   pan, three oscillator beeps, compressor). First native results: rate / filter graphs failed at the
   end of the source (Chromium lets biquads ring out and interpolates towards zero past the last
   sample; fixed), and the compressor was 32 % off with a textbook design: Chromium's
   DynamicsCompressorKernel was ported (pre-delay, knee curve, adaptive release) → 3.8e-4.
5. **Game rules.** `GameAudio` ports playGunSound / playExplosionSound / playBodySound /
   playEnemyVoice / beep / theme sync. The simulation got events for body sounds (jump, pad launch,
   landing > 1.9 m/s, footsteps, slide) and enemy voices (idle, attack, hurt, death) plus player hits.
   Replays before / after compared line by line: identical (the voice draws reuse the existing draws).
   A later check found the body events were cleared only by `PlayerController::step`, which
   `combat::Arena` does not call: fixed (cleared at the start of `Arena::step`).
6. **Fonts and UI drawing.** SDF atlas from Liberation Bold faces; 33 symbols were missing → DejaVu
   Sans Bold fallback; then Liberation Serif Regular added for the legacy's weight-400 menu text.
   Comparing captures showed thin strokes vanishing at 13-18 px ("PLΛY"): the UI shader now takes
   four sub-pixel samples and dilates 0.12 px. Captures were also semi-transparent where UI blended
   (alpha blended like colour): the UI pipeline keeps destination alpha at 1.
7. **Menus and settings.** `cat::ui::Menu` and `cat::GameSettings`. The keyboard test hung: Down on a
   settings tab went to the next tab and never reached the list; tabs are now one stop (Down enters
   the list, Up from the first control returns to the tab). Touch drags that start on a control now
   scroll the list after 8 px.
8. **HUD.** `cat::ui::build_hud` from the measured legacy boxes. In touch mode the legacy hides
   #stanceLabel (it overlapped the stick in the first native layout). Notices moved into the legacy
   event box (right 2 %, top 27 %, double brass border).
9. **Shell.** `src/main_menus.inc`: menus, staged loading, pause, settings application, persistence,
   SDL audio device, pointer capture, touch / gamepad input, safe area, performance overlay. The
   simulation input got the legacy `touchStick` term (`TickInput::stick_x / stick_y`, zero for
   keyboard play: replays unchanged). Offscreen `--ui-capture` and the `--menu-selftest` window test
   (passed on its first run; also passed with SDL's dummy audio driver: 1,176,576 frames mixed).
10. **Comparison with the legacy captures** (home side by side): button rows moved down 24 px, cut
    corners instead of chevrons, title size / spacing, map tag lines in capitals, the full card
    descriptions — then re-captured (`docs/n7-evidence/native-ui`).
11. **Matrix.** First run: clang (both configurations) failed on an unused constant in menu.cpp
    (`-Wunused-const-variable`, GCC does not warn); removed; the matrix was run again on the final tree.
