# N5 — weapons, projectiles, damage, viewmodel, effects, HUD (2026-11-10)

> Update, N5.1: item 1 of section 5 (dynamic lights on the map) is closed and the medkit billboard
> was added; see docs/N5_1_DYNAMIC_LIGHTS.md. The numbers below are the N5 measurements.

Scope of the phase prompt: all 16 source-defined weapons with their own ammo / cadence / reload /
hitscan / projectile rules, muzzle effects, recoil, impacts, splash, rocket and grenade jump,
first-person viewmodel; ammo / health / HUD and pickups connected; deterministic fixtures for
projectile travel, hitscan rays, splash falloff and self damage / impulse.

Status in one paragraph: the weapon simulation, the effect system and the viewmodel are ported and
reproduce the unmodified legacy game on 29 recorded tapes (18,220 ticks): every gameplay value is
bit-identical, the viewmodel camera agrees to 1e-16 and every effect quad the legacy builds is
built natively (same count, same vertex checksums). The 16 weapon models render 45-47 dB (PSNR,
whole frame) against legacy pictures. **Not done:** dynamic lights of the effects on map surfaces,
the legacy's HTML HUD layout (the native HUD shows the same data in its own layout), sounds, and
everything that needs enemies (N6). Details under "Not implemented / differences".

## 1. What the legacy does (source study)

All line numbers refer to `legacy/index.html` (unchanged, SHA-256 e3c914bc…0393).

| Topic | Legacy | Notes that matter for a port |
|---|---|---|
| Weapon table | `WEAPONS` 5140-5156 | 16 rows: damage, delay, clip, reload, pellets, spread, range; flags rocket / grenade / rift / rail / lightning. Plasma Repeater and Hellfire Carbine are hitscan. There is no reserve ammo: a magazine and an unlimited reload. |
| Firing | `fireWeapon` 12054 | Blocked while `now < cooldown` or reloading. Empty magazine starts a reload. Hitscan resolves on the firing tick; tracers are cosmetic. The mouse-down handler fires immediately, between ticks; while held, `update()` fires after the movement of the tick. |
| Shot origin | `firePosition` 11811 | Eye without cosmetic motion (`q3ShotOrigin`) + 0.24 m along the pellet direction. |
| Spread | 12079-12080 | Two random numbers per pellet: yaw ± spread, pitch ± 0.7 spread, pitch clamped to ±1.49. |
| World hit | `worldShotCollisionT` 11820, `levelSolidAt` 8443 | The segment is sampled every 0.19 m; a wall counts within radius + 0.095 m. Hit points are therefore up to one sample short of the surface. |
| Target hit | `shotSweep` 11829, `segmentBoxTime` 11572, `enemyHitBoxes` 11530 | Two boxes per enemy (body, wider and deeper head), three size classes. A level shot from the eye meets the head box first: a hit counts double. |
| Damage | `damageBot` 11585 | Head = 2x. A non-lethal hit draws one random number (hurt voice); a kill removes the enemy, adds a frag and rerolls the weapon in hand (`rerollOne`: a different random weapon, full magazine, 0.12 s cooldown). |
| Projectiles | 12066-12072, loops 12308-12343 | Rocket 26 m/s, 2.6 s fuse, radius 0.14; grenade 22 m/s + 1.5 m/s up, gravity 15, 2.3 s, radius 0.15; rift bolt 19 m/s, 2.6 s, radius 0.23. Three lists, updated in that order, each from last to first. A direct hit deals no extra damage: only the blast. |
| Blast | `explode` 12015 | Enemies: 95 x power, linear falloff over 3.35 x power m measured to the nearest hit box (always body damage). Player: `round(38 x power x falloff)`, at least 1, over 3.25 x power m to the player box; then launch `sqrt(2 g 1.8 x 2.25) x pct` upward and 8 x pct m/s away (pct = 1 inside 42 % of the range). Powers: rocket 1.15, grenade 1.08, rift 1.42. |
| Shotgun recoil | `applyShotgunRecoil` 11992 | Shotgun / Super / Quad: 4.7 / 5.7 / 6.7 m/s backwards on the ground; in the air 55 % of that plus a lift to at least 7.3 / 7.8 / 8.2 m/s (a mid-air jump). |
| Reload / swap | 12008 | Reload timer per weapon; swap cancels a reload and sets a 0.15 s cooldown. |
| Player damage | `takeDamage` 11912 | Ignored during 2.5 s spawn protection. The blast launch is applied even then. |
| Pickups | 12344, `worldPickupSpots` 9738 | Only medkits: +40 health up to 100 within 0.85 m and 1.2 m of height, 13 s cooldown. |
| Weapon models | `MESHY_ASSETS` 820, `buildGun` 14268 | 16 textured meshes (273-300 triangles, base / metal-rough / normal JPEG), all with material 300. |
| Viewmodel camera | `renderGun3D` 14516, `CLASSIC_VIEWMODEL`, `VIEWMODEL_MUZZLE_HEIGHT` | Depth is cleared, then the model is drawn with its own 60° camera placed so that the muzzle lands on a fixed screen position; Quake III weapon motion + recoil spring, raise on switch, reload dip. |
| Effects | `WFX_DEF` 10800, `WFX` 10818-11177, `FXR` 2638-2765, `FX_ATLAS` 2433 | Local entities, marks, tracers, rail trails, lightning, projectile sprites, muzzle flash; one procedural 8 x 8 tile atlas; three batches (marks, depth-sorted main, weapon space). |
| HUD | DOM, 627 and `updateHud` 14620 | HTML / CSS, drawn by the browser. |

