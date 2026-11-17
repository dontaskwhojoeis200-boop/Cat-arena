# N8 change list (relative to N7, commit 5a220aa)

New
- `native/include/cat/json.hpp`, `src/json.cpp` — JSON value, parser (depth 64, duplicate keys as
  JavaScript), writer with JavaScript number formatting.
- `native/include/cat/net.hpp`, `src/net.cpp` — URLs and relay addresses (`netRelayURL` /
  `netRoomsURL`), sockets (Winsock / BSD), WebSocket client over TCP, frame codec, HTTP GET
  (Content-Length and chunked), SHA-1 / base64; `CAT_NET_WINHTTP=1` test switch (Windows).
- `native/src/net_winhttp.cpp` — Windows: WinHTTP WebSocket and HTTP client (`wss://`, `https://`).
- `native/include/cat/relay.hpp`, `src/relay.cpp` — the relay of `reference/multiplayer/server.js`
  in C++ (rooms, `/rooms`, `/health`, `/ws`, limits, heartbeat, error texts).
- `native/include/cat/coop.hpp`, `src/coop.cpp` — `net::Coop`: the legacy multiplayer client
  (host / join / leave, pose, fire, world snapshots, frag, shotFx, restart, roster, server list,
  spawn search, revive status, room badge, statistics).
- `native/tests/net_tests.cpp` (`net_relay_protocol`, `--external`), `tests/coop_tests.cpp`
  (`coop_multiplayer`, `--external`, `--soak`), `tests/coop_crossplay.cpp` (helper for the
  browser cross-play), `tools/n8/run_with_legacy_relay.cjs`, `tools/n8/crossplay_legacy.cjs`.
- `docs/N8_MULTIPLAYER.md`, `docs/DEVELOPMENT_LOG_N8.md`, `docs/N8_DIFF.md`,
  `docs/ROADMAP_N8_N20.md`, `docs/MIGRATION_LOG.md`, `docs/n8-evidence/`, `README_N8.md`.

Changed
- `combat.hpp/.cpp` — `NetRole`, teammates (`Teammate`, add / remove, boxes), nearest-player
  targeting (`target_for`), remote damage / downed, host downed instead of respawn with teammates,
  revive (4 s, 3.2 m, line of sight) and squad wipe (2.7 s) in `coop_tick`, remote fire with the
  shooter's pose and fire-rate limit, frag routing, projectile owners, medkits for teammates,
  guest-side authority switches; key bit `key_revive`.
- `enemies.hpp/.cpp` — targets chosen per enemy among live players, shots at remote players use
  the Marine Cat boxes, puppet mode (snapshot → enemies, death animations for vanished enemies,
  interpolation), `WorldState`.
- `menu.hpp/.cpp` — MULTIPLAYER page, host map page (PLAYER NAME / SERVER NAME / SERVER ADDRESS
  text fields, HOST ON THIS PC, CREATE SERVER), server browser, connecting page, game menu in a
  server (no pause, LEAVE SERVER, no map change); text field input (`MenuEvent::text`, `erase`).
- `ui_hud.hpp/.cpp` — `build_coop_hud`: name tags, revive line, room badge.
- `game_settings.hpp/.cpp` — `serverName`, `relay` saved; player name limited to 20 characters.
- `main.cpp`, `main_menus.inc` — connection lifecycle, built-in relay, server list, loading the
  room's map, guest spawn, restart on squad wipe, F / D-pad down revive, remote volley sounds,
  Marine Cats for the other players, simulation running behind the menu in a server,
  `--relay-server`, `--coop-selftest`, name-tag projection, staged co-op pictures in
  `--ui-capture`, memory lines at the end of the self-tests.
- `platform_info.hpp/.cpp` — private bytes (Windows `PrivateUsage`, Linux `RssAnon`).
- `CMakeLists.txt` — `cat_arena_net`, `cat_arena_coop`, tests (`net_relay_protocol`,
  `net_interop_legacy_relay`, `coop_multiplayer`, `coop_interop_legacy_relay`, `coop_soak`,
  `window_coop_selftest`, Windows-only `*_winhttp`), `cat_arena_coop_crossplay`; milestone N8;
  `window_input_selftest` runs serially.
- `.github/workflows/windows-build.yml` — milestone N8, npm `ws` for the interop tests only,
  multiplayer summary.
- `README_WINDOWS.md` — multiplayer section, F revive, `--relay-server`, known limitations.

Unchanged on purpose
- Solo simulation: every multiplayer path is behind `NetRole != solo` or a non-empty team; the
  legacy replays are bit-identical to N7.
- Renderer, shaders, assets.
