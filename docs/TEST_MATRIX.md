# N7 test matrix — 2026-11-12

Linux: Ubuntu 24.04 container, GCC 13.3 / clang 18.1, Mesa lavapipe (llvmpipe), Xvfb, no sound card.
Windows: GitHub Actions windows-2022, MSVC (VS 2022), no GPU. Legacy side: Chromium 153 + Playwright.
Logs: docs/n7-evidence.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (36 tests each) | PASS 36/36 each, 0 compiler warnings | linux-matrix/core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (67 tests each) | PASS 67/67 each, 0 compiler warnings | linux-matrix/desktop-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (67) | PASS 67/67, no sanitizer report | linux-matrix/asan-desktop-*.txt |
| Earlier matrix runs during N7 | run 1: clang builds FAIL (unused constant, fixed); run 2: `window_menu_selftest` FAIL once in a parallel CTest (now RUN_SERIAL, explicit capture wait); both re-run above | linux-matrix/earlier-runs |
| Legacy replays: 14 replay hashes / bit-exact counts in the 7 Linux builds | PASS: identical in all 7 builds, identical to N6.5 and to Windows | linux-matrix/hash-by-build.txt, windows-ci/run2-replay-summary-windows.txt |
| `audio_webaudio_parity`: decoding (49 files), 7 Web Audio graphs vs Chromium, legacy sound rules | PASS (relative RMS 6.1e-8 … 4.9e-2 within per-graph limits) | docs/N7_UI_AUDIO.md §2 |
| `ui_menus_settings_hud` (936 checks): 65 settings vs the legacy recording, presets, persistence, font atlas (0 missing), keyboard-only navigation (47/47 controls), mouse / touch, HUD on 5 sizes, capture rules, FPS statistics, touch controls | PASS | N7_UI_AUDIO.md §7 |
| `window_menu_selftest` (Xvfb + lavapipe): home → maps → loading → game → fire → pause → settings (FOV, volume) → main menu → exit, settings file written | PASS in all 4 desktop builds; also with SDL's dummy audio driver (1,093,632 frames mixed) | linux-matrix/menu-selftest-*.log |
| UI captures, validation layers on: 4 sizes (1280x720 x2, 2400x1080 touch, 800x480) | PASS: exit 0, no warning / error lines | native-ui/*.log |
| Comparison with legacy pages (side by side) | Visual comparison only (no PSNR threshold for UI) | legacy_left_native_right.jpg |
| Windows run 1 (43335019026) | FAIL: MSVC C2015 in ui_font.cpp (target without /utf-8); fixed | windows-ci/run1-43335019026-failed-steps.log |
| Windows run 2 (43336140592): MSVC build, package check, ZIP, artifact, CTest without GPU (36), start without GPU | PASS: 0 warnings in project code (2 in SDL), package 766 files / 97 MiB, artifact `CAT_ARENA_FERAL_CLAW_N7_WINDOWS` 13997451666 (35,146,465 bytes), 36/36, clean "Vulkan is not available" | windows-ci/run2-43336140592-job.log |
| Windows with a GPU / sound card: menus, rendering, audio, input | NOT_RUN (no GPU or sound on hosted runners, no Windows PC) | — |
| Android: pointer capture, touch, safe area on a device | NOT_RUN (N9) | unit tests only |
| Real sound card on Linux | NOT_RUN (none in the container) | — |
| Gamepad | NOT_RUN (no device) | — |

# N6.5 test matrix — 2026-11-11 / 12

Windows: GitHub Actions windows-2022 runner, MSVC 19.44 (VS 2022 17.14), Windows SDK 10.0.26100, no
GPU. Linux: as N6 (GCC 13.3 / clang 18.1, Mesa lavapipe, Xvfb). Logs: docs/n6_5-evidence.

| Check | Result | Evidence / scope |
|---|---|---|
| Windows: MSVC configure + build of every target (final run 43170081594) | PASS; 2 warnings, both in SDL's sources | windows-ci/run4-*-job.log |
| Windows: package check + dumpbin (x64, no VC++ runtime DLL, SDL3.dll imported, vulkan-1.dll delay-loaded) | PASS: 703 files, 91 MiB | run4 job log |
| Windows: ZIP + artifact `CAT_ARENA_FERAL_CLAW_N6_WINDOWS` uploaded | PASS: artifact 13921817501 (ZIP 29,987,699 bytes) | run4 job log |
| Windows: CTest without GPU (`ctest --preset windows-release`) | PASS 34/34 | run4 job log |
| Windows: every legacy replay identical to Linux (bit-exact counts, worst errors, native hashes) | PASS: all 14 replay lines identical | run4-replay-summary-windows.txt vs replay-summary-linux.txt |
| Windows: start without GPU (`--headless-test`; `--vk-info` must report the missing driver) | PASS: exit 0; clean "Vulkan is not available" message, exit 1 | run4 job log |
| Windows runs before the fixes: run 1 (MSVC C2039), run 2 (32/33: trajectories 1.30e-9) | FAIL, fixed (N6_5_BUILD_REPORT.md §3.2) | run1-, run2-*-job.log |
| js_math vs Chromium 153: 200,000 arguments per function | sin / cos / tan / atan2 bit-exact on all; pow on all but 7 (Chromium 1 ulp off) | js-math-chromium.txt |
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (34 tests each) | PASS 34/34 each, 0 warnings | core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (64 tests each) | PASS 64/64 each, 0 warnings | desktop-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (64) | PASS 64/64, no sanitizer report | asan-desktop-*.txt |
| Same native bits in the 7 Linux builds (14 replay hashes and bit-exact counts) | PASS: identical in all 7 builds, and equal to the Windows run | hash-by-build.txt |
| `vulkan-1.lib` from the import allowlist (llvm-lib) | PASS: 151 commands from vulkan-1.dll | vulkan-import-library.txt |
| Package build, install, package check, zip, extracted ZIP played (Linux, before js_math) | PASS | linux-package/ |
| Windows with a GPU: rendering, window, input, fullscreen, performance | NOT_RUN (no GPU on hosted runners, no Windows PC) | N6_5_BUILD_REPORT.md §10 |

# N6 test matrix — 2026-11-11

Environment: as N5.1 (Linux x64, GCC 13.3 / clang 18.1, Mesa lavapipe, Xvfb, Chromium + Playwright
for the legacy side). Logs: docs/n6-evidence. Earlier matrices follow below as recorded; their
tests were re-run in every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (32 tests each) | PASS 32/32 each, 0 warnings | core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (60 tests each) | PASS 60/60 each, 0 warnings | desktop-debug-*, desktop-release-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (60) | PASS 60/60, no sanitizer report | asan-desktop-*.txt |
| Legacy enemy tapes: 4 maps x 6 scenarios, 48,000 ticks, 64 values per tick | PASS on every tick: worst 1.4e-14 (tolerance 1e-9); bit-exact ticks 18,273 / 9,596 / 9,580 / 9,590; all 20 types, up to 20 enemies, waves 1-5, 165 kills, 27 player deaths, 1,716 routes | enemy_legacy_tapes.txt |
| Legacy animation + effect tapes: same tapes, 81 values per tick, 441,990 poses | PASS: poses / hands / bodies / Marine Cat worst 1.1e-13 (tolerance 2e-5); cosmetic random draws equal on every tick | enemy_animation_and_effects.txt |
| Recorder self-check: enemy fixtures unchanged by the version-2 animation recording; a legacy picture taken again is identical | PASS | capture-identity.txt |
| Unit tests: enemies (tables + Helios: waves, cadence, cap, distance, decisions, bodies, melee, shots, restart); presentation (poses, phases, death, hands, instance lists, x-ray, shadows, Marine Cat parts and visibility, meshes) | PASS 8,759 + 83,061 checks | unit_tests.txt |
| Same native bits in 7 builds (enemy and animation hashes per map, N5 and trajectory hashes) | PASS: all 14 replay hashes equal in all 7 builds (combat 36759acbc0ae3d1 and effects 80d2f9e2ef4d703c unchanged from N5.1; four trajectory hashes unchanged; new: enemies and animation per map) | hash-by-build.txt |
| Frames of the Helios tapes vs legacy pictures, 21 frames, 1280x720 | 42.5-63.6 dB, median 52.0 (first native run during the work: 19.9-59.4) | frames/tape_frames_psnr.txt, frames/tape_frames_legacy_left_native_right.jpg |
| Staged scenes vs legacy pictures: 20 types idle, 20 types wind-up + projected shadows, final five + outlines, 10 bodies, 2 Marine Cats | 48.5-52.8 dB | frames/staged_scenes_psnr.txt, frames/staged_legacy_left_native_right.jpg |
| CTest picture tests (5 staged scenes >= 45 dB, 5 tape frames >= 40 dB) | PASS in the 4 desktop configurations | *-tests.txt |
| Automatic Vulkan path vs 1.2 baseline, 4 staged scenes | PASS: 4/4 byte-identical, validation 0 (exit codes 0) | path-identity.txt |
| N2 Helios golden frames, automatic and baseline | PASS 7/7 byte-identical each | n2-golden-check.txt |
| N5 combat and effect tapes | PASS, unchanged (18,220 / 18,186 bit-exact ticks) | n5_replays.txt |
| Window under Xvfb: input self-test (automatic, baseline; no enemies), map cycle + resize with waves | PASS, validation 0 / 0 | window/*.log, *.json |
| legacy/, reference/, assets n1/n2/n4/n5, fixtures n2-n5 unchanged vs N5.1 | PASS: 682 files, 0 changed | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (60) | PASS 60/60, 0 warnings | fresh-extract-*.txt |
| Sound, Marine Cat clips (not used by the legacy runtime), mirrors | NOT IMPLEMENTED | N7 / not applicable / N10 |
| Windows x64, Android ARM64, hardware GPU, real input devices | NOT_RUN | no toolchain / device |

# N5.1 test matrix — 2026-11-10

Environment: docs/n5_1-evidence/environment.json (as N5). Logs: docs/n5_1-evidence. Earlier matrices
follow below as recorded; their tests were re-run in every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (22 tests each) | PASS 22/22 each, 0 warnings | core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (40 tests each) | PASS 40/40 each, 0 warnings | desktop-debug-*, desktop-release-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (40) | PASS 40/40, no sanitizer report | asan-desktop-*.txt |
| Light selection vs legacy (`uploadLights`), 29 tapes, 18,220 ticks | PASS: count and sums equal on every tick, 8,603 lights uploaded | weapon_fx_legacy_tapes.txt |
| Combat and effect replays (N5 values) | PASS, unchanged | combat_legacy_tapes.txt, weapon_fx_legacy_tapes.txt |
| Recorder self-check: combat fixture byte-identical to N5; effect fixture v2 = v1 in its first 40 values per tick | PASS | capture-identity.txt |
| Same native bits in 7 builds | PASS: combat 36759acbc0ae3d1, effects 80d2f9e2ef4d703c (new: three more values per tick), four trajectory hashes unchanged, in all 7 | hash-by-build.txt |
| Effect unit tests incl. selection (> 16 lights, viewer, ties) and billboard | PASS, 533 checks | *-tests.txt |
| Frames during effects vs legacy, 17 frames without targets, 1280x720 | 41.0-59.2 dB, median 57.1 (N5: 20.0-59.2, median 27.8) | frames/summary.md, frames/effect_frames_comparison.json, frames/legacy_left_N5_middle_N5_1_right.jpg |
| Frames with practice targets (7) | 17.5-50.5 dB: enemy model (legacy) vs box (native). Not a pass / fail check | frames/summary.md |
| Weapon views vs legacy, 16 weapons | 51.5-52.5 dB (N5: 45.4-47.5) | frames/weapon_views_comparison.json |
| Frame tests in CTest: rocket fired, rocket blast, medkit (minimum 40 dB); weapon views (minimum 40 dB) | PASS | *-tests.txt (desktop) |
| Lights off (`--features -dlights`) on the rocket frames | 23.9-25.7 dB, as expected without lights | lights-off.txt |
| Automatic Vulkan path vs 1.2 baseline, three frames with lights | PASS: byte-identical, validation 0 / 0 | path-identity.txt |
| N2 Helios golden frames (no lights), automatic and baseline | PASS 7/7 byte-identical each | n2-golden-check.txt |
| Window under Xvfb: input self-test and map cycle with weapons and lights | PASS, validation 0 / 0 (automatic and baseline self-test; map cycle + resize) | window/*.log |
| legacy/, reference/, assets unchanged vs N5 package | PASS: 663 files, 0 changed | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (40) | PASS 40/40 | fresh-extract-*.txt |
| Lights on enemy / player models; lens flares | NOT IMPLEMENTED | N6 / later |
| Windows x64, Android ARM64, hardware GPU, real input devices | NOT_RUN | no toolchain / device |

# N5 test matrix — 2026-11-10

Environment: docs/n5-evidence/environment.json (as N4). Logs: docs/n5-evidence. Earlier matrices
follow below as recorded; their tests were re-run in every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (22 tests each) | PASS 22/22 each, 0 warnings | core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (37 tests each) | PASS 37/37 each, 0 warnings | desktop-debug-*, desktop-release-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (37) | PASS 37/37, no sanitizer report | asan-desktop-*.txt |
| Legacy combat tapes: 29 tapes, 18,220 ticks, 48 values per tick | PASS: all ticks bit-identical; 16/16 weapons fired, 630 pellets, 32 detonations, 4 frags, 1,614 random draws | combat_legacy_tapes.txt |
| Legacy effect tapes: same 29 tapes, 40 values per tick | PASS: camera / glow / recoil / muzzle worst 1.1e-16; quad and light counts, batch checksums and 17,578 cosmetic draws equal; 18,186 ticks bit-identical | weapon_fx_legacy_tapes.txt |
| Same native bits in 7 builds, both fixtures | PASS: combat 36759acbc0ae3d1 and effects 6dc0fcb7e1a865c3 in all 7 | hash-by-build.txt |
| Combat unit tests (weapon table, random stream, boxes and rays, world shots, hitscan, spread, cadence / magazine / reload of all 16, swap, projectile travel, splash falloff, self damage and impulse, shotgun recoil, kills and reroll, medkits, determinism) | PASS, 18,372 checks | *-tests.txt |
| Effect unit tests (tables, quad writers, muzzle anchoring of 16 viewmodels, raise / reload / recoil, simulation untouched by effects, explosions, pool limits, surface normals) | PASS, 523 checks | *-tests.txt |
| HUD unit tests | PASS, 29,702 checks | *-tests.txt |
| Recorder self-check: combat fixture identical with effects running and with pictures taken; effect fixture identical with pictures taken | PASS (`cmp`) | capture-identity.txt |
| Weapon views vs legacy pictures, 16 weapons, 1280x720 | PSNR 45.4-47.5 dB | frames/weapon_views_comparison.json, frames/weapon_views_*.jpg |
| Weapon views in CTest (weapons 0, 4, 7, 10; minimum 35 dB) | PASS | *-tests.txt (desktop) |
| Frames during effects vs legacy, 24 frames of 5 tapes | 17.5-59.2 dB. Low values: missing dynamic lights around flashes and blasts, and enemy model (legacy) vs box (native). Not a pass / fail check | frames/effect_frames_comparison.json, frames/effect_frames_*.jpg |
| Offscreen weapon walk in CTest, automatic path and Vulkan 1.2 baseline | PASS, validation 0 / 0 | *-tests.txt (desktop) |
| Window under Xvfb: input self-test incl. fire, reload, swap; automatic and baseline paths | PASS, validation 0 / 0 | window/selftest_*.{log,json} |
| Window: map cycle through four maps + resize with weapons | PASS, validation 0 / 0 | window/map_cycle.log |
| N2 Helios golden frames (pose captures, no overlay), automatic and baseline | PASS 7/7 byte-identical each | n2-golden-check.txt |
| N3 / N4 trajectory and navigation fixtures | PASS, hashes unchanged | *-tests.txt, hash-by-build.txt |
| legacy/, reference/, assets/n1, assets/n2, assets/n4 unchanged vs N4 package | PASS: 596 files, 0 changed | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (37) | PASS 37/37 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| Dynamic lights on map surfaces; legacy HUD layout; sounds | NOT IMPLEMENTED | see N5_WEAPONS.md section 5 |
| Player death and respawn vs legacy | NOT_RUN (unit test only; no legacy tape kills the player) | — |
| Combat on Bastion Keep, Reactor Bastion, The Arsenal vs legacy | NOT_RUN (runs there; tapes were recorded on Helios) | — |
| Windows x64, Android ARM64, hardware GPU, real input devices | NOT_RUN | no toolchain / device |

# N4 test matrix — 2026-11-10

Environment: docs/n4-evidence/environment.json (as N3). Logs: docs/n4-evidence. Earlier matrices
follow below as recorded; their tests were re-run in every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest: GCC Debug, GCC Release, clang Release (17 tests each) | PASS 17/17 each | core-*, core-release-*, clang-release-*.txt |
| Desktop build + CTest: GCC Debug, GCC Release, clang Debug (26 tests each) | PASS 26/26 each, 0 warnings | desktop-debug-*, desktop-release-*, clang-desktop-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (26) | PASS 26/26 (an earlier run found a zero-length read in a new test reader; fixed, see DEVELOPMENT_LOG_N4.md) | asan-desktop-*.txt |
| Legacy trajectories, Bastion Keep: 15 tapes, 13,420 ticks | PASS: worst 1.8e-15, 13,125 bit-identical, contacts equal (3 ceiling) | physics_legacy_trajectories_bastion-keep.txt |
| Legacy trajectories, Reactor Bastion: 17 tapes, 14,820 ticks | PASS: worst 1.1e-16, 14,378 bit-identical, contacts equal (3 ceiling) | physics_legacy_trajectories_reactor-bastion.txt |
| Legacy trajectories, The Arsenal: 14 tapes, 12,720 ticks | PASS: worst 8.9e-16, 12,683 bit-identical, contacts equal (5 ceiling) | physics_legacy_trajectories_q3-gallery.txt |
| Legacy trajectories, Helios (N3 fixture, unchanged) | PASS, same hash as N3 | physics_legacy_trajectories_helios-crucible.txt |
| Same native trajectory bits in 7 builds, all four maps | PASS: one hash per map | trajectory-hash-by-build.txt |
| Navigation vs legacy, four maps: all nodes, adjacency of 3 walker profiles (ids, order, costs), 50 route queries each (start/goal, expansions, chain, smoothed points) | PASS, identical on every map | navigation_*.txt, nav_capture_*.json |
| Navigation properties: every smoothed leg traversable; `route()` equals the separate steps; larger walkers never have more links | PASS | navigation_*.txt |
| Regions: load on four maps; every spawn and nav node inside a region | PASS (no legacy comparison) | navigation_*.txt |
| Frames vs legacy at 1280x720: Bastion Keep 5, Reactor Bastion 4, The Arsenal 3 poses | PSNR 44.4-61.2, 46.0-63.5, and 47.9 / 47.9 / 33.5 dB (missing model in the last) | frames/summary.md, frames/*/comparison.json, *_legacy_left_native_right.jpg |
| New maps: automatic Vulkan path vs Vulkan 1.2 baseline | PASS: 0 differing frames, validation 0 / 0 | frames/path-identity.txt |
| N2 Helios golden frames after the shader interface change (automatic and baseline) | PASS 7/7 byte-identical each | n2-golden-check.txt |
| Per-map offscreen render tests in CTest (3 maps) | PASS, validation clean | *-tests.txt |
| In-window map change through all four maps (Xvfb), automatic and baseline paths | PASS: 3 switches, validation 0 / 0 | window/map_cycle_*.{log,json} |
| Sky box extraction: generator deterministic, hashes recorded | PASS | assets/n4/skybox-env-gallery_nebula.json |
| legacy/, reference/, assets/n1, assets/n2 unchanged vs N3 package | PASS | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (26) | PASS 26/26 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| MD3 models, portal/mirror/monitor rendering, PVS | NOT IMPLEMENTED | see N4_MAPS_AND_NAVIGATION.md |
| Region lookup vs legacy | NOT_RUN | the legacy has no such function |
| Windows x64, Android ARM64, hardware GPU, real input devices | NOT_RUN | no toolchain/device |

