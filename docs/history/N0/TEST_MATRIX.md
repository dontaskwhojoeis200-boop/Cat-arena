# N0 test matrix — actual results

| Check | Status | Evidence / limits |
|---|---|---|
| CMake core Debug + strict GCC14.2 C++20 build | PASS | core configure/build and debug.txt |
| Core Release build/tests | PASS | release-*.txt; 2/2 CTest targets |
| Fixed-clock suite | PASS | 60-second drift, render partition invariance, interpolation, negative elapsed, long-stall bound |
| Settings suite | PASS | roundtrip, version/truncation/range/bool/trailing-data rejection |
| Math/RNG suite | PASS | basis/inverse, handedness, seed golden vector/replay and [0,1) range |
| Input suite | PASS | key-action mapping, repeat, edge consumption, short tap, focus loss |
| Ownership/paths suite | PASS | move/scope/exception/release; absolute path/user directory creation |
| Linux SDL3/Vulkan desktop compile/link | PASS | desktop-build-final.txt; GCC14.2, headers/loader1.4.309, SDL3.2.10 |
| Vertex/fragment shader compile | PASS | shader-compile.txt; glslang15.1.0, Vulkan1.1 SPIR-V target |
| Desktop `--headless-test` plus core targets | PASS | desktop-tests.txt, 3/3; no GPU initialized |
| ASan/UBSan with leak checks disabled | PASS | sanitizers-no-leak.txt, 2/2 |
| Default sanitizer suite including LeakSanitizer | FAILED (runner limitation) | asan-test.txt: LeakSanitizer fatal error / ptrace restriction; not leak-free evidence |
| Graphical startup | FAILED (environment) | graphics-attempt.txt: No available video device, exit1 |
| Actual Vulkan instance/device/pipeline execution | NOT_RUN | SDL video failed before Vulkan init |
| Visible triangle, validation logs, resize/minimize, VSync | NOT_RUN | compile success is not graphics runtime evidence |
| Windows x64 build/run | NOT_RUN | Windows toolchain unavailable |
| Android ARM64 core cross-build/APK/device | NOT_RUN | NDK/Gradle unavailable; lifecycle APK work scheduled N9 |
| Source hashes + archive CRC preservation | PASS | source-integrity.txt and SOURCE_INVENTORY verification |
| Gameplay/visual/multiplayer parity | NOT_RUN | future milestones; no native gameplay yet |

All evidence is under docs/n0-evidence. Earlier audit/baseline findings are
historical, copied under docs/history/pre-N0, and are not N0 test runs.

Manual graphics checklist on a supported machine: launch with validation layers;
confirm triangle/clear; resize repeatedly; minimize/restore; change DPI/move between
monitors; close during resize; launch --no-vsync and --frame-cap 120; remove a shader
and confirm a logged nonzero exit; run --frames 300; check stderr for validation
errors. Test device/surface loss separately. Record real device, driver and output.
