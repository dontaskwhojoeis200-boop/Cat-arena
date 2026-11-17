# N8 development log — multiplayer (2026-11-12 … 2026-11-16)

Chronological; every result below was produced by a command that ran in this session.

1. **Inspection.** The N7 source (branch `n7`, commit 5a220aa) had a placeholder MULTIPLAYER page
   and no network code. The legacy multiplayer (v3.24: `legacy/index.html` 15495–16070 and
   `reference/multiplayer/server.js`) was read for the protocol (create / join / relay, roster,
   peer events), the host-authoritative rules (targeting, remote damage, downed / revive / wipe,
   remote fire, medkits for teammates, pose validation) and the snapshot format (keys, limits,
   quantisation). Outstanding for N8: everything, plus name tags (absent from the legacy).
2. **Roadmap.** `docs/ROADMAP_N8_N20.md` written for the 2026-11-12 handoff (Windows-only, 480 MiB),
   then rewritten for the revised handoff of 2026-11-16 (192 MiB = 128 CPU + 64 GPU, Quake III
   direction, Vulkan 1.1 as intended baseline). Audit for the latter: the renderer detects all
   newer features at runtime but rejects devices below API 1.2; lowering to 1.1 is planned for N9.
3. **Network layer.** JSON, WebSocket over plain sockets, HTTP GET, and the relay in C++. No new
   third-party dependency; WinHTTP (system library) for `wss://` on Windows.
   `net_relay_protocol`: 79 checks pass against the native relay; the same checks against the
   untouched `server.js` (Node 22, npm `ws`) pass 78 (the 79th check reads the native relay's
   own statistics, which server.js does not have).
4. **Simulation.** Teammates, NetRole, targeting, remote damage / fire / frags, revive, wipe,
   puppets. Solo replays compared line by line with N7 after every change: identical.
5. **Client (`net::Coop`) and tests.** `coop_multiplayer`: rules + four clients over TCP.
   Found while writing it: a dangling reference (range-for over `hero(t).as_object()`, a
   temporary) in the snapshot writer — fixed; two test mistakes (the 2 s grace after a revive,
   the wipe clock starting inside the downing step). Enemies killed guests during the network
   part, which made the damage checks depend on timing; the test now freezes the wave first.
6. **Menus and HUD.** Host map page with text fields (SDL text input), server browser, connecting
   page, game menu that keeps a server running, name tags, revive line, badge. `--ui-capture`
   pictures checked by eye (docs/n8-evidence/ui).
7. **Window self-test.** `--coop-selftest` drives the real game through the menus and adds three
   in-process guests. First run: guests were told "Disconnected from the multiplayer service"
   instead of "Host disconnected": the built-in relay was stopped at once when the host left,
   closing sockets with unread data, which can reset the connection and drop the last message.
   The relay now stops once the guests have gone (at most 3 s later). PASS since.
8. **Windows CI, first N8 run** (43359148828, commit a0fd083): build, package, 42 GPU-free tests
   pass, including the WinHTTP client (`CAT_NET_WINHTTP=1`) and both interop tests with the
   original `server.js`. The co-op test takes 9.3 s there against 0.9 s on Linux (Windows sleep
   granularity in the test loop; the game does not sleep in its network path).
9. **Soak.** 4 clients, live waves, random input; a first version measured snapshot gaps in game
   time while the network ran in real time, which failed under CPU load without a fault in the
   game. Now each snapshot round must be delivered within 0.5 s of real time. `coop_soak` (900 s
   of game time) in CTest.
10. **Cross-play with the original browser game.** First attempt: 6 / 13 checks — the headless
    page runs at about 1 FPS (software WebGL), so fixed waits read stale state, and the idle
    browser guest was downed by enemies before the damage checks. The native side now takes
    commands from the driver (freeze, damage, down, walk / stop) and the driver polls; 13 / 13.
11. **Linux matrix.** Run 1: both clang configurations failed to compile (`-Wsign-conversion` in
    the SHA-1 loop, an unused member) — fixed. Run 2: clang Debug failed 5 tests with a crash in
    `free()` — not a code fault: the matrix tree is synced with `tar`, which kept source times older
    than object files built from an intermediate state, so CMake did not rebuild them (a stale
    test object with the old `Coop` layout). Run 3 with freshly stamped sources: all seven builds
    PASS (41 core / 73 desktop tests, 0 warnings, ASan / UBSan clean), replays identical.
12. **Memory.** Private bytes added to the process report; the self-tests log working set, peak,
    private and the renderer's Vulkan allocations (N8_MULTIPLAYER.md §4).
13. **Windows CI, second N8 run** (44014040537, commit 21bed9e): 43 / 43 GPU-free tests including
    the 15-minute soak; replay lines identical to Linux. Artifact `CAT_ARENA_FERAL_CLAW_N8_WINDOWS`.
