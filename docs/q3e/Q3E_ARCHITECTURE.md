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

## Enemies (stage D, part 4)

- **Types.** The 20 N10 types are one C++20 table (`game/catarena/src/ca_enemies.cpp`, from N10's
  `kTypes`, attack timings and the generated legacy tables), shared by qagame and cgame through
  `CA_EnemyType()` / `CA_EnemyShot()`. Hit boxes: the union of N10's two boxes per hit class.
- **Server (ca_horde.cpp, g_ca.c).** N10 spawn rules (random type; ground enemies on a random graph
  node of a random level where their capsule fits; flyers over open-sky nodes; the first three of
  a wave's batch are flyers); ground enemies walk with Pmove along N10 routes with their own
  navigation profile and stop at N10's distances; flyers run N10's `updateFlyer` steering on
  N10's collision world and are placed directly; the attack state machine with N10 wind-up,
  cooldown and reach; enemy projectiles are `ET_MISSILE` entities that only players and the world
  stop, with the fireball burn. The entity carries the type (`modelindex`), a spawn id
  (`powerups`), the start of the current attack (`time`) and the last hit (`time2`).
- **Client (cg_ca_enemies.c, ca_enemy_anim.cpp).** cgame loads N10's creature meshes (`.emesh`,
  unchanged) and poses each creature every frame with the C++20 port of N10's `enemyEvalPose`
  (gait from its own movement, look at the player, attack phases from the attack start, pain from
  the last hit); the skeleton deformation N10 does in its vertex shader runs on the CPU and the
  posed triangles go to the renderer as polygons (`trap_R_AddPolysToScene`), lit from the light
  grid at the creature. A death event leaves a client-side body with N10's death animation (or
  gibs below -40 health). Quake III's renderer has no API for bone poses, so this replaces N10's
  GPU skinning; the cost (about 260 triangles per creature) is to be measured in Q3E-F.
  `default.cfg` raises `r_maxpolys` / `r_maxpolyverts` to 16384 / 65536 for 20 creatures and 24
  bodies.
- **Maps.** The converter adds player clip along the open edges of N10's footprint, so players
  and enemies stay where N10 lets them be (blasts could throw a player off the map before).

## Data pipeline

- **Maps**: N10 compiled map data → `tools/q3e` converter → `.map` (one brush per convex slab,
  thin brushes for wall segments, solid prisms, sky shell to seal the arena, entities for spawns,
  pickups, jump pads) + visual mesh as `misc_model` → q3map2 (`-bsp -meta`, `-vis`, `-light`) →
  `maps/<id>.bsp`. Collision comes from the brushes (not from the triangle mesh) so it matches
  the N10 slab world.
- **Material mesh** (stage D part 5; Helios Crucible, Bastion Keep and The Arsenal draw most of
  their detail with it): N10's `materials.cmesh` as a second `misc_model`; N10's material
  definitions (Quake III-style multi-stage shaders from the legacy game) written back as Quake III
  shaders by `tools/q3e/n10materials.py`; their textures copied under the same names. The kit
  pieces' "vertex light" (N10: its baked lightmap sampled per vertex) is sampled the same way into
  the OBJ vertex colours and kept with `q3map_noVertexLight`.
- **Sky**: the map's N10 sky material; the `$baked` far box is baked offline from N10's sky
  geometry seen from the map centre, as the legacy game bakes it; the sun for q3map2 comes from its
  glow. The Arsenal keeps its nebula box.
- **Lighting**: q3map2 lights from the N10 light entities, then lights again with `-scale` until
  the mean lightmap brightness matches N10's baked lightmap (`tools/q3e/light_scale.py`).
