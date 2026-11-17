# N8 — four-player co-op multiplayer

Native port of the multiplayer of the legacy game (v3.24, `legacy/index.html` 15495–16070 and
`reference/multiplayer/server.js`): one host simulates the arena, up to three guests join through a
WebSocket relay, everybody fights the same waves. The wire protocol is the legacy one, byte for
byte in its JSON keys and quantisation, so the native game, the original relay and the original
browser game interoperate (tested, section 5).

Status classification used below: **Implemented** (code exists) · **Compiled** · **Executed** (ran)
· **Tested** (ran with checks that pass) · **Unverified** (not run) · **Blocked**.

## 1. What the player gets

| Feature | Status | Notes |
|---|---|---|
| MULTIPLAYER > HOST A SERVER: map choice, PLAYER NAME, SERVER NAME, SERVER ADDRESS | Tested (window self-test, captures) | Text fields with keyboard input (SDL text input), saved in settings.txt (`playerName`, `serverName`, `relay`) |
| HOST ON THIS PC (built-in relay on port 8787, all interfaces) | Tested on one machine | Native addition: the legacy needed the Node relay on Render |
| CREATE SERVER on a typed relay (`ws://`, `wss://`) | Tested with `ws://` | `wss://` uses WinHTTP (Windows only); TLS path not exercised (section 7) |
| JOIN A SERVER: live server list (GET /rooms), 9 s refresh, JOIN, FULL / OTHER MAP | Tested | Joining loads the room's map (the legacy page reloaded itself on that map) |
| Up to 4 players, room badge `HOSTING · 2/4 PLAYERS` / `CONNECTED · …` | Tested | Relay limit 4 (`kRoomSize`), 100 rooms, 410 peers, as server.js |
| Host-authoritative enemies, waves, damage, pickups | Tested | Guests show the host's enemies as puppets (90 ms snapshots, `1-exp(-16 dt)` smoothing) |
| Player movement / aim / weapon fire synchronisation | Tested | Pose every 48 ms; each volley sent as `fire`, simulated by the host with the shooter's pose; host checks move distance and capsule |
| Enemy targeting of any live player (nearest), hit boxes of remote players | Tested | `enemyTargetFor`, Marine Cat 6-part boxes for enemy shots |
| Projectiles: enemy shots synchronised; players' rockets / grenades / rift bolts simulated by the host and credited to their owner | Tested | Other players' rockets are not drawn on other machines (legacy behaviour: snapshots carry enemy shots only) |
| Kills by guests: `frag` → their score and weapon reroll | Tested | |
| Damage to guests, medkits for guests (host-side) | Tested | |
| Downed state at 0 HP, revive by holding F for 4 s within 3.2 m with line of sight (45 HP) | Tested | Gamepad: hold D-pad down. Touch revive button: not added (touch is out of scope) |
| Squad wipe: everyone down for 2.7 s → wave 1 restarts for all | Tested | Restart in place on the loaded map (no loading screen) |
| Name tags over teammates (name, health bar, DOWN / HOST) | Tested (capture, self-test counts drawn tags) | New: the legacy had no name tags. Dimmed when a wall is in between |
| Revive status line (`HOLD F TO REVIVE …`, `REVIVING … %`, `YOU ARE DOWN …`, `SQUAD ELIMINATED …`) | Tested | Texts of `coopReviveStatus` |
| Join / leave announcements, `TEAMMATE LEFT · ROOM STILL OPEN` | Tested | |
| Host leaves → guests: "Host disconnected. This server is closed…" and back to the menu | Tested | |
| Connection lost / relay closed / refused join / incompatible relay → message, back to the menu | Tested (relay errors, closed sockets); stalled snapshots message Implemented | |
| Game menu (Esc) does not pause a server; map change (1–4, F5, CHOOSE ANOTHER MAP, MAP button) disabled in a server | Tested (self-test checks the clock runs under the menu) | Deliberate: a paused host would freeze everybody |
| `CatArena.exe --relay-server [--port N] [--bind ADDR]` (dedicated relay, no window) | Tested (cross-play B uses it) | `PORT` environment variable as server.js |

