# Q3E architecture — Cat Arena on Quake3e (target design)

Status: the layout below exists since stage B (branded engine, the three modules, base folder,
packaging); the Cat Arena systems in the table are still to be ported (stages C–E). It follows
Quake3e's own module architecture; Cat Arena code goes where Quake III game code goes.

```
CatArena.exe  (engine/quake3e, C, GPL v2)            catarena/  (base folder, fs_basegame)
├─ qcommon: hunk/zone memory, pk3 file system,        ├─ qagamex86_64.dll   server game   ─┐
│  cvars, console, BSP collision + traces (cm_*),     ├─ cgamex86_64.dll    client game    ├ Cat Arena modules
│  net channel, delta snapshots, VM / DLL loader      ├─ uix86_64.dll       menus          ─┘ (C ABI, C++20 inside)
├─ server: snapshots, client slots, usercmds          ├─ default.cfg
├─ client: prediction driver, input, sound mixer      └─ pak0.pk3  textures, shaders, models, sounds, music, maps/*.bsp
└─ renderervk: BSP + PVS world, lightmaps, shaders,
   MD3 / IQM models, VBO world, flares, offscreen FX
```

## Module boundary

- Each module exports exactly `dllEntry(syscallptr)` and `vmMain(command, ...)` with C linkage, as
  Quake3e loads them (`qcommon/vm.c` `VM_LoadDll`). Cat Arena modules implement them in a thin C
  (or `extern "C"`) layer that calls into C++20.
- No C++ exception crosses `vmMain`: every entry catches, logs and turns errors into
  `trap_Error` (engine's error path).
- Memory ownership: the engine owns hunk / zone; modules own their C++ allocations, made at map
  load into fixed-capacity pools (entities, enemies, projectiles, effects) so gameplay frames do
  not allocate. Engine handles (shaders, models, sounds) are plain integers, never freed by modules.
- QVM is not supported for Cat Arena modules (C++). Windows ships native DLLs only; `vm_game`,
  `vm_cgame`, `vm_ui` are forced to 0 by `default.cfg`.

## Where each Cat Arena system goes

| System | Module | Language | Source to adapt |
|---|---|---|---|
| Player movement (shared by server and predicting client) | `bg_*` in qagame + cgame | C | N10 `player.cpp` / `physics_world.cpp` rules (D1) on engine traces; Q3 `bg_slidemove` stepping |
| Weapons, projectiles, splash, recoil, pickups, medkits, frags | qagame (authority), cgame (effects) | C++20 | N10 `combat.cpp`, `weapon_fx.cpp` |
| Enemies, waves, AI, routes, flyers, enemy shots | qagame | C++20 | N10 `enemies.cpp`, `navigation.cpp` |
| Enemy / Marine Cat animation | cgame (posing) + renderer (MD3 / IQM) | C++20 | N10 `enemy_fx.cpp`; format decided in Q3E-D |
| Downed / revive / squad wipe / scoring | qagame | C++20 | N10 `combat.cpp`, `coop.cpp` rules |
| HUD, damage numbers, name tags | cgame | C++20 | N10 `ui_hud.cpp`, `hud.cpp` |
| Menus, settings, server browser | ui | C++20 | N10 `menu.cpp`, `game_settings.cpp` (settings become cvars) |
| Sound rules (which sample, when, how loud) | cgame | C++20 | N10 `audio_game.cpp`; mixing by the engine |
| Collision, traces, PVS, rendering, mixing, networking, input, files | engine | C | Quake3e as is |

## Weapons (stage D, part 3)

- **State.** Each player carries two N10 weapons in Quake III's `WP_MACHINEGUN` slot:
  `stats[STAT_CA_LOADOUT]` holds both weapon numbers (N10 indices 0-15), the slot in hand and
  the switch-button latch; `STAT_CA_CLIP` / `STAT_CA_CLIP2` the two magazines. The table is the
  C++20 `CA_Weapon()` (`game/catarena/src/ca_weapons.cpp`, the N10 `kWeapons` rows), linked into
  both qagame and cgame.
- **Predicted (bg_pmove.c, server and client).** Fire rate (N10 delay), magazine, reload (R,
  or an empty magazine), switch (Q = `+button7`, 150 ms), shotgun recoil (back 4.7 / 5.7 /
  6.7 m/s; in the air 55 % and lifted to 7.3 / 7.8 / 8.2 m/s). `EV_FIRE_WEAPON` carries the
  weapon fired as its parameter, so the server fires the weapon the client predicted.
- **Server (g_ca_weapons.c).** Hitscan pellets with the N10 spread (yaw +-spread, pitch
  +-0.7 x spread), range and sweep radius; players do not block shots or projectiles; head hits
  (top third of the enemy box) count double. Rockets, grenades (own trajectory `TR_CA_GRAVITY`,
  15 m/s^2) and rift bolts are `ET_MISSILE` entities that explode on first contact or at the end
  of their life; a projectile that reaches the sky is removed without a blast. Blasts: N10 enemy
  and player splash by distance to the box, no line-of-sight test (as N10), full self damage
  (Quake III halves it), N10 launch. A frag rerolls the weapon in hand (`EV_CA_REROLL`); every
  spawn rolls a new pair. Test hooks: `ca_loadout`, `ca_give`, `ca_selftest` fires the real
  weapon code at a fiend.
- **Client (cg_ca_effects.c).** `EV_CA_SHOT` (one per pellet: tracer, rail trail, thunderbolt
  beam, impact spark), `EV_CA_BLAST` (flash, light, smoke), projectile glows and trails, muzzle
  flash and light at the N10 muzzle point of each of the 16 viewmodels. Quake III's shell brass,
  blood blob and score plums are off (no art for them).
- **Network cost.** One temporary entity per pellet (a QUAD SHOTGUN shot is 20), broadcast;
  counted in the stage E snapshot audit.

## Data pipeline

- **Maps**: N10 compiled map data → `tools/q3e` converter → `.map` (one brush per convex slab,
  thin brushes for wall segments, solid prisms, sky shell to seal the arena, entities for spawns,
  pickups, jump pads) + visual mesh as `misc_model` → q3map2 (`-bsp -meta`, `-vis`, `-light`) →
  `maps/<id>.bsp`. Collision comes from the brushes (not from the triangle mesh) so it matches
  the N10 slab world.
- **Textures / materials** → TGA / JPG in the pk3 + Quake III `.shader` scripts.
- **Sounds** → WAV / OGG. **Models** → MD3 (props), IQM or MD3 (enemies).

## Networking (Q3E-E)

- Server-authoritative game on the engine's client / server model; up to 4 players; a "host"
  runs a listen server.
- Enemies, players, projectiles and pickups as `entityState_t` entities in delta snapshots;
  cosmetic effects as events (no entity per effect).
- Audit before E: snapshot entity count against `MAX_SNAPSHOT_ENTITIES` 256 (SOURCE_AUDIT §2).
