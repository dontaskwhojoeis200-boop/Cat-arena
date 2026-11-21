# Stage D review — 2026-11-21

Stage D moved Cat Arena's content and gameplay from N10 onto Quake3e: the four maps, N10's
navigation, the 16 weapons, the 20 enemy types with their animation, the sounds and soundtrack and
the medkits (parts 1-7, `TEST_MATRIX.md`). This review rebuilt everything from a clean clone,
checked that the converters reproduce the committed data, ran every stage D test on one build,
measured where the stock Quake III player box can go compared with N10's player, and lists what
still differs from N10, with the decisions that are the owner's.

Status words as everywhere in `docs/q3e`: Implemented / Compiled / Executed / Tested / Unverified /
Blocked.

## 1. Clean build and reproducibility

| Check | Result | Status |
|---|---|---|
| Engine from a clean clone (commit 62ac2d7; the engine source is unchanged in e6d303f), Linux, `make USE_SDL=1 USE_CURL=0` | builds, 0 warnings; the Vulkan renderer byte-identical to the binary used for stage D's tests, the client and server differ only by build-time data | Compiled |
| Game modules (qagame, cgame, ui) from a clean clone, Linux Release | builds; 2 warnings, both in unmodified ioquake3 code (`ai_dmnet.c` empty format string, `cg_weapons.c` MatrixMultiply bounds) | Compiled |
| Converters re-run on the clean clone: `make_base.py`, `convert_weapons.py`, `convert_enemies.py`, `convert_sounds.py`, `n10map2q3.py` on the four maps | every committed file they write is reproduced byte for byte (git sees no change) | Tested |
| q3map2 re-run on the test arena and The Arsenal | geometry, shaders, lightmaps, visibility identical; the light grid is not (91 of 3,125 cells in the test arena, 1,640 of 12,710 in The Arsenal, by up to 243 of 255 in a channel): q3map2's threaded grid pass. A single-threaded light pass gives identical lumps (padding bytes between lumps still differ) | Tested |

The light grid lights the models (creatures, viewmodel, props), not the map surfaces. Decision D-R1.

## 2. Every stage D test on one build

One package, built from commit e6d303f (engine from the clean clone, game modules and `pak0.pk3`
from e6d303f), Linux, lavapipe on Xvfb, run by `tools/q3e/run_stage_d.py` (about 95 minutes on two
cores). The waves and medkit runs are from the last pass, after their scripts were made robust (the
build was not changed). Logs and checks: `evidence/stage-d/review/`.

| Test | Result | Status |
|---|---|---|
| `ca_selftest`, dedicated server: test arena, Reactor Bastion, Helios Crucible, Bastion Keep, The Arsenal | ALL PASS on each (29 / 40 / 38 / 38 / 40 checks: waves, the 20 types, N10 routes, flyers, the 16 weapons, the medkit rule) | Tested |
| Stage C gameplay run (movement, sprint, slide, reload, fiends, waves) | ALL PASS (11) | Tested |
| Weapons run (16 weapons, magazines and rates, frag rerolls, blasts, combat) | ALL PASS: 40 frags with all 16 weapons, waves 1-3 cleared | Tested |
| Enemy lineup (20 types, pictures) | all 20 spawned | Tested |
| Waves, Reactor Bastion | 8 of 9: no melee enemy reached the player in this run (fireball, shot, spike and void orb did); ALL PASS in the run before it on the same build | Tested (run to run variance) |
| Waves, Helios Crucible / Bastion Keep / The Arsenal | ALL PASS on each (spawns of 16+ types with the three flyers, kills, every attack kind, flyer attacks, damage with god mode off, waves 3+) | Tested |
| Map test against N10's data | Reactor 50 floors / 20 ramps / 4 pads; Helios 54 / 8 / 2; Bastion 45 / 10 / 2; The Arsenal 12 / 99 / 1: all reached (two Arsenal spiral steps are shorter than the player box, which starts on the next step) | Tested |
| Sound (disk audio driver) | ALL PASS: output peak 0.1926 for 0.1926 expected, soundtrack at 0.322, 282 shots of the 16 guns, 75 voices of all kinds and families within N10's limits, nothing clipped | Tested |
| Medkits on Reactor Bastion | ALL PASS (9): spawn at 100, +40, back after 13,000 ms, an 8 m fall costs nothing | Tested |
| Pictures from six spawn spots of each map | 24 pictures (`review-views.jpg`) | Executed |
| Windows CI (MSVC build, the five maps on the dedicated server) | see `TEST_MATRIX.md`, stage D review | — |