- **Portals / mirrors** (The Arsenal, Helios Crucible's monitor): Quake III portal surfaces with
  `misc_portal_surface` / `misc_portal_camera` at N10's cameras. N10 only drew stand-ins for them.
- **Props** (The Arsenal): the flag and turret MD3s as `misc_ca_prop` entities, animated in cgame as
  N10 does (flag frames, the turret's head tracking the player, its barrel spinning).
- **Textures / materials** → JPG / PNG in the pk3 + Quake III `.shader` scripts.
- **Sounds** → WAV / OGG. **Models** → MD3 (weapons, props); enemies: N10 meshes posed in cgame.

## Sound (stage D, part 6)

Quake III plays a sample at one volume (`s_volume`), placed in the world or at the listener; it
has no per-call gain, playback rate, filters or buses. N10's sound rules (`native/src/audio_game.cpp`,
the legacy playGunSound / playExplosionSound / playBodySound / playEnemyVoice / beep) give each
sound a gain, a bus level and sometimes a rate and filters. So:

- **Offline** (`tools/q3e/sound_bake.cpp`, built against N10's `audio_mixer.cpp`): each sound is
  rendered through N10's own mixer with its gain, the default bus level (weapons 85 %, enemies
  80 %), the voice family's tone (bass .92, light 1.025) and filters, and scaled so that Quake III's
  mixer (127 x 255 s_volume / 65536 for a sound at the listener, 63.5 per side for a sound straight
  ahead) gives N10's output level with `s_volume` = N10's master. Gains that depend on the moment
  are baked at a few levels: landing force .3 / .6 / .95 / 1.3, slide .6 / .9 / 1.2, the blast
  power of rocket / grenade / rift.
- **In cgame** (`cg_ca_sound.c`): the rules. Gun takes alternate with N10's random skip; the
  player's own guns, blasts, body sounds and beeps play at the listener (N10 has no panner for
  them); enemy voices are placed on the enemy and keep N10's limits (31 m, 3 at once, 5 with
  deaths, 95 ms apart, per-enemy 0.42 / 0.28 / 1.8 s, idle 1.15 s, hurt 0.1 s), with idle voices every
  4.4-9.7 s per enemy and the hurt voice on 52 % of hits; attack voices start with the wind-up
  (entity `time`), hurt with `time2`, death with `EV_CA_ENEMY_DEATH`.
- **Events from the shared pmove** (predicted): `EV_FOOTSTEP` (Quake III's bob-cycle rule is N10's;
  eventParm 1 = sprinting), `EV_CA_LAND` (faster than 1.9 m/s, speed x 10), `EV_CA_SLIDE`,
  `EV_CA_SWITCH`, `EV_CA_RELOAD`; the damage beep follows `ps.damageEvent`.
- **Soundtrack**: N10's ASHEN DRIVE as Quake III's background track (`music/ashen_drive.ogg`) when
  the map sets no music; `s_musicvolume` = N10 music x master.
- Not reproduced: the per-play random pitch (+-1.4 %), N10's compressor on the weapon and enemy
  buses, N10's distance curve and pan law for voices (Quake III attenuates linearly from 2.5 m to
  41.6 m and pans fully), separate weapon / enemy volume settings (baked at the defaults).
  Every Quake III sound that is not shipped gets the silent `sound/feedback/hit.wav`.

## Medkits (stage D, part 7)

qagame asks the C++ side for N10's medkit spots (`CA_HordePickups`: N10's `CollisionWorld::pickups()`
on the map's N10 data) and spawns one `ET_CA_PICKUP` entity per spot. After every Pmove of a player,
`G_CA_MedkitTouch` applies N10's rule along the move (sampled every 4 units): +40 health up to 100,
the medkit away for 13 s (`SVF_NOCLIENT`, so clients stop drawing it), `EV_CA_MEDKIT` for the beep.
cgame draws the glow sprite and the billboard (`CG_CA_Pickup`).

## Networking (Q3E-E)

- Server-authoritative game on the engine's client / server model; up to 4 players; a "host"
  runs a listen server.
- Enemies, players, projectiles and pickups as `entityState_t` entities in delta snapshots;
  cosmetic effects as events (no entity per effect).
- Audit before E: snapshot entity count against `MAX_SNAPSHOT_ENTITIES` 256 (SOURCE_AUDIT §2).
