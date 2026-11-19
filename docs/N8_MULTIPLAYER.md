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
| Downed state at 0 HP, revive by holding F for 4 s within 3.2 m with line of sight (45 HP) | Tested | Gamepad: hold D-pad down (Implemented, no pad to test). Touch revive button: not added (touch is out of scope) |
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
| `coop_soak` (live waves, 4 players moving and firing, 900 s of game time) | 115.9 KiB/s of snapshots, 116.7 KiB/s in all | ≈ 39 KiB/s |
| Guest upload (poses, volleys, relay envelope), same run | | 4.0–4.1 KiB/s |

Snapshots are JSON as in the legacy (no compression). The relay forwards each message once.

## 4. Memory

First figures for the 192 MiB objective (docs/ROADMAP_N8_N20.md), measured at the end of the
window self-tests on the Linux development machine. That machine renders with lavapipe (software
Vulkan): "GPU" allocations live in process memory, so the working set already contains them and
the two numbers must not be added; the working set also contains the software rasteriser.

| Run | Working set (peak) | Private | Renderer's Vulkan allocations |
|---|---|---|---|
| `window_menu_selftest` (solo: the scripted tour of menus, settings and a game) | 534.6 MiB (534.8) | 302.0 MiB | 149.9 MiB |
| `window_coop_selftest` (host + 3 guest simulations in the same process) | 437.4 MiB (437.4) | 248.7 MiB | 104.0 MiB |

What this says, and what not: the engine's own GPU allocations (exact sums of `vkAllocateMemory`,
independent of the device) are already 104–150 MiB, above the 64 MiB GPU budget before any
art replacement — the first concrete target for N12 (categories) and N15 (reduction). CPU-side
figures on lavapipe are not representative of a Windows PC with a GPU. Networking memory was not
separable in these process-level figures: the client keeps one connection with its receive buffer
and a packet queue (≤ 60 while loading), the relay one entry per peer and room; per-subsystem CPU
accounting arrives with N12. An upper bound for networking plus simulation without rendering: the
1 h soak process (relay, four clients, four simulations of Reactor Bastion) peaked at 26.0 MiB
resident (Linux `VmHWM`). Windows with a real GPU: Unverified.

## 5. Tests

