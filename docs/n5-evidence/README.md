# N5 evidence (2026-11-10)

- *-configure / -build / -tests.txt: clean builds and CTest of seven configurations.
- combat_legacy_tapes.txt, weapon_fx_legacy_tapes.txt, unit_tests.txt: stdout of the N5 tests
  (desktop Debug build).
- hash-by-build.txt: native state hash of both replays and of the N3 / N4 trajectory replays per build.
- legacy_combat_capture.json / .log: report and log of the legacy recorder (29 tapes, pictures).
- capture-identity.txt: the fixtures do not change when effects run or pictures are taken.
- frames/: weapon_views_* — 16 weapons, legacy left, native right (downscaled sheets; the
  statistics in weapon_views_comparison.json were computed on the 1280x720 originals);
  effect_frames_* — ten of the 24 compared frames during effects (order in effect_frames_order.txt),
  statistics for all 24 in effect_frames_comparison.json. Low values there are explained in
  docs/N5_WEAPONS.md (dynamic lights, enemy model vs box).
- demo/: a scripted offscreen session with HUD and practice targets (command.txt, walk.json, frames).
- window/: input self-test with fire / reload / swap on two Vulkan paths; map cycle + resize.
- n2-golden-check.txt, source-integrity.txt, environment.json, fresh-extract-*.
