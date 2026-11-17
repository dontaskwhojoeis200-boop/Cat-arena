# Cat Arena: Feral Claw — native rewrite, N8 (four-player co-op multiplayer)

Complete source tree after N8. Read `docs/N8_MULTIPLAYER.md` for what was built and how it was
checked, `docs/ROADMAP_N8_N20.md` for the revised direction (Windows-only, 192 MiB combined memory,
Quake III Arena-inspired art); players read `README_WINDOWS.md`.

- **Host / join**: MULTIPLAYER > HOST A SERVER (map, player name, server name; HOST ON THIS PC runs
  the relay inside the game on port 8787, CREATE SERVER uses a typed relay address) and JOIN A
  SERVER (live server list, refreshed every 9 s). `CatArena.exe --relay-server` runs the relay alone.
- **Up to four players** against the same waves. The host simulates enemies, waves, damage and
  pickups; guests send their movement and shots and show the host's world.
- **Synchronised**: player movement and aim, weapon fire and its kills (credited to the shooter),
  enemies and their shots, damage, medkits, wave numbers.
- **Downed and revive**: at 0 health a player is down; a teammate holds **F** (gamepad: D-pad down)
  next to them for 4 seconds; when everybody is down the wave restarts at 1.
- **Name tags** over teammates (name, health, DOWN / HOST), revive line, room badge.
- **Disconnections**: join / leave messages, host leaving closes the server for everybody with a
  message, lost connections and refused joins return to the menu with the reason.
- The wire protocol is the legacy one: the native game works with the original relay
  (`reference/multiplayer/server.js`) and with the original browser game (tested both ways).

Status:
- Linux: built and tested in seven configurations (41 core / 73 desktop tests each, all PASS, 0 warnings); solo replays identical to N7.
- Windows: built, packaged and tested (43 GPU-free tests PASS, including the WinHTTP client, the
  original server.js and a 15-minute four-player soak) by GitHub Actions run
  https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/44014040537 on branch `n8`;
  game folder = artifact `CAT_ARENA_FERAL_CLAW_N8_WINDOWS`
  (https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/44014040537/artifacts/14297264643).
- Not yet: play between separate PCs, over the internet, through NAT, with a TLS (`wss://`) relay,
  or on Windows with a GPU.

Try it on Windows: unzip the artifact, start `CatArena.exe` on two PCs of one network; on the first
MULTIPLAYER > HOST A SERVER > HOST ON THIS PC (allow the firewall prompt for private networks), on
the second MULTIPLAYER > JOIN A SERVER, type the first PC's address (for example
`192.168.1.20:8787`) in SERVER ADDRESS, REFRESH LIST, JOIN.

Run (developers): `cat_arena --coop-selftest`, `cat_arena --relay-server`, `cat_arena_coop_tests
assets/n1 [--soak 3600]`, `tools/n8/crossplay_legacy.cjs` (docs/BUILD.md, N8).