## 2. Architecture

```
cat_arena_net   json.cpp     JSON value / parser / writer (numbers printed like JavaScript)
                net.cpp      URLs, sockets (Winsock / BSD), WebSocket client over TCP (ws://),
                             HTTP GET (rooms list), SHA-1 / base64 for the handshake
                net_winhttp.cpp  Windows: WinHTTP WebSocket + HTTP (wss:// and https://, system TLS)
                relay.cpp    the relay of server.js in C++ (HTTP /rooms /health, WebSocket /ws)
cat_arena_coop  coop.cpp     the legacy client (netConnect, netOnRelay, netPose, netSnapshotMessage,
                             netHostTick glue, netFindValidSpawn, server list)
combat / enemies             teammates, NetRole, downed / revive / wipe, remote fire, puppets
main_menus.inc               menus, connection lifecycle, restart on wipe, name tags, HUD lines
```

No third-party networking library: plain sockets everywhere, plus the WinHTTP system library on
Windows for TLS. The relay runs on its own thread (poll loop); the client is polled once per frame
on the game thread (WinHTTP receives on a worker thread into a queue).

### Roles

- **Host**: runs the only simulation of enemies, waves, damage, pickups, downed / revive / wipe.
  Guests are `combat::Teammate`s in its arena (position from their poses, health owned by the
  host). Every 90 ms it sends each guest a `world` snapshot made for that guest (`you` = the
  guest's own health / downed state).
- **Guest**: simulates its own movement and weapons locally (immediate response), sends a pose every
  48 ms and each volley as `fire`; damages nothing itself (`take_damage` / `damage_target` are the
  host's), shows the host's enemies, enemy shots, pickups and wave numbers from snapshots.

### Packets (unchanged from the legacy)

| Direction | `type` | Content |
|---|---|---|
| relay | `create` / `join` / `relay` (`to`) | name, title, build `VA-MODULAR-RB-011`, map id (`reactor-bastion-v1` …) |
| relay → client | `ready`, `roster`, `peer` (`guest_joined`, `guest_left`, `host_left`), `error` | |
| guest → host | `pose` | x, y, z, a, pitch, vx, vy, c, step, holdingRevive |
| guest → host | `fire` | weapon |
| host → guest | `world` | wave, total, kills, spawned, cooldown; pickups (≤ 64); bots (≤ 40: u, t, x, y, z, a, h, maxHp, phase, walk, hit, wind, totalWind, recover, totalRecover); shots (≤ 100); players; you; reviveFor, reviveTime, wipe — quantised to 1/1000 and 1/100 as `netQ` / `netQ2` |
| host → all | `frag` (head), `shotFx` (weapon, + `shooter`), `restart`, `map` | |

Deliberate differences from the legacy client (protocol-compatible both ways):
1. A guest loads the room's map itself; the legacy page reloaded itself with `?map=`.
2. `shotFx` carries `shooter` (the relay id) so the right Marine Cat plays the shoot pose; the
   legacy ignores the extra key.
3. A server's map is fixed while it is open (the legacy could load another map under a room).
4. The game menu does not pause a server (the legacy paused the host's simulation for everybody).
5. Name tags (new), HOST ON THIS PC and `--relay-server` (new).

### Determinism

Every multiplayer path is behind `NetRole != solo` or a non-empty team: the solo legacy replays
(combat, enemies, physics, animation, sound rules) are bit-identical to N7 on Linux and Windows.

## 3. Bandwidth (measured in the tests, Reactor Bastion)

| Situation | Host upload (snapshots to 3 guests) | Per guest |
|---|---|---|
| `coop_multiplayer` (wave 1, part of the time frozen) | 80.4 KiB/s | ≈ 27 KiB/s |
| `coop_soak` / 1 h soak (live waves, 4 players moving and firing) | 115.9 KiB/s | ≈ 39 KiB/s |
| Guest upload (poses + volleys) | ≈ 2–3 KiB/s | |

Snapshots are JSON as in the legacy (no compression). The relay forwards each message once.

## 4. Memory

Measured with the window self-tests on the Linux development machine (software Vulkan, lavapipe:
"GPU" allocations live in system memory there, so the working set already contains them — the
two numbers must not be added). See section 6 for the figures. The network layer itself is small
(a few hundred KiB of buffers per connection at most); the in-process relay holds one entry per
peer and room. Measurement on Windows with a real GPU: Unverified (no GPU run yet).

## 5. Tests

| Test | What | Linux | Windows CI |
|---|---|---|---|
| `net_relay_protocol` | JSON, WebSocket codec, URLs, relay create / rooms / health / errors / join / full room / routing / leave / host_left (79 checks) | Tested | Tested |
| `net_relay_protocol_winhttp` | the same through the WinHTTP client (`CAT_NET_WINHTTP=1`, plain ws:// / http://) | — | Tested |
| `net_interop_legacy_relay` | the same checks against the ORIGINAL server.js with npm `ws` (78 checks) | Tested | Tested |
| `coop_multiplayer` | rules (targeting, downed, revive timing 4 s, wipe 2.7 s, remote kills incl. rockets, medkits, guest puppets) + 4 clients over TCP: list, join, snapshots, poses, remote kill, damage, downed, revive by a guest holding F, wipe and restart, leave, host leaves (52 checks) | Tested | Tested |
| `coop_multiplayer_winhttp` | the 4-client part with WinHTTP clients | — | Tested |
| `coop_interop_legacy_relay` | the 4-client part through the ORIGINAL server.js (30 checks) | Tested | Tested |
| `coop_soak` | 4 clients, live waves, random movement / fire, 15 min of game time; wipes restart everybody | Tested | Tested |
| 1 h soak (`--soak 3600`, by hand) | 3600 s game time: 82 squad wipes, 39 277 snapshot rounds, 216 024 poses, 456 354 relayed messages, no disconnect, worst snapshot gap < 1 s | Tested | — |
| `window_coop_selftest` | the real game: MULTIPLAYER → HOST A SERVER → typed name (SDL text input) → HOST ON THIS PC → ENTER THE ARENA; 3 in-process guests join over TCP; snapshots, Marine Cats (3) and name tags (3) drawn, downed + revive, game menu keeps running, LEAVE SERVER, guests told, exit | Tested | Unverified (needs a GPU) |
| `--ui-capture` pages | multiplayer, servers, connecting, host map page, co-op HUD (staged teammates), game menu in a server | Executed (pictures in docs/n8-evidence) | — |
| Cross-play with the original browser game (`tools/n8/crossplay_legacy.cjs`) | see section 5.1 | see 5.1 | — |

### 5.1 Cross-play with the original browser game

CROSSPLAY_RESULTS

## 6. Results of this milestone

RESULTS_TABLE

## 7. Not verified / open

- Play between separate PCs (LAN), over the internet, through NAT / port forwarding, with real
  latency and jitter: Unverified. All runs used one machine (loopback TCP).
- Windows Firewall prompt for HOST ON THIS PC (listening on all interfaces): Unverified.
- `wss://` / `https://` relays (TLS through WinHTTP): the WinHTTP code path is tested on Windows
  without TLS; a TLS relay (for example the original on Render) was not reached from CI or this
  session. Linux builds do not support `wss://` (development host only).
- The interactive multiplayer on Windows with a GPU (menus, name tags, Marine Cats): Unverified —
  the GitHub runner has no GPU; the window self-test ran on Linux only.
- Touch revive button and touch text entry: not implemented (touch / Android out of scope from N8).
- Other players' rockets, grenades and explosions are not drawn on other machines (as the legacy).
- Host migration: none (as the legacy): when the host leaves the server closes.