| Test | What | Linux | Windows CI |
|---|---|---|---|
| `net_relay_protocol` | JSON, WebSocket codec, URLs, relay create / rooms / health / errors / join / full room / routing / leave / host_left (79 checks) | Tested | Tested |
| `net_relay_protocol_winhttp` | the same through the WinHTTP client (`CAT_NET_WINHTTP=1`, plain ws:// / http://) | — | Tested |
| `net_interop_legacy_relay` | the same checks against the ORIGINAL server.js with npm `ws` (78 checks) | Tested | Tested |
| `coop_multiplayer` | rules (targeting, downed, revive timing 4 s, wipe 2.7 s, remote kills incl. rockets, medkits, guest puppets) + 4 clients over TCP: list, join, snapshots, poses, remote kill, damage, downed, revive by a guest holding F, wipe and restart, leave, host leaves (52 checks) | Tested | Tested |
| `coop_multiplayer_winhttp` | the 4-client part with WinHTTP clients | — | Tested |
| `coop_interop_legacy_relay` | the 4-client part through the ORIGINAL server.js (30 checks) | Tested | Tested |
| `coop_soak` | 4 clients, live waves, random movement / fire, 900 s of game time (23 squad wipes, 9 823 snapshot rounds, 54 024 poses, 119 688 relayed messages); every snapshot round delivered within 0.5 s, nobody disconnected, guests on the host's wave | Tested | Tested (11 wipes; timing-dependent) |
| 1 h soak (`--soak 3600`, by hand) | 3600 s of game time in 82 s: 89 squad wipes, 39 277 snapshot rounds (114.7 KiB/s), 216 024 poses, 459 841 relayed messages; every round delivered within 0.5 s, no disconnect; peak resident memory of the whole test process (relay + 4 clients + 4 simulations, no rendering) 26.0 MiB | Tested | — |
| `window_coop_selftest` | the real game: MULTIPLAYER → HOST A SERVER → typed name (SDL text input) → HOST ON THIS PC → ENTER THE ARENA; 3 in-process guests join over TCP; snapshots, Marine Cats (3) and name tags (3) drawn, downed + revive, game menu keeps running, LEAVE SERVER, guests told, exit | Tested (4 desktop builds) | Unverified (needs a GPU) |
| `--ui-capture` pages | multiplayer, servers, connecting, host map page, co-op HUD (staged teammates), game menu in a server | Executed (pictures in docs/n8-evidence) | — |
| Cross-play with the original browser game (`tools/n8/crossplay_legacy.cjs`) | see section 5.1 | see 5.1 | — |

### 5.1 Cross-play with the original browser game

`tools/n8/crossplay_legacy.cjs` runs the **unmodified legacy page** (SHA-256 checked) in headless
Chromium 153 against native clients. Browser actions are real clicks on the legacy buttons and
real key presses; game state is read from the page's globals. Evidence:
`docs/n8-evidence/crossplay` (JSON results, native logs, relay log, screenshots).

| Check | Result |
|---|---|
| A. Browser server list shows the native host's room (title, host, 1/4) | PASS |
| A. Browser guest sees the native host and its 10 enemies (wave 1) | PASS |
| A. Native host clears the wave and restores the guest: browser shows 0 enemies, 100 HP | PASS |
| A. Browser guest walks (W): the native host has it at the same spot (< 0.5 m) | PASS |
| A. Native host damage 30 → browser HP 70; 200 → browser downed | PASS, PASS |
| A. Closing the browser tab → native host "TEAMMATE LEFT" | PASS |
| B. Native server list shows the browser host's room (relay: `cat_arena --relay-server`) | PASS |
| B. Browser host and native guest see each other | PASS |
| B. Native guest shows the browser host's 10 enemies (snapshots) | PASS |
| B. Native guest's position reaches the browser host (< 0.5 m) | PASS |
| B. Browser host's `remoteApplyDamage(30)` → native guest HP 70 | PASS |
| B. Browser host quits → native guest "Host disconnected …" | PASS |

13 of 13 checks pass. Caveat: the headless page renders with software WebGL at 480×270 and ran
at 1.2 FPS (as guest) and 0.3 FPS (as host), so the browser side was slow (two snapshot rounds
from the browser host during the B checks); the checks poll with timeouts. Timing behaviour
(interpolation smoothness, latency) between the native game and a real browser was not judged.

## 6. Results of this milestone

Classification of the N8 requirements of the handoff.

| Requirement | Status | Basis |
|---|---|---|
| Host / join | **Tested** | window self-test (menus), 4-client tests, cross-play both ways |
| Server browser | **Tested** | `/rooms` against the native relay and server.js; browser lists native rooms and vice versa |
| Four-player sessions | **Tested** | 4 clients in `coop_multiplayer`, soak, window self-test; a fifth player is refused (`net_relay_protocol`, native relay and server.js) |
| Player synchronisation | **Tested** | pose distance checks (< 0.5 m) native ↔ native and native ↔ browser |
| Enemy synchronisation | **Tested** | same enemy ids / counts / wave on guests; puppets and deaths |
| Projectile synchronisation | **Tested** for enemy shots and for kills by players' projectiles; other players' rockets not drawn (legacy behaviour) | `coop_multiplayer` rules, snapshot checks |
| Damage synchronisation | **Tested** | host damage → guest HP (native and browser), browser host damage → native guest |
| Downed and revive | **Tested** | 4 s / 3.2 m / line of sight, 45 HP; wipe after 2.7 s; in the window self-test with a guest holding F |
| Name tags | **Tested** on Linux (drawn count, pictures) | new in N8 |
| Disconnection handling | **Tested** | guest leaves, host leaves, browser tab closed, browser host quits, refused joins, closed relay |
| Preserve original mechanics | **Tested** | solo replays bit-identical to N7 (Linux, Windows); legacy rules ported line by line |
| No unnecessary network dependencies | Done | sockets + WinHTTP (system); npm `ws` only for interop tests |
| Networking memory tracking | **Executed** (first figures, §4) | per-subsystem accounting is N12 |
| Windows multiplayer build | **Compiled, Tested without GPU** | CI run 44014040537: 43 / 43 GPU-free tests incl. WinHTTP, server.js interop, soak |
| Windows play with a GPU, LAN / internet, NAT, TLS relay | **Unverified** | no Windows PC with a GPU, one machine only, no TLS relay reachable |

Linux matrix (seven configurations: GCC / clang, Debug / Release, ASan + UBSan): every build PASS
(41 core / 73 desktop tests), 0 warnings, solo replays identical in all of them (TEST_MATRIX.md).


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
