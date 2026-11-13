# N7 evidence

- `legacy-ui/` — the unmodified legacy page in headless Chromium 153 (lavapipe), 1280 x 720:
  every menu page, the six settings tabs (as the legacy draws them, before the v0.18 tab fix), the
  keyboard and touch HUD, pause; `legacy_ui_layout.json` holds the rectangle and computed style of
  every UI element (`tools/n7/legacy_ui_capture.cjs`).
- `native-ui/` — the native game, offscreen Vulkan (lavapipe), `cat_arena --offscreen WxH --ui-capture`:
  - `1280x720-helios-crucible/` (PNG): every page, settings tabs, loading at stage 3 and ready,
    HUD (keyboard / mouse, touch), pause, settings from pause, HUD editor;
  - `1280x720-reactor-bastion/`, `2400x1080-touch/` (phone size, touch layout), `800x480/` (JPEG);
  - `*.log`: the runs' logs (validation layers on; no warning or error lines, exit code 0).
- `legacy_left_native_right.jpg` — the 1280 x 720 pages side by side (legacy left, native right).
  The legacy HUD captures show Reactor Bastion's welcome notice; the native ones Helios' wave notice.
- `linux-matrix/` — the seven Linux configurations (configure / build / test logs, replay hashes).
- `windows-ci/` — the GitHub Actions run of the N7 Windows build (job log, test list).