## 2. Native design

```
physics::PlayerController (N3)
        │ owned by
combat::Arena          native/include/cat/combat.hpp   src/combat.cpp     (lib cat_arena_physics)
        │ events of the last tick (shots, blasts, damage, fire context)
fx::WeaponFx           native/include/cat/weapon_fx.hpp src/weapon_fx.cpp  (lib cat_arena_fx)
hud::Canvas / build    native/include/cat/hud.hpp       src/hud.cpp        (lib cat_arena_fx)
        │ plain vertex data (cat::Overlay)
Renderer::draw(view, overlay)                           src/renderer.cpp   shaders fx / gun / hud / solid
```

* `combat::Arena` is the simulation: weapon table, loadout, magazines, timers, three projectile
  lists, targets, pickups, health, respawn. One `step(TickInput)` is one 1/120 s tick in the legacy
  order: key transitions (movement, reload, swap, fire press), look, timers, movement, held fire,
  reload timer, rockets, grenades, rift bolts, pickups. It uses one seeded random stream
  (mulberry32) for gameplay only.
* `fx::WeaponFx` is presentation only. It reads `Arena::events()` after each tick, owns a second
  random stream, and produces quads plus the weapon camera. Running or not running it leaves the
  simulation bit-identical (tested).
* The renderer receives `cat::Overlay` (pointers to vertex arrays, two cameras, HUD triangles) and
  knows nothing about weapons. `draw(view)` without an overlay renders exactly the N4 picture.
* Each weapon keeps its own table row and its own branch in `Arena::fire` (rocket, grenade, rift,
  hitscan with per-weapon pellets / spread / range, shotgun recoil for exactly the three
  shotguns). There is no generic weapon, and the weapon view uses the 16 dedicated weapon models.

### Tables generated from the legacy

