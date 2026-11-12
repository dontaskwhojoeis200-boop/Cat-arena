# Cat Arena: Feral Claw — native rewrite, N5.1 (dynamic lights)

Complete source tree after N5.1. Start with `docs/PROGRESS.md`; this step is described in
`docs/N5_1_DYNAMIC_LIGHTS.md`, the weapons milestone in `docs/N5_WEAPONS.md`.

What changed since N5
- Muzzle flashes, projectiles and explosions light floors and walls as in the legacy game.
- Medkits are drawn (billboard + glow); N5 drew only the glow.

How it was checked
- The legacy's choice of lights was recorded for every tick of the 29 tapes and is reproduced
  exactly. Frames during effects went from a median of 27.8 dB to 57.1 dB against legacy pictures
  (17 frames without practice targets; minimum 41 dB). Weapon views are at 51.5-52.5 dB.
- 22 GPU-free and 40 desktop tests in seven build configurations; N2 golden frames unchanged.

Still open (see docs/KNOWN_LIMITATIONS.md): the HUD is a native layout; no enemies (N6), no sound
(N7); MD3 prop models, portal / mirror feeds and PVS from N4; Windows and Android not built here.
