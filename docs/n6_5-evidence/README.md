# N6.5 evidence (2026-11-11, Linux build machine)

- *-configure / -build / -tests.txt: clean builds and CTest of seven Linux configurations.
- post-matrix-utf8-test.txt: the UTF-8 path test, added afterwards, in the same seven build trees.
- hash-by-build.txt: replay hashes (identical to docs/n6-evidence/hash-by-build.txt).
- vulkan-import-library.txt: the Windows vulkan-1.lib generation, run with llvm-lib.
- linux-package/: the stages of .github/workflows/windows-build.yml run on Linux (configure with
  fetched SDL3 + glslang, build, install, package check, zip, CTest, then the ZIP extracted and played).
- fresh-extract-*: the delivered source ZIP built from an empty folder.
Nothing here was run on Windows (see N6_5_BUILD_REPORT.md).
