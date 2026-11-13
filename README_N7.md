# Cat Arena: Feral Claw — native rewrite, N7 (menus, settings, HUD, audio)

Complete source tree after N7. Read `docs/N7_UI_AUDIO.md` for what was built and how it was checked;
players read `README_WINDOWS.md`.

- **Menus** in the legacy routing and look: main menu, map selection with the live view of each
  map, seven-stage loading screen with ENTER THE ARENA, pause menu, exit dialog, settings in six
  tabs (all visible, scrolling, keyboard tab switching — the v0.18 fixes), HUD editor.
  MULTIPLAYER is a placeholder page (N8).
- **Settings**: the 65 legacy controls with the legacy defaults, ranges and options; 32 act as in
  the legacy, 8 with a stated difference, 25 are saved for N10 (among them scene resolution,
  anti-aliasing and the culling modes — the renderer does not offer those yet).
- **HUD** in the legacy layout (keyboard / mouse and touch), safe-area aware, scalable, editable;
  performance overlay with the legacy FPS statistics, CPU, GPU, RAM and GPU memory.
- **Input**: keyboard-only navigation of everything, mouse, touch controls, gamepad; control mode
  AUTO and the v0.18 Android / DeX pointer-capture rules (unit-tested; no Android device yet).
- **Audio**: the legacy's 48 sound takes and the ASHEN DRIVE soundtrack, mixed by a port of the
  legacy Web Audio graph that matches Chromium on recorded graphs; sounds follow the legacy rules.
  Gameplay replays are unchanged.

Status:
- Linux: built and tested in seven configurations (36 core / 67 desktop tests each, all PASS; legacy
  replays identical in every build).
- Windows: built, packaged and tested (36 GPU-free tests PASS) by GitHub Actions run
  https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43336140592 on branch `n7`;
  game folder = artifact `CAT_ARENA_FERAL_CLAW_N7_WINDOWS`
  (https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43336140592/artifacts/13997451666).
  Replays identical to Linux.
- Not yet played on Windows with a GPU, not heard on a real sound card, not run on Android.

Run: `cat_arena` (menus), `cat_arena --map reactor-bastion` (loading screen of that map),
`cat_arena --menu-selftest`, `cat_arena --offscreen 1280x720 --ui-capture out/` (docs/BUILD.md, N7).