The scripted waves runs depend on where the enemies go, so a check can miss in one run and pass in
the next. Across the review's runs on this build every check passed at least once on every map; the
only misses were "no melee hit" and "no hit while god mode was off", when the wave happened to
be down to enemies far away or flyers out of reach.

## 3. Where the Quake III player can go

Stage C kept stock Quake III movement (owner decision D1), and with it the stock player box: 30 x
30 x 56 units = 0.94 x 0.94 x 1.75 m. N10's player is a 0.60 m wide, 1.80 m tall capsule. `ca_fitsurvey`
(dedicated server command, added in this review) checks both on every N10 navigation node (1 m grid
over N10's floors) and floods N10's own links for its standard walker (0.32 m radius) from the map's
spawn spots, once with all links and once keeping the links the Quake III box can sweep (steps up to
18 units).

| Map | Nodes | N10 player fits | Quake III box fits | Reached by N10's player | by the Quake III box | Only N10: next to the Quake III reach (≤ 1 m) | 1-2 m away | further |
|---|---|---|---|---|---|---|---|---|
| Reactor Bastion | 6,182 | 6,120 | 5,317 | 6,074 | 5,610 | 450 | 8 | 6 |
| Helios Crucible | 5,270 | 5,246 | 4,753 | 5,240 | 5,026 | 214 | 0 | 0 |
| Bastion Keep | 6,359 | 6,359 | 5,934 | 6,358 | 6,230 | 128 | 0 | 0 |
| The Arsenal | 2,535 | 2,530 | 2,185 | 2,520 | 2,286 | 228 | 6 | 0 |

Almost everything the wider box loses is the strip along walls and edges it cannot get as close to
(0.47 m from its centre instead of 0.30 m). Areas it cannot enter at all: four small corner pockets
on Reactor Bastion's upper ring (at ±832, ±832, 2.2 m from the nearest point the box reaches) and a
few nodes on The Arsenal within 2 m of its reach. No room, corridor or level is cut off on any map.
Jump height (1.42 m stock, N10 1.63 m) is not part of this count; the map test reaches every N10
ramp, floor and jump pad. Decision D-R2.

## 4. Fixed in this review

- **Fall damage**: stock Quake III took 5 or 10 health on hard landings; N10 has none (`player.cpp`:
  "No fall damage in the legacy game"). Removed; the medkit run now checks an 8 m fall.
- **Test robustness**: the waves runs use spawn spots open to the sky on Reactor Bastion too (a
  skyjuggernaut kept rising and sinking in place above the old low-level spot, which left the
  god-mode-off window without attackers), and the full set runs from one script
  (`tools/q3e/run_stage_d.py`).

## 5. What still differs from N10

Found in this review:
- **Light on the maps**: q3map2's lightmaps have N10's mean brightness (the calibration of part 5)
  but not its spread. N10's are soft: the 10th to 90th percentile of the lit luxels lies between 36
  and 72 (Reactor Bastion; the other maps alike). q3map2's run from 5 to 146: shadows near black,
  lit spots much brighter. In the pictures (`review-views.jpg`) walls facing away from the lights
  are black where N10 shows them dim. Decision D-R5.

| Map | N10 lightmap, percentiles 10 / 25 / 50 / 75 / 90 (Quake III units) | q3map2 | Mean N10 / q3map2 |
|---|---|---|---|
| Reactor Bastion | 36 / 41 / 49 / 59 / 72 | 5 / 6 / 30 / 68 / 146 | 52 / 51 |
| Helios Crucible | 35 / 39 / 46 / 56 / 64 | 5 / 7 / 22 / 62 / 163 | 49 / 47 |
| Bastion Keep | 35 / 38 / 47 / 57 / 65 | 5 / 5 / 18 / 65 / 137 | 49 / 47 |
| The Arsenal | 38 / 44 / 54 / 72 / 98 | 5 / 5 / 28 / 85 / 191 | 61 / 60 |

By decision (owner, stage A):
- **Movement** is stock Quake III (D1) plus N10's sprint, crouch and slide: Quake III air control,
  acceleration and jump (1.42 m against N10's 1.63 m), and the wider player box (section 3).

By construction (Quake III has no equivalent, or N10's way would mean rewriting the renderer or the
sound mixer):
- **Creatures** are posed on the CPU (N10's animation code, ported) and drawn as polygons lit by the
  map's light grid: no normal or metal / roughness maps, no dynamic lights on them, no blob shadows.
- **Sounds** have N10's levels, voice tones and filters baked in; per-play random pitch (±1.4 %),
  the compressor on the weapon and enemy buses, N10's distance curve and pan law for voices and
  separate weapon / enemy volume settings are not reproduced (`Q3E_ARCHITECTURE.md`, Sound).
- **Medkits** and creatures get no dynamic lights or fog.
- **Hit boxes**: one box per enemy (the union of N10's two), head hits are the top third of it.
- **Frag reroll** is decided by the server: for one snapshot the client can still predict the old
  weapon.

Not ported yet (each listed where it was found):
- N10's distance fog (a renderer setting in N10) on the maps.
- Enemies: the "final five" outline, casting glows, melee arcs, N10's gib meshes (gibs are sprites);
  the route scheduler is the simplified one of part 2; Marine Cats (co-op, stage E).
- Weapons: projectiles that reach the sky vanish (N10 explodes them at the end of their life);
  effects approximate N10's WFX sprites.
- N10's HUD (legacy layout, damage numbers, hit indicator, editor), its menus and the 65 settings:
  only the stage B menu (main / in-game menu, loading screen) exists. FEATURE_PARITY plans them for
  D/F; they are not part of stage D's seven parts. Decision D-R4.
- Crash reports (N9) in the Quake3e build.

Observed, the same as N10's own code:
- N10's flyers steer greedily toward a point 2.45 m above the nearest player and do not search a
  route. A player under a roof, or a flyer in a pocket of the map, leaves it hovering (or rising and
  sinking) out of reach, and the wave waits for it. The scripted tests stand on spots open to the
  sky and turn god mode off early in the run for this reason. Decision D-R3.

## 6. Not verified yet

- Nobody has played the Quake3e build: no Windows PC with a GPU, no mouse, no real audio device.
  Every test ran on Linux with lavapipe (software Vulkan) or on the Windows CI runner without a GPU.
- Frame time and memory on the owner's PC (Pentium G2020 + GTX 660): not measured (stage F).
- The maps were compared with N10's pictures by eye only.

## 7. Decisions for the owner

- **D-R1 Reproducible lighting.** q3map2's threaded light pass gives a slightly different light grid
  each build. (a) Keep it (fast; the grid lights the models — creatures, viewmodel, props — not the map). (b) Light single-threaded in
  `build_maps.sh`: identical BSPs from the same sources, about twice the light time. Recommended: (b),
  done when the maps are next rebuilt (with D-R5).
- **D-R2 Player box.** (a) Keep the stock Quake III box (owner decision D1 spirit; loses the strip
  along walls and four corner pockets on Reactor Bastion). (b) An N10-sized box (19 x 19 x 58 units):
  N10's reach, but a smaller target for enemy shots than stock Quake III. Recommended: (a).
- **D-R3 Flyers stuck out of reach.** (a) Keep N10's steering as it is. (b) Add a rescue: a flyer that
  has not got closer to its target for some seconds follows the ground route at flying height (a
  deviation from N10). Recommended: (a) until it is seen in real play; (b) is small if it is.
- **D-R4 HUD, menus, settings.** (a) Port N10's HUD and menus with its settings now, as stage D
  part 8, so the first real play test looks and plays like Cat Arena. (b) Go to stage E (multiplayer)
  and leave them to stage F. Recommended: (a).
- **D-R5 Light on the maps.** (a) Keep q3map2's contrast (closer to Quake III Arena's look, the art
  direction since 2026-11-16). (b) Match N10's soft light: ambient light in the light pass, the
  scale loop matching the spread as well as the mean, the four maps rebuilt. Recommended: (b), since
  the maps were converted to look like N10.
- **Before a release** (no action now): the asset rights listed in `LICENSING.md` (the weapon emblem,
  the Arsenal and Bastion kit textures and models — N10's data names the Bastion Keep kit as "the
  40-piece Meshy kit", generated with that commercial service, whose terms apply — the sounds and
  the soundtrack), and the prebuilt libcurl still linked in the Windows build (stage F).
