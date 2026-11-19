# N9 development log — Windows platform completion

N9 was done in two parts by two sessions. Every result below was produced by a command that ran;
the first part's results are taken from its own evidence (docs/n9-evidence) and Windows CI run.

## Part 1 — branch `n9-windows` (2026-11-13, another session, commits 869bff3 … 525adc5)

Branched from the first N8 commit (a0fd083).
- Display: `DesktopDisplay` — DISPLAY RESOLUTION (window size or exclusive fullscreen mode) and
  FULLSCREEN REFRESH RATE in Settings → HUD / UI; unavailable modes fall back to desktop fullscreen
  with a status line; F11 restores the saved window size; windows fit the monitor's usable area;
  re-applied when the window moves to another display.
- Controls (Settings → Controls): WASD / ZQSD / arrow keys, mouse sensitivity 10–300 %, invert Y,
  raw relative input on / off (Windows acceleration), left or right mouse button fires.
- Focus: losing focus clears held keys, buttons and axes and releases mouse capture; the menu
  self-test exercises the window-size setting.
- Audio: the output stream is reopened when the device in use disappears or a device appears while
  the game is silent.
- Vulkan 1.1 device baseline (was 1.2): 1.2 driver properties only on a negotiated 1.2+ path.
- Evidence: Linux core 41/41, software-Vulkan suite (one flaky input test passed on rerun);
  Windows CI run 43425587347 succeeded.

## Part 2 — branch `n9` (2026-11-18, this session)

1. **Merge.** The final N8 (03351cc: soak and cross-play tests, revised roadmap, N8 docs) merged
   into `n9-windows`. Code merged without conflicts; four documents were resolved in favour of the
   N8 record. Linux: 73/73 tests, solo replay lines identical to N8.
2. **Audit against the revised handoff's N9 list.** Present: fullscreen, resolution, refresh,
   mouse, keyboard, audio, settings navigation, focus, asset loading next to the executable,
   EXE packaging (CI package + validation). Missing: crash reporting; the EXE had no icon, version
   information or manifest; the log silently failed when the game folder was not writable.
3. **Crash reporting** (`cat::crash`). Windows: unhandled exceptions, `std::terminate`, invalid
   CRT parameters and pure virtual calls → minidump + text report (module + offset, Windows
   version, last 64 log lines), written from a helper thread (stack overflows), then a message box
   naming the files. POSIX: signals → text report with backtrace. `--crash-test` / `--crash-dir`.
   CTest `crash_report_access` (null write) and `crash_report_terminate` (uncaught exception path)
   — Linux PASS; Windows (with the `.dmp`) in CI.
4. **Log folder.** Log and crash reports go next to `CatArena.exe` when writable, else to
   `%APPDATA%\CatArena\FeralClaw` (SDL_GetPrefPath).
5. **Executable resources.** Original icon drawn by `tools/n9/make_icon.py` (16–256 px),
   VERSIONINFO (0.9.0.0, "N9"), manifest (Windows 10/11 compatibility, long paths; DPI left to SDL
   as before). `validate_package.cmake` checks that the version resource is in the EXE.
6. **Docs.** `WINDOWS_ROADMAP.md` (480 MiB) marked superseded by `ROADMAP_N8_N20.md` (192 MiB).
7. **Linux:** 75/75 tests (the two new crash tests included).
8. **Windows CI, run 44466414431** (9bb3784): build, package (version resource found), 45/45
   GPU-free tests including both crash tests; replay lines identical to Linux.
9. **Minidump check fixed.** The crash-test script tested `if(WIN32)`, which CMake does not set in
   script mode, so the `.dmp` was not actually checked. Now `CMAKE_HOST_WIN32`; run 44467678007:
   minidumps of 253,555 and 245,892 bytes.
10. **WinHTTP hang found and fixed.** In run 44467678007 `coop_multiplayer_winhttp` timed out after
    300 s (it passed in the job's verbose rerun, and in every earlier run). Most likely cause, from
    reading the code (no stack trace was available from the runner):
    `close()` used `WinHttpWebSocketClose`, which waits for the server's close frame while the
    worker thread is blocked in `WinHttpWebSocketReceive` on the same handle; the destructor's
    `join()` then waited forever. This is the `wss://` path the game uses with the Render relay,
    so a player leaving a server could have frozen the game. Fix: `close()` only sends the close
    frame (`WinHttpWebSocketShutdown`); the worker closes the handle when its receive returns; the
    destructor cancels a receive still pending after 3 s. CI now repeats both WinHTTP tests 15 times
    with a 90 s timeout.
11. **Windows CI, run 44469465569** (final code): 45/45 tests, crash tests with minidumps (242,434
    and 237,139 bytes), WinHTTP stress 15/15 rounds of both tests, replay lines identical to Linux
    and N8. Artifact `CAT_ARENA_FERAL_CLAW_N9_WINDOWS` (id 14504490142, 35,463,344 bytes). 15 clean
    rounds make the fix likely, not proven: the hang had appeared once in all CI runs so far.