# N3 test matrix — 2026-11-10

Environment: docs/n3-evidence/environment.json (Ubuntu 24.04 container, GCC 13.3 and clang 18.1,
2 CPU cores, Mesa lavapipe software Vulkan 1.4.318, validation layer 1.4.309; legacy reference in
Playwright Chromium headless). Logs: docs/n3-evidence. The N2.1 matrix follows below unchanged in
content; its tests were re-run as part of every CTest run listed here.

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest, GCC Debug (10) | PASS 10/10 | core-*.txt |
| Core-only build + CTest, GCC Release -O3 (10) | PASS 10/10 | core-release-*.txt |
| Core-only build + CTest, clang Release (10) | PASS 10/10 | clang-release-*.txt |
| Desktop GCC Debug build (-Werror) + CTest (16) | PASS 16/16, 0 warnings | desktop-debug-*.txt |
| Desktop GCC Release + CTest (16) | PASS 16/16 | desktop-release-*.txt |
| Desktop ASan + UBSan + LeakSanitizer + CTest (16) | PASS 16/16 | asan-desktop-*.txt |
| Desktop clang Debug + CTest (16) | PASS 16/16 | clang-desktop-*.txt |
| Physics unit tests (synthetic world): speeds, jump apex/air time, jump buffer, coyote time, step up / too-high block, slope up/down, ceiling, stacked floors, low gap vs crouch collider, wall slide, slide, jump pad, look clamps, view bob/roll, determinism | PASS, 9,157 checks | physics_units.txt |
| Legacy trajectory replay, Helios, 17 tapes, 21,820 ticks, 30 values per tick | PASS: worst difference 1.8e-15 (tolerance 1e-9), 21,725 ticks bit-identical, contact counters equal | physics_legacy_trajectories.txt, legacy_trajectories_capture.json |
| Same native trajectory bits in 7 builds (GCC/clang, Debug/Release, sanitizers) | PASS: identical hash | trajectory-hash-by-build.txt |
| All four maps: collision data loads, every spawn clear and supported, 5 s random play per spawn stays inside | PASS (no legacy reference for 3 maps) | physics_all_maps.txt |
| Headless first-person walk, 1,339 ticks, 13 frames, validation | PASS, 0 errors / 0 warnings | walk/walk.json, walk/walk_sheet.png |
| Window input self-test (Xvfb): SDL key/mouse events through the event path; relative mouse on, yaw change exact, moved, jumped, slid, crouched; auto and Vulkan 1.2 baseline paths | PASS, validation 0 / 0 | window/input_selftest_*.{log,json} |
| N2 golden preview frame after N3 (baseline path) | PASS: byte-identical | n2-golden-check.txt |
| legacy/, reference/, assets/ unchanged vs N2.1 package | PASS, 593 files | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (16) | PASS 16/16 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| Legacy reference trajectories for Bastion Keep, Reactor Bastion, The Arsenal | NOT_RUN | N4 |
| Real keyboard/mouse devices; Windows capture | NOT_RUN | events were synthetic SDL events on Linux/Xvfb |
| Windows x64, Android ARM64, hardware GPU | NOT_RUN | no toolchain/device |
| Cross-platform bit-identical simulation (other C math libraries) | NOT_RUN | Linux glibc only |
| Weapons, enemies, HUD, audio, UI, network | NOT_RUN | not implemented |

