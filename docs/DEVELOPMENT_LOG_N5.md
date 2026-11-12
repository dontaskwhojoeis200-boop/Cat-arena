# Development log — N5 (2026-11-10)

Order of work, with what each step produced and what went wrong.

1. **Source study.** Read `WEAPONS`, `fireWeapon`, `firePosition`, `shotSweep`,
   `worldShotCollisionT`, `levelSolidAt`, hit boxes, `damageBot`, `rerollOne`, `explode`,
   `applyShotgunRecoil`, the projectile and pickup loops of `update()` and `takeDamage`. Later the
   presentation side: `MESHY_ASSETS`, `buildGun`, `renderGun3D`, the gun branch of the main fragment
   shader, `WFX_DEF`, `WFX`, `FXR`, `FX_ATLAS`.
2. **Legacy recorder** `tools/n5/legacy_combat.cjs`: 29 tapes, 48 values per tick, seeded
   `Math.random`, static targets in the legacy `bots` array. Fixture `helios_combat.bin`.
3. **World shot queries** added to `CollisionWorld`: `solid_at`, `shot_collision_t`, `pickups()`
   (baked pickups filtered like `worldPickupSpots`). `js_hypot3` shared with navigation.
4. **`combat::Arena`** written from the recorded rules. First replay: 28 of 29 tapes bit-identical.
   The one difference was the explosion height: the legacy *draws* the explosion at
   `max(0.05, z)`; the recorder had captured the drawn value. Damage uses the true height.
   Added `BlastEvent::fx_z()`; all 18,220 ticks identical.
5. **Combat unit tests** on a synthetic room. Six expectations of mine were wrong and were changed
   to the legacy behaviour after checking the source: magazine total (arithmetic slip), first shot
   one tick early on a press, rocket lifetime test left the room, a direct rocket hit is a blast
   from 0.14 m, rocket-jump apex bounds, spawn protection does not block the launch.
6. **Weapon assets** `tools/n5/legacy_weapon_assets.cjs`: 16 meshes, 48 JPEG textures, the effect
   atlas, and generated C++ tables. All 16 weapons turned out to be data meshes with one material
   (300), so the hand-built gun generators in `buildGun` are dead code for the shipped game and
   were not ported.
7. **`fx::WeaponFx`** ported from `WFX` / `FXR` / `renderGun3D` with its own random stream.
8. **Effect recorder**: `--fx-out` added to the combat tool. The effect code runs under a second
   seeded generator; the camera uniforms of `renderGun3D` are captured by wrapping `gl.uniform3f` /
   `gl.uniform1f` for the duration of the call. First attempt crashed the browser tab after eight
   tapes (hundreds of unpresented draws queued per tape); the draw, clear and upload calls are now
   skipped during the capture. The combat fixture written by this run is byte-identical to the
   one from step 2.
9. First effect replay: everything equal except the three shotguns (0.06-0.18 in a position
   checksum, on the firing tick). Cause: I took the view camera at the start of `fire()`; the
   legacy calls `WFX.fire` after `applyShotgunRecoil`, and the view leans with velocity. Moved the
   capture to the end of the volley: all tapes pass, checksums exact.
10. **Renderer overlay**: `cat::Overlay`, six pipelines, shaders `fx`, `gun`, `hud`, `solid`,
    `overlay.glsl`. `gun.frag` needed negated y derivatives (Vulkan window y is down).
11. **HUD** with a built-in 5 x 7 font; practice targets as boxes; `Session` in `main.cpp`.
12. First picture: weapon, muzzle flash and HUD present; two HUD texts overlapped (moved the
    target count under the map title).
13. **Pictures from the legacy** (`--views-dir`, `--frames`): 16 weapon views and 24 frames of
    five tapes. Weapon views 45.4-47.5 dB, except the railgun at 40.3 dB: I had set the legacy
    rail charge to "full" for the pictures while the native starts at zero like a new game. Reset
    the tool to the game-start value: 46.1 dB. Effect frames show the missing dynamic lights and
    the enemy model / box difference; both are listed as not implemented.
14. **Window**: fire, reload and swap bound; the input self-test extended. It failed: reload and
    swap were pressed and released within one rendered frame and never reached a tick. The window
    now latches taps for one tick. Default seed in the window now differs per run and per map
    change (it had rolled the same pair every time).
15. Regression matrix, sanitizers, fresh-extraction check, packaging.

Not done in this milestone, by decision: dynamic lights on map surfaces (most surfaces go through
the multi-stage material shader, which needs its own light pass), the DOM HUD layout, sounds.
