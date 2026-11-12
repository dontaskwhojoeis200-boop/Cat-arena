# N6.5 evidence (2026-11-11 / 12)

Windows (GitHub Actions, windows-2022, MSVC 19.44) — windows-ci/:
- run1-43164340554-job.log: first run, MSVC error C2039 in main.cpp (SDL_main.h).
- run2-43164827201-job.log: build, package and upload passed; 32/33 tests (physics_legacy_trajectories 1.30e-9).
- run3-43169039455-job.log: first fully successful run (correctly rounded math).
- run4-43170081594-job.log: final run (build, package check, dumpbin checks, ZIP, artifact, 34/34 tests, start without GPU).
- run4-replay-summary-windows.txt / replay-summary-linux.txt: the replay and js_math result lines on both systems
  (identical except the informational count of C-runtime differences).

Linux (build machine; final source):
- *-configure / -build / -tests.txt: clean builds and CTest of seven Linux configurations.
- hash-by-build.txt: native replay hashes and bit-exact tick counts in the seven builds.
- js-math-chromium.txt: js_math and the C runtime against 200,000 Chromium results per function.
- vulkan-import-library.txt: the Windows vulkan-1.lib generation, run with llvm-lib.
- linux-package/: the stages of .github/workflows/windows-build.yml run on Linux before GitHub was
  available (configure with fetched SDL3 + glslang, build, install, package check, zip, CTest, then the
  ZIP extracted and played). Recorded before the js_math change.
- post-matrix-utf8-test.txt, fresh-extract-*: from the first N6.5 package (before the Windows runs).