`tools/n5/legacy_weapon_assets.cjs` reads the running legacy page and writes
`native/include/cat/generated/weapon_fx_tables.inc` (effect tile ids, the 16 `WFX_DEF` rows, muzzle
heights, viewmodel pose constants, model muzzle points with full double precision) and
`assets/n5` (16 `.gun` meshes, 48 JPEG files copied byte for byte out of the page's data URLs,
`fx-atlas.png` read back from the page's canvas). The gameplay weapon table in `combat.cpp` is
written by hand and is verified by the tapes below.

### Rendering

| Pass (in order, after the N4 map passes) | Pipeline state | Legacy counterpart |
|---|---|---|
| Practice targets | opaque, depth test + write | none (stand-in until N6) |
| Marks | blend ONE / ONE_MINUS_SRC_ALPHA, depth LEQUAL, no depth write, polygon offset -1 / -3 | `FXR.draw('mark')` |
| Main effects, back to front | same without offset | `FXR.draw('main')` |
| Depth clear | `vkCmdClearAttachments` | `gl.clear(DEPTH)` in `renderGun3D` |
| Weapon model | two-sided, depth LESS, own camera, three textures | main program `isGun==1 && vMat==300` |
| Muzzle flash | no depth test, weapon camera | `FXR.draw('gun')` |
| HUD | alpha blend, no depth | DOM |

Six new pipelines, built for both the dynamic-rendering path and
the Vulkan 1.2 render-pass path. Cameras travel as 80-byte push constants; effect and HUD vertices
are written into host-visible buffers every frame; textures use ordinary descriptor sets (so the
overlay works with and without push descriptors). `gun.frag` negates the y derivatives because
window y points down in Vulkan; without that the normal map would be mirrored.

## 3. Verification

### 3.1 Recording the legacy

`tools/n5/legacy_combat.cjs` runs the hash-checked legacy page in headless Chromium, holds its own
loop, and for every tick of a scripted input tape calls the legacy functions in the legacy order
(the same method as N3; the transcribed part of `update()` is in the tool's header). `Math.random`
is replaced by a seeded mulberry32 for the run. Targets are plain objects in the legacy `bots`
array, so the legacy hit boxes, `shotSweep`, `explode` and `damageBot` run on them unchanged.

Two fixtures result:

* `native/tests/fixtures/n5/helios_combat.bin` — 48 values per tick: player state, loadout,
  magazines, cooldown, reload, kick, flash timer, number of random draws, projectile counts and
  position sums, pellet end points, detonations, target health, frags, pickup cooldowns.
* `native/tests/fixtures/n5/helios_weapon_fx.bin` — 40 values per tick of the legacy effect code
  running with its own random stream: the camera, glow and aspect `renderGun3D` sends to the
  shader, quad counts and checksums (positions, texture attributes, colours, sort keys) of the
  main, mark and weapon batches after `WFX.build`, dynamic lights, the recoil spring, the
  world-space muzzle.

The effect recording does not disturb the gameplay recording: the combat fixture written with and
without effects, and with and without taking pictures, is byte-identical (checked with `cmp`).

29 tapes, 18,220 ticks (151.8 s): each of the 16 weapons (sustained fire at three target classes,
magazine exhaustion, reload key, swap, second weapon), kills with rerolls, rocket and grenade
jump, rocket at the feet while moving, splash falloff rows for rocket / grenade / rift, grenade
arcs at five pitches, shotgun recoil on the ground and in the air for the three shotguns,
headshots, a medkit picked up twice, firing while moving / crouching / sliding.

### 3.2 Results

| Check | Result |
|---|---|
| Combat replay, 48 values x 18,220 ticks | all ticks bit-identical; 630 pellets, 32 detonations, 4 frags, 1,614 random draws; all 16 weapons fired |
| Effect replay, 40 values x 18,220 ticks | viewmodel camera, glow, recoil, muzzle: worst difference 1.1e-16; quad counts, light counts and all batch checksums equal; 17,578 cosmetic random draws equal; 814,887 quads; 18,186 ticks bit-identical in every value |
| Same native bits in 7 builds (GCC / clang, Debug / Release / sanitizers) | one hash per fixture, see n5-evidence/hash-by-build.txt |
| Combat unit tests (synthetic room) | 18,372 checks |
| Effect unit tests | 523 checks |
| HUD unit tests | 29,702 checks |
| Weapon views vs legacy, 16 weapons, 1280x720 | PSNR 45.4-47.5 dB (whole frame, lavapipe both sides) |
| Frames during effects vs legacy (24 frames of 5 tapes) | effects and weapon agree; frames with a flash or blast differ in the lighting of the floor and walls (dynamic lights, not implemented); frames with targets differ where the legacy draws an enemy model and the native a box |
| Validation layer, both Vulkan paths, window and offscreen | 0 warnings, 0 errors |

What the fixtures required (the phase prompt's list) and where it is covered:

| Fixture | Legacy tape | Native unit test |
|---|---|---|
| Projectile travel | `weapon_02/04/10`, `grenade_arcs`, `splash_falloff_*` | straight flight at 26 / 19 m/s, fuse 312 ticks, grenade arc against the closed form |
| Hitscan rays | all hitscan weapon tapes, `headshots_rail_handcannon`, `fire_while_moving` | box / segment tests, head vs body, wall stop, range end, nearest target, spread cone |
| Splash falloff | `splash_falloff_rocket/grenade/rift` (7 targets each) | linear falloff at 7 distances x 3 powers, exact values |
| Self damage / impulse | `rocket_and_grenade_jump`, `rocket_at_feet_moving`, `weapon_10` | 43 / 41 / 54 damage at the feet, launch 12.07 m/s, sideways push, edge of the radius, crouch box, death, spawn protection |

### 3.3 Things the comparison found

* The effect code must see the view as it is *after* the volley: the shotgun recoil changes the
  player's velocity, and the legacy view camera leans with velocity. Taking the camera before the
  recoil gave 0.1-0.18 m checksum differences on exactly the three shotguns.
* After a kill inside a volley the muzzle point belongs to the *new* weapon (the reroll happens
  before `WFX.fire`). The native passes "weapon in hand after the volley" with the shot.
* The explosion is drawn at `max(0.05, z)` while damage uses the real height.
* Unit tests written from expectation and corrected to the legacy behaviour: a direct rocket hit
  is a blast from 0.14 m, not full damage; spawn protection blocks damage but not the launch; the
  first shot of a held button comes one tick early (it is fired by the press, before the tick).
* A key pressed and released between two simulation ticks was lost by the window (found by the
  input self-test at lavapipe frame rates). The window now latches such taps for one tick, which
  is what the legacy's event handlers do.

## 4. Preserved legacy behaviour worth knowing

* Level shots at a standing enemy are headshots (the head box is wider and starts at 1.06 m).
* World hits are sampled every 0.19 m; thin geometry between samples is not hit.
* Plasma Repeater and Hellfire Carbine are hitscan with slow cosmetic projectiles.
* No ammo pickups and no reserve: every weapon reloads without limit.
* A frag changes the weapon in hand; the other slot keeps its weapon and magazine.
* Splash damage to enemies is never doubled; splash damage to the player is rounded.
* Blast impulse ignores spawn protection.

## 5. Not implemented / differences

1. **Dynamic lights on the map.** The effect code collects the lights (count and values equal to
   the legacy), the world and material shaders do not use them yet. Visible as missing light on
   floors and walls around muzzle flashes, rockets and explosions.
2. **HUD layout.** The legacy HUD is HTML. The native HUD shows health, magazine / clip or the
   reload state, weapon names, eliminations / deaths, crosshair, hit marker, damage flash and
   notices with a built-in 5 x 7 font in its own layout. Damage numbers, the incoming-hit
   indicator and the wave display are not there.
3. **Enemies.** Targets are static boxes with the legacy hit boxes. Enemy models, hurt and death
   effects (gibs, blood on a kill), enemy projectiles and burn damage are N6. In comparison frames
   the legacy draws its enemy model where the native draws a box.
4. **Respawn.** The death path (health 0, respawn at a spawn point, new loadout, 2.5 s protection)
   exists and is unit-tested, but no legacy tape covers it, and the spawn point is chosen
   uniformly instead of with `makeRandomSpawn` (which weighs enemy positions).
5. **Sound.** No weapon, explosion or pickup sounds (N7).
6. **Other maps.** The tapes are recorded on Helios Crucible. The code is map independent and
   runs on all four maps (window map cycle, 26 / 33 / 38 / 5 medkits found), but combat on the
   other three maps is not compared with the legacy.
7. **Settings.** Effect detail, marks, brass, blood and rail-trail time are fixed at the legacy
   defaults; the viewmodel calibration sliders (angle, size, height) are fixed at their defaults.
8. **Texture decoding.** The weapon JPEGs are decoded by stb_image, in the browser by Chromium;
   the decoders differ by small amounts.
9. Co-op parts of the same functions (remote players' shots and damage, shared pickups) are N8.
10. Open from N4, unchanged: MD3 prop models, portal / mirror / monitor feeds, PVS.

## 6. Running

```sh
./build-core/cat_arena_combat_tests                      # unit tests
./build-core/cat_arena_combat_tests assets/n1 native/tests/fixtures/n5/helios_combat.bin
./build-core/cat_arena_fx_tests assets/n1 native/tests/fixtures/n5/helios_combat.bin native/tests/fixtures/n5/helios_weapon_fx.bin

./build-desktop/cat_arena                                # left mouse fire, R reload, Q / Tab swap
./build-desktop/cat_arena --list-weapons
./build-desktop/cat_arena --weapons 4,7 --seed 5
./build-desktop/cat_arena --offscreen 1280x720 --weapons 4,7 --walk "-:20,fire:3,-:120,swap:2,-:40,fire:3,-:60" --capture-every 10 --capture-dir out/
./build-desktop/cat_arena --offscreen 1280x720 --no-hud --combat-fixture native/tests/fixtures/n5/helios_combat.bin \
    --scenario rocket_at_feet_moving --capture-ticks 59,62,70 --capture-dir out/
```

Re-creating the references (tooling only; Node >= 22 with Playwright Chromium):

```sh
node tools/n5/legacy_weapon_assets.cjs
node tools/n5/legacy_combat.cjs --out native/tests/fixtures/n5/helios_combat.bin \
     --fx-out native/tests/fixtures/n5/helios_weapon_fx.bin --report combat.json \
     --views-dir views/ --frames "rocket_at_feet_moving:59,62,70" --frames-dir frames/
```
