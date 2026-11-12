# N3 evidence (2026-11-10)

- *-configure/-build/-tests.txt: clean builds and CTest of seven configurations (core GCC Debug and
  Release, core clang Release, desktop GCC Debug and Release, desktop ASan+UBSan+LSan, desktop clang).
- physics_units.txt, physics_legacy_trajectories.txt, physics_all_maps.txt: stdout of the physics tests
  (desktop Debug build). One line per legacy scenario with the worst difference and contact counters.
- trajectory-hash-by-build.txt: bit-exact tick count, worst difference and native state hash per build.
- legacy_trajectories_capture.json / .log: report of tools/n3/legacy_trajectories.cjs (what was run,
  what page state was set, scenario list, legacy contact counters, fixture hash).
- walk/: headless first-person walk (walk.json with per-frame state, walk_sheet.png contact sheet).
- window/: SDL input self-test in window mode on the automatic and the Vulkan 1.2 baseline path.
- n2-golden-check.txt: the N2 preview frame is still byte-identical after N3.
- source-integrity.txt: legacy/, reference/ and assets/ against the N2.1 manifest.
- fresh-extract-*: configure/build/test of the extracted candidate ZIP.