# N2.1 test matrix (2026-11-10, results as recorded for N2.1; evidence in docs/n2_1-evidence)

| Check | Result | Evidence / scope |
|---|---|---|
| Core-only build + CTest, Debug (7) | PASS 7/7 | core-*.txt |
| Core-only build + CTest, Release (7) | PASS 7/7 | core-release-*.txt |
| Desktop Debug build, GCC, -Werror | PASS, 0 warnings | desktop-debug-build.txt |
| Desktop Debug CTest (13) | PASS 13/13 | desktop-debug-tests.txt |
| Desktop Release build + CTest (13) | PASS 13/13 | desktop-release-*.txt |
| Desktop ASan + UBSan + LeakSanitizer, CTest incl. all GPU path tests (13) | PASS 13/13 | asan-desktop-*.txt (N2 ran sanitizers on the GPU-free tests only) |
| Desktop Debug with clang 18, -Werror, CTest (13) | PASS 13/13 | clang-desktop-*.txt |
| Build with a Vulkan 1.2.198 SDK header directory + GPU tests | PASS | sdk-1.2.198-headers-*.txt |
| Path selection rules, synthetic devices (GPU-free) | PASS, 69 checks | vulkan_path_selection.txt |
| Path matrix on the installed driver, 17 requests: selection, objects, identical frames, validation | PASS 17/17, 0 validation errors, 0 warnings | render_path_matrix.txt |
| Device-simulation layer self-test | PASS, 31 checks | vk_sim_layer_selftest.txt |
| Simulated devices: bare 1.2, 1.2+extensions, extension without bit, bare 1.3, 1.3 without bits, 1.4 without hostImageCopy, 1.4 without optional bits, 1.4 with unresolvable entry points, 1.1 rejected | PASS 11/11, 0 validation errors (2 loader notices per run about the test layer) | render_path_simulated_devices.txt |
| Linked Vulkan symbols are 1.0 + WSI only (library and executable) | PASS, 89 imports; negative check fails as intended | vulkan_link_imports*.txt, cat_arena_vulkan_imports.txt |
| 1280x720, 7 poses, 17 rendering configurations vs N2 golden frames | PASS: 7/7 pixel-identical in every configuration | matrix/matrix_summary.md, matrix/*/report.json |
| Simulated Vulkan 1.1 device (CLI) | PASS: rejected with "No Vulkan 1.2+ device" | matrix/sim_vk11_rejected/run.log |
| N2 offscreen render test (preview sanity, determinism, feature toggles, winding, validation) on the auto path | PASS | render_helios_offscreen.txt |
| Window (SDL3 X11, Xvfb), FIFO 120 frames with live resize 1280x720 -> 960x540, 6 paths | PASS, 0 errors / 0 warnings each | window/*_fifo.* |
| Window MAILBOX (`--no-vsync`), 300 frames, 6 paths | PASS, 0 / 0 each | window/*_novsync.* |
| Presented window image == offscreen capture (baseline, auto) | PASS (max difference 0) | window/*_screen.png |
| Swapchain recreation under live resize | PASS (first execution; NOT_RUN in N2) | window/*_fifo.log |
| N0/N1/N2 regression (core, assets, baked_maps for 4 maps, scene units, Helios scene) | PASS | included in every CTest run above |
| legacy/, reference/, assets/n1, assets/n2 unchanged vs N2 package | PASS | source-integrity.txt |
| Fresh extraction of the candidate ZIP: configure, build, CTest (13), baseline frame == golden | PASS 13/13 | fresh-extract-*.txt (delivered ZIP = candidate + these logs + manifest) |
| Real Vulkan 1.2 / 1.3 driver | NOT_RUN | simulated only |
| Hardware GPU (any vendor) | NOT_RUN | software rasterizer only |
| Depth+stencil formats on the dynamic-rendering path | NOT_RUN | lavapipe selects D32_SFLOAT |
| Windows x64 build/run | NOT_RUN | no toolchain |
| Android ARM64 | NOT_RUN | N9; no NDK/device |
| Performance / FPS | NOT MEASURED | CPU rasterizer; object counts reported instead |
| Physics, gameplay, AI, audio, UI, network | NOT_RUN | not implemented |
