# Cat Arena: Feral Claw — native rewrite, N6 (enemies)

Complete source tree after N6. Start with `docs/PROGRESS.md`; this milestone is described in
`docs/N6_ENEMIES.md` (log: `docs/DEVELOPMENT_LOG_N6.md`, changes: `docs/N6_DIFF.md`).

What changed since N5.1
- Waves of the legacy's 20 enemy types are the default game: spawning, scaling, 30 Hz AI, routes on
  the stacked navigation graph, flyers, ranged and melee attacks, enemy projectiles, burning, the
  player's death and the wave restart.
- Creatures are drawn with their models, textures, skeletal poses, attack and pain animation, death
  animation and gibs, lit by the map's light volume and dynamic lights, with blob or projected
  shadows and the "final five" outline through walls. Their projectiles, casting glows, melee arcs,
  impacts and blood are drawn too.
- Marine Cats (other players; the local one only in the third-person debug view) from the
  game's model, verified against the shipped GLB, with joints and hit parts per vertex.
- HUD wave box, "last five" banner and wave announcements.

How it was checked
- 24 tapes recorded from the unmodified legacy game on all four maps (48,000 ticks): simulation
  within 1.5e-14, poses within 1.2e-13, cosmetic random draws equal every tick.
- 21 frames of those tapes and 5 staged scenes against legacy pictures: 42.5-63.6 dB and
  48.5-52.8 dB. Unit tests: 8,759 + 83,061 checks.
- 32 GPU-free and 60 desktop tests in seven build configurations (docs/TEST_MATRIX.md).

Still open (docs/KNOWN_LIMITATIONS.md): no sound (N7); the HUD is a native layout; the legacy's
1.8 ms wall-clock cut of route searches is not reproduced on purpose; MD3 props, portal / mirror
feeds and PVS from N4; Windows and Android not built here.
