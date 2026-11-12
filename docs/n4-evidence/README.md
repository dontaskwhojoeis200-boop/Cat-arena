# N4 evidence (2026-11-10)

- *-configure/-build/-tests.txt: clean builds and CTest of seven configurations.
- physics_legacy_trajectories_<map>.txt, navigation_<map>.txt: stdout of the per-map tests (desktop Debug build).
- trajectory-hash-by-build.txt: native trajectory hash per map and build.
- trajectories_capture_<map>.json, nav_capture_<map>.json: reports of the legacy capture tools.
- frames/: per map the poses, the legacy capture report, the native report and comparison.json;
  summary.md; path-identity.txt (automatic path vs Vulkan 1.2 baseline);
  <map>_legacy_left_native_right.jpg side-by-side sheets (downscaled; statistics were computed on
  the 1280x720 originals).
- window/: in-window map cycle through all four maps on two Vulkan paths.
- n2-golden-check.txt, source-integrity.txt, fresh-extract-*.
