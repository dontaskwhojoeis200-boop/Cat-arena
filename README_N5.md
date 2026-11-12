# Cat Arena: Feral Claw — native rewrite, N5 (weapons)

This package is the complete source tree after milestone N5. Start with `docs/PROGRESS.md`;
the N5 report is `docs/N5_WEAPONS.md`.

What N5 adds
- All 16 weapons with their own magazine, cadence, reload, pellets, spread and range; hitscan
  weapons, rockets, grenades and rift bolts; splash damage, rocket / grenade jump, shotgun recoil;
  headshots; the frag-rerolls-your-weapon rule; medkits; death and respawn.
- First-person weapon models (the legacy's 16 meshes and textures), viewmodel camera, recoil,
  muzzle flash, tracers, rail trail, lightning, projectile sprites, impacts, explosions, marks.
- A HUD (health, magazine, weapons, score, crosshair, hit marker) and practice targets.

How it was checked
- The unmodified legacy game was recorded on 29 scripted tapes (18,220 ticks). The native
  simulation reproduces every recorded gameplay value bit for bit, and the native effect system
  builds the same quads and the same weapon camera as the legacy effect code.
- 16 weapon views compared with pictures of the legacy: 45-47 dB.
- 22 GPU-free tests and 37 desktop tests in seven build configurations.

What is not done (see docs/KNOWN_LIMITATIONS.md)
- Flashes and explosions do not light the map yet.
- The HUD is a native layout, not the legacy's HTML HUD.
- No enemies (N6), no sound (N7). Windows and Android were not built in this environment.
- Open from N4: MD3 prop models, portal / mirror feeds, PVS.

Build and run: `docs/BUILD.md` (section N5 at the end).
Nothing under `legacy/` or `reference/` was modified.
