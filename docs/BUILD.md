## Current Windows build

See [README_N9](../README_N9.md) for the current commands and supported target. The instructions below record earlier milestones.

# Building N0

Run commands from the extracted project root unless noted. No JavaScript runtime
is linked into the native app. Python3 is needed only for the inventory utility.
Core compilation has no external library dependency and does not need a GPU.

## Tested environment and pins

Linux x86_64 Debian13; GCC14.2.0; CMake4.4.4; Ninja1.13.2; SDL3 release-3.2.10;
Vulkan-Headers v1.4.309; system loader libvulkan.so.1.4.309; glslang15.1.0
(optimizer disabled). Exact archive SHA256 values are in DEPENDENCIES.json.
CMake minimum3.24; C++20 compiler required; Vulkan build headers >=1.4 required.
Runtime device/loader minimum1.1 with swapchain, graphics+present queue and color
attachment surface support. No optional feature bits are required for N0.

The master references SDK1.4.363.1. That SDK was not available here. A desktop
installation of that SDK can be used, but was not tested; the actual tested
baseline above must not be confused with a latest-SDK verification claim.

## Core, all desktop platforms

```sh
cd native
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

Targets: cat_arena_core (library), cat_arena_headless (self-test CLI),
cat_arena_tests (seven groups of behavioral checks). Release checks use explicit
requirements; they do not disappear when NDEBUG disables assertions.

## Windows x64 native shell (NOT_RUN here)

Install Visual Studio2022 C++ desktop tools + Windows SDK, CMake/Ninja, and Vulkan
SDK with glslc or glslangValidator. In an **x64 Native Tools Command Prompt**:

```bat
cd native
cmake --preset desktop-debug
cmake --build --preset desktop-debug
ctest --preset desktop-debug
build\desktop-debug\cat_arena.exe --headless-test
build\desktop-debug\cat_arena.exe
```

VULKAN_SDK should point to the installed SDK. SDL3 is downloaded with an archive
SHA256 pin by CAT_FETCH_SDL=ON in this preset. Alternatively build/install exact
SDL3.2.10 yourself and pass `-DCMAKE_PREFIX_PATH=C:/path/to/sdl-prefix` and
`-DCAT_FETCH_SDL=OFF`. The CMake post-build step copies a shared SDL DLL when the
SDL3::SDL3-shared target is used. The executable requires its adjacent
`assets/shaders` directory. GPU Vulkan drivers are supplied by the GPU vendor,
not this source ZIP. No Windows binary was generated in this run.

## Linux shell

Install compiler, CMake, Ninja, Vulkan headers/loader, shader compiler, SDL3
(or SDL build prerequisites). For Debian-like systems, representative packages:

```sh
sudo apt-get install build-essential cmake ninja-build libvulkan-dev glslang-tools \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
cd native
cmake --preset desktop-debug
cmake --build --preset desktop-debug
ctest --preset desktop-debug
./build/desktop-debug/cat_arena --headless-test
./build/desktop-debug/cat_arena
```

Ensure installed Vulkan headers satisfy >=1.4; otherwise use a Vulkan SDK and set
Vulkan_INCLUDE_DIR/Vulkan_LIBRARY. A working X11 or Wayland desktop, Vulkan ICD and
supported GPU are required. Optional validation layer package is typically
vulkan-validationlayers. Distributions differ; no apt install was possible here.

Common options:

```sh
./cat_arena --frames 300
./cat_arena --no-vsync --frame-cap 0
./cat_arena --frame-cap 120
./cat_arena --no-validation
./cat_arena --shaders /absolute/path/to/shaders
./cat_arena --settings /absolute/path/settings.txt
```

Default validation requested, VSync on, frame cap0, window1280x720. Escape or close
exits the N0 shell. `--headless-test` bypasses SDL/Vulkan initialization. `--frames`
limits loop iterations for smoke testing; it is not proof of presented frames.
Settings format (version, then width height cap vsync validation sensitivity):

```text
cat-settings 1
1280 720 0 1 1 0.002
```

The settings codec is tested. The app reads an explicit settings path; it does
not yet auto-save preferences. Render assets resolve beside the actual executable
via SDL_GetBasePath, not the working directory.

## Exact constrained-runner build used in N0

Dependencies were fetched into a scratch deps folder, not bundled as runtime code.
SDL3 was built static, X11/Wayland disabled because their headers were absent.
This permits compiling/linking the shell, **not graphical runtime validation**.
`deps/SDL-release-3.2.10` and `deps/Vulkan-Headers-1.4.309` refer to extracted pinned
archives from DEPENDENCIES.json; glslang was built as follows:

```sh
cmake -S deps/SDL-release-3.2.10 -B deps/sdl-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/deps/install" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
  -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build deps/sdl-build -j 4
cmake --install deps/sdl-build
cmake -S deps/glslang-15.1.0 -B deps/glslang-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \
  -DENABLE_GLSLANG_BINARIES=ON
cmake --build deps/glslang-build -j 4
cmake -S native -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/deps/install" \
  -DVulkan_INCLUDE_DIR="$PWD/deps/Vulkan-Headers-1.4.309/include" \
  -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  -DCAT_GLSLANG="$PWD/deps/glslang-build/StandAlone/glslang"
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
./build-desktop/cat_arena --frames 5
```

The final command failed at SDL initialization with `No available video device`.
There are no native screenshots and no validation-layer runtime results.

## Sanitizers

```sh
cmake -S native -B build-asan -G Ninja -DCAT_BUILD_DESKTOP=OFF \
  -DCAT_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Here LeakSanitizer failed due to runner ptrace restrictions. A separate rerun with
`ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure`
passed ASan/UBSan checks. Do not treat it as a leak-check pass. Keep default leak
detection on when running on a machine that supports it.

## Android ARM64 provisional core setup (NOT_RUN)

Suggested reproducible starting toolchain: Android NDK r27c (27.2.12479018),
API26+, CMake3.24+ (tested host CMake4.4.4 only). NDK/toolchain availability was
not verified here. This builds the native core library only, not an APK:

```sh
cmake -S native -B build-android-core -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCAT_BUILD_DESKTOP=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-core --target cat_arena_core
```

Do not infer GPU Vulkan support from the Android API level. SDL3 has Android
support, but N9 must integrate its Android project/activity, Gradle, shared
library entry, lifecycle/surface recreation, touch/KBM, audio and asset packaging.
No Gradle wrapper or fake APK is included in N0, and no Gradle version is claimed
tested. The preserved reference APK remains the web build, not this native code.

## Source integrity and next iteration

```sh
python3 native/tools/inventory_sources.py --verify docs/N0_SOURCE_INVENTORY.json
```

This hashes all reference files, verifies ZIP/APK CRCs and member hashes, and
indexes script hashes without executing legacy JS. Never edit legacy/reference.
Use N0_FILE_MANIFEST.json for package contents. Update source/test/progress records
with every milestone, beginning N1 asset extraction.

## N1: use the included content and native loaders

N0 instructions above remain valid. The package now contains assets/n1; a core
configuration automatically registers the baked_maps test when this data exists.

```sh
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core
ctest --test-dir build-core --output-on-failure
./build-core/cat_arena_asset_check assets/n1
```

New targets: cat_arena_assets (C++20 library), cat_arena_asset_tests,
cat_arena_asset_check. There is no native JSON/JavaScript interpreter dependency.
The asset library can be cross-compiled with the same provisional NDK core
configuration from N0 (`--target cat_arena_assets`); Android was NOT_RUN.

## N1: rebuild all offline content

Offline tool dependencies: Node>=22 (tested24.22.1), Python>=3.11, acorn8.15.0,
@napi-rs/canvas0.1.100. npm lockfile pins all resolved packages; npm is used only
for conversion setup. No browser or graphics driver is needed. Original source
SHA256 is checked before executing its selected CPU modules.

From the project root:

```sh
npm ci --prefix native/tools --ignore-scripts
python3 native/tools/bake_assets.py --out build-assets-a
python3 native/tools/validate_assets.py build-assets-a
python3 native/tools/bake_assets.py --out build-assets-b
python3 native/tools/validate_assets.py build-assets-a --compare build-assets-b
./build-core/cat_arena_asset_check build-assets-a
```

Output directories must be empty, preventing accidental stale-file inclusion.
Do not point the baker at legacy/reference or overwrite a prior output directory.
After validation, replace assets/n1 with the selected complete output directory
and reconfigure CMake. Packaged assets were produced exactly this way;577 tracked
content files and the conversion manifest match across both clean bakes.

`--finish-existing` is an explicit development-recovery option for the Python
wrapper; it requires a successfully completed JS conversion manifest. Normal
reproducibility acceptance uses clean directories and does not use this option.
The independent validator needs only Python's standard library. Optional full
PNG/JPEG/WebP decoding was additionally verified with Pillow13.0.0.

Native formats and vertex layouts are defined in N1_FORMAT.md. JSON sidecars are
for inspection, cadata/cmesh are for native loading. Do not interpret a successfully
loaded map as rendered or collision-tested: N2/N3 implement those paths.

## N2: native Helios Crucible renderer

N0/N1 instructions above remain valid. N2 adds `cat_arena_scene` (GPU-free scene preparation),
`cat_image_codec` (vendored stb, see native/third_party/stb/PROVENANCE.txt), `cat_arena_render`
and the GPU test `cat_arena_render_tests`. New shaders: sky, world, material (+ shared includes).

### Exact runner configuration used for N2 (Linux x86_64, Ubuntu 24.04 container)

GCC 13.3.0, CMake 3.28.3, Ninja; SDL3 release-3.2.10 (git tag, built static with X11 enabled,
Wayland/Xrandr/XInput/Xcursor dev headers absent); Vulkan-Headers vulkan-sdk-1.4.309.0; system
loader libvulkan.so.1.3.275; glslangValidator 15.2.0 (vulkan-sdk-1.4.309.0, built locally; N0
recorded 15.1.0); Vulkan device: Mesa lavapipe (llvmpipe, Vulkan 1.4.318, software CPU rasterizer);
Vulkan-ValidationLayers vulkan-sdk-1.4.309.0 built locally for testing (not shipped).

```sh
# dependencies (any equivalent install works; paths below are the runner's)
cmake -S deps/sdl3-3.2.10 -B deps/sdl3-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/deps/install" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
  -DSDL_X11=ON -DSDL_WAYLAND=OFF -DSDL_VULKAN=ON -DSDL_OFFSCREEN=ON -DSDL_RENDER=OFF -DSDL_GPU=OFF
cmake --build deps/sdl3-build && cmake --install deps/sdl3-build

# core + scene (no GPU, no SDL)
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core && ctest --test-dir build-core --output-on-failure

# desktop (window + offscreen renderer)
cmake -S native -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCAT_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH="$PWD/deps/install" \
  -DVulkan_INCLUDE_DIR="$PWD/deps/vulkan-headers/include" \
  -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  -DCAT_GLSLANG="$PWD/deps/glslang-install/bin/glslangValidator"
cmake --build build-desktop
VK_ICD_FILENAMES=/opt/lavapipe/lvp_icd.json ctest --test-dir build-desktop --output-on-failure
```

`render_helios_offscreen` exits 77 (CTest: SKIPPED) only when no Vulkan driver/device exists.
With the validation layer installed it also fails on any validation error. To use a locally built
layer: `VK_LAYER_PATH=<install>/share/vulkan/explicit_layer.d LD_LIBRARY_PATH=<install>/lib`.

### Running

```sh
./build-desktop/cat_arena                                   # window, Helios, fly camera (click to capture mouse)
./build-desktop/cat_arena --camera 78,42,0,2.45,0.15        # x,y,feetZ,yaw,pitch (legacy player convention)
./build-desktop/cat_arena --triangle                        # N0 triangle shell
./build-desktop/cat_arena --offscreen 1280x720 --capture out.png --time 10
./build-desktop/cat_arena --offscreen 1280x720 --capture-set tools/n2/helios_poses.txt \
    --capture-dir frames --report frames/native_frames.json --time 10
./build-desktop/cat_arena --offscreen 640x360 --capture lm.png --debug-view 1      # lightmap only
./build-desktop/cat_arena --offscreen 640x360 --capture nofog.png --features -fog,-posterize
```

Features: sky, world, materials, clouds, translucent, lightmap, fog, posterize, culling
(`+name`/`-name`). Debug views: 1 lightmap, 2 fullbright, 3 normals. Assets resolve from
`assets/n1` beside the executable, then `--assets`, then (development builds) the source tree.
`cmake --install` copies shaders and `assets/n1` beside the installed executable.

### Windows x64 (NOT_RUN in N2)

As N0: Visual Studio 2022, Vulkan SDK (provides headers, loader, glslangValidator/glslc and the
validation layer), `cmake --preset desktop-debug` (fetches SDL3 3.2.10). The executable needs
`assets/shaders` and `assets/n1` beside it (`cmake --install build\desktop-debug --prefix dist`).
No Windows build was performed in N2.

### Legacy reference frames and comparison (tooling, not product)

```sh
# Chromium + playwright (Node >= 22); in the N2 runner WebGL2 ran on lavapipe:
CHROMIUM_WEBGL_RENDERER=lavapipe node tools/n2/legacy_reference.cjs \
    --poses tools/n2/helios_poses.txt --out legacy-frames --time 10
python3 tools/n2/compare_frames.py --native frames --legacy legacy-frames --out compare   # needs Pillow
```

The reference tool checks the legacy HTML SHA-256, serves it unmodified from memory, presses
the real start/deploy controls and records every runtime state change it makes.

## N2.1 — Vulkan requirements and path control (2026-11-10)

Supersedes the Vulkan version statements above.

- **Run time:** a Vulkan **1.2** device and a Vulkan 1.1+ loader. Vulkan 1.3/1.4 are optional.
- **Build time:** any Vulkan loader library (`libvulkan.so.1`, `vulkan-1.lib`), a GLSL compiler
  (`glslangValidator` or `glslc`) and SDL3 3.2.10. Vulkan headers are **not** taken from the SDK:
  the build compiles against `native/third_party/vulkan-headers` (1.4.309). `CAT_VULKAN_HEADERS`
  overrides the directory; headers older than 1.4.304 will not compile.
- The explicit `-DVulkan_INCLUDE_DIR=...` used in the N2 runner is no longer needed. The N2.1
  runner configuration was:

```sh
cmake -S native -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCAT_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH="$PWD/deps/install" -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  -DCAT_GLSLANG="$PWD/deps/glslang-install/bin/glslangValidator"
cmake --build build-desktop
VK_ICD_FILENAMES=/opt/lavapipe/lvp_icd.json ctest --test-dir build-desktop --output-on-failure
```

Tests added in N2.1: `vulkan_path_selection` (no GPU), `render_path_matrix`, `vulkan_link_imports`
(needs `nm`; not on MSVC), `vk_sim_layer_selftest` and `render_path_simulated_devices` (Linux; the
test layer is built when `CAT_BUILD_VK_SIM_LAYER=ON`, the default on Linux). GPU tests exit 77
(SKIPPED) when no Vulkan 1.2 device exists.

```sh
./build-desktop/cat_arena --vk-info                    # capabilities and selected path as JSON
./build-desktop/cat_arena --vk-baseline                # mandatory Vulkan 1.2 path
./build-desktop/cat_arena --vk-max-api 1.3 --vk-no-extensions
./build-desktop/cat_arena --vk-disable dynamic_rendering,host_image_copy
./build-desktop/cat_arena --frames 120 --resize-test   # swapchain recreation check

# every path against the N2 golden frames (needs python3 + Pillow for the comparison):
sh tools/n2_1/run_path_matrix.sh ./build-desktop/cat_arena . out/matrix build-desktop/vk_sim_layer
python3 tools/n2_1/compare_golden.py docs/n2-evidence/frames/native out/matrix out/summary.json out/summary.md
```

Windows x64 and Android ARM64 were NOT built in N2.1. On Windows the Vulkan SDK is needed only for
the loader import library, the shader compiler and (optionally) the validation layer.

## N3 — physics tests and traversal (2026-11-10)

No new dependency. `cat_arena_physics` builds in the GPU-free configuration, so the physics tests
run without SDL or Vulkan:

```sh
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core && ctest --test-dir build-core --output-on-failure
./build-core/cat_arena_physics_tests                                   # unit tests
./build-core/cat_arena_physics_tests assets/n1 native/tests/fixtures/n3/helios_trajectories.bin   # legacy replay
./build-core/cat_arena_physics_tests --maps assets/n1                  # all four maps
```

Desktop (configuration as in the N2.1 section):

```sh
./build-desktop/cat_arena                    # first-person traversal; click captures the mouse
./build-desktop/cat_arena --spawn 12 --fov 105
./build-desktop/cat_arena --fly              # N2 fly camera
./build-desktop/cat_arena --offscreen 960x540 --walk "w:150,w+shift:200,w+shift+jump:3,w@4:200" \
    --capture-every 60 --capture-dir out     # headless walk: PNG every 60 ticks + out/walk.json
./build-desktop/cat_arena --frames 60 --input-selftest
```

Re-recording the legacy reference (tooling only; Node >= 22 with Playwright Chromium):

```sh
node tools/n3/legacy_trajectories.cjs --out native/tests/fixtures/n3/helios_trajectories.bin --report capture.json
```

Windows x64 and Android ARM64 were NOT built or run in N3.

## N4 — maps, navigation tests and reference tools (2026-11-10)

No new dependency. New tests run in the GPU-free build: `physics_legacy_trajectories_<map>` (3) and
`navigation_<map>` (4). Desktop adds `render_map_<map>` (3).

```sh
./build-core/cat_arena_nav_tests assets/n1 bastion-keep native/tests/fixtures/n4/bastion-keep_navigation.bin
./build-core/cat_arena_physics_tests assets/n1 native/tests/fixtures/n4/q3-gallery_trajectories.bin q3-gallery

./build-desktop/cat_arena --list-maps
./build-desktop/cat_arena --map reactor-bastion            # keys 1-4 change map in the window
./build-desktop/cat_arena --map q3-gallery --offscreen 1280x720 --capture arsenal.png --time 10
./build-desktop/cat_arena --frames 120 --map-cycle-test
```

Re-creating the N4 references (tooling only; Node >= 22 with Playwright Chromium):

```sh
node tools/n4/legacy_skybox.cjs --map q3-gallery --out assets/n4/skybox-env-gallery_nebula.png
node tools/n3/legacy_trajectories.cjs --map bastion-keep --random 4 --random-ticks 1500 --out native/tests/fixtures/n4/bastion-keep_trajectories.bin
node tools/n4/legacy_navigation.cjs --map bastion-keep --out native/tests/fixtures/n4/bastion-keep_navigation.bin
node tools/n2/legacy_reference.cjs --map bastion-keep --poses <poses.txt> --out <dir> --time 10
```

## N5 — weapons, effects, HUD (2026-11-10)

No new dependency. New library `cat_arena_fx` (GPU-free). New tests in the GPU-free build:
`combat_units`, `combat_legacy_tapes`, `weapon_fx_units`, `weapon_fx_legacy_tapes`, `hud_units`
(22 tests in total). Desktop adds `render_weapons_overlay`, `render_weapons_overlay_baseline` and
`render_weapon_view_{00,04,07,10}` (37 in total). The desktop app needs `assets/n5` next to
`assets/n1`; without it the weapon model, effects and HUD are not drawn (a warning is logged).

```sh
./build-core/cat_arena_combat_tests assets/n1 native/tests/fixtures/n5/helios_combat.bin
./build-core/cat_arena_fx_tests assets/n1 native/tests/fixtures/n5/helios_combat.bin native/tests/fixtures/n5/helios_weapon_fx.bin

./build-desktop/cat_arena                      # left mouse fire, R reload, Q / Tab swap, keys 1-4 change map
./build-desktop/cat_arena --list-weapons
./build-desktop/cat_arena --weapons 4,7 --seed 5 --no-targets
./build-desktop/cat_arena --offscreen 1280x720 --weapons 4,7 --walk "-:20,fire:3,-:120,swap:2,-:40,fire:3,-:60" --capture-every 10 --capture-dir out/
./build-desktop/cat_arena --no-overlay         # N4 picture
```

Re-creating the N5 assets and references (tooling only; Node >= 22 with Playwright Chromium):

```sh
node tools/n5/legacy_weapon_assets.cjs         # assets/n5 + native/include/cat/generated/weapon_fx_tables.inc
node tools/n5/legacy_combat.cjs --out native/tests/fixtures/n5/helios_combat.bin \
     --fx-out native/tests/fixtures/n5/helios_weapon_fx.bin --report combat.json --views-dir views/
```

## N5.1 — dynamic lights (2026-11-10)

No new dependency or build option. Desktop CTest adds `render_effect_frame_rocket_at_feet_moving`,
`render_effect_frame_rocket_and_grenade_jump` and `render_effect_frame_health_pickup` (40 tests).

```sh
./build-desktop/cat_arena --features -dlights          # lights off (legacy r_dynamiclight 0)
./build-desktop/cat_arena --offscreen 1280x720 --no-hud --combat-fixture native/tests/fixtures/n5/helios_combat.bin \
    --scenario rocket_and_grenade_jump --capture-ticks 44 --capture-dir out/
./build-desktop/cat_arena_image_compare native/tests/fixtures/n5/frames/rocket_and_grenade_jump_00044.png out/rocket_and_grenade_jump_00044.png 40
```

## N6 — enemies, waves, AI, animation (2026-11-11)

No new dependency or build option. `src/enemies.cpp` joins `cat_arena_physics`, `src/enemy_fx.cpp`
joins `cat_arena_fx`. GPU-free CTest adds `enemy_units`, `enemy_legacy_tapes_<map>` (4),
`enemy_fx_units`, `enemy_fx_legacy_animation_<map>` (4): 32 tests. Desktop adds
`render_enemy_lineup_{idle,windup,strike_final5,bodies,marines}` and five
`render_enemy_frame_<scenario>_<tick>`: 60 tests. The desktop app needs `assets/n6` next to
`assets/n1` to draw creatures (without it they are simulated, not drawn; a warning is logged).

```sh
./build-core/cat_arena_enemy_tests --units assets/n1
./build-core/cat_arena_enemy_tests assets/n1 helios-crucible native/tests/fixtures/n6/helios-crucible_enemies.bin
./build-core/cat_arena_enemy_fx_tests assets/n6
./build-core/cat_arena_enemy_fx_tests assets/n1 helios-crucible native/tests/fixtures/n6/helios-crucible_enemies.bin native/tests/fixtures/n6/helios-crucible_poses.bin

./build-desktop/cat_arena                                   # waves of enemies (default)
./build-desktop/cat_arena --wave 5 --shadows 3 --god        # all three flyer types, projected shadows, no damage
./build-desktop/cat_arena --third-person --god              # debug chase view with the local Marine Cat
./build-desktop/cat_arena --enemies practice                # the N5 practice targets instead
./build-desktop/cat_arena --offscreen 1280x720 --no-hud --enemy-fixture native/tests/fixtures/n6/helios-crucible_enemies.bin \
    --scenario fight_spin --capture-ticks 530,1300 --capture-dir out/
./build-desktop/cat_arena --offscreen 1280x720 --no-hud --enemy-lineup native/tests/fixtures/n6/lineup/lineup.txt \
    --lineup-shot windup --capture out/windup.png
```

Re-creating the N6 assets and references (tooling only; Node >= 22 with Playwright Chromium):

```sh
node tools/n6/legacy_enemy_assets.cjs        # assets/n6 + native/include/cat/generated/enemy_tables.inc
node tools/n6/legacy_enemies.cjs --map helios-crucible --out native/tests/fixtures/n6/helios-crucible_enemies.bin \
     --pose-out native/tests/fixtures/n6/helios-crucible_poses.bin --report helios.json
node tools/n6/legacy_enemies.cjs --map bastion-keep --scale 0.5 --out ... --pose-out ...   # same for the other maps
node tools/n6/legacy_enemies.cjs --map helios-crucible --only fight_spin --out tmp.bin \
     --frames "fight_spin:530,1300" --frames-dir frames/ --width 1280 --height 720
node tools/n6/legacy_enemies.cjs --map helios-crucible --lineup-dir lineup/ --width 1280 --height 720
```

## N6.5 — Windows x64 build and packages (2026-11-11)

Windows (on a Windows PC with Visual Studio 2022 or its Build Tools, "Desktop development with C++",
CMake >= 3.24, Git and Python 3 on PATH; no Vulkan SDK needed):

```bat
cd native
cmake --preset windows-release                 :: Visual Studio 17 2022, x64; fetches SDL3 3.2.10 and glslang vulkan-sdk-1.4.309.0
cmake --build --preset windows-release --parallel
ctest --preset windows-release                 :: tests without GPU / window (what CI runs)
ctest --preset windows-release-gpu             :: rendering and window tests: needs a Vulkan-capable GPU and driver
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS -DZIP=../dist/CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip -P tools/package/zip_package.cmake
```

The same steps run in GitHub Actions: `.github/workflows/windows-build.yml` (manual "Run workflow" with
a milestone name, or on pushes / pull requests touching native/, assets/ or the workflow). The
artifact `CAT_ARENA_FERAL_CLAW_<milestone>_WINDOWS` is the game folder.

What the Windows build does differently (all in native/CMakeLists.txt):
- static MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), also for SDL3 / glslang: no
  Visual C++ Redistributable needed;
- `vulkan-1.lib` is generated from `tests/vulkan_baseline_imports.txt` (cmake/cat_vulkan_import_library.cmake);
  `CAT_VULKAN_IMPORT_LIBRARY` can point at an SDK's library instead;
- `CatArena.exe`, GUI subsystem (`CAT_WINDOWS_GUI`, default ON; OFF gives a console program),
  `vulkan-1.dll` delay-loaded, 8 MiB main-thread stack (as on Linux), `/utf-8 /bigobj /permissive- /W4`;
- shaders: glslc / glslangValidator if installed, otherwise `CAT_FETCH_GLSLANG=ON` builds glslang.

Packages on any platform: `cmake --install <build> --prefix <folder>` makes the self-contained game
folder (executable, SDL3 runtime, `assets/shaders/*.spv`, `assets/n1 n2 n4 n5 n6`, README.txt =
README_WINDOWS.md, THIRD_PARTY_LICENSES.txt); the two scripts in native/tools/package check and zip it.
The executable finds its assets next to itself (SDL_GetBasePath), so it can be started from anywhere.

New tests: `windows_vulkan_import_library` (llvm-lib on non-Windows hosts), `window_input_selftest`,
`window_controls_selftest` (pause, resume, F11, F5, Esc twice). Tests that need a GPU or a window
carry the label `gpu`: `ctest -LE gpu` runs the rest.

Determinism across compilers (N6.5, after the first Windows runs): the simulation and effect
libraries call `cat::jsmath::sin / cos / tan / atan2 / pow` (include/cat/js_math.hpp), never the C
runtime's, and are built without FMA contraction (`-ffp-contract=off`, MSVC `/fp:precise`). Keep it
that way for any new code whose results are compared with the legacy game. `js_math_browser` checks
the functions against Chromium; `tools/js_math/gen_tables.py` regenerates their tables and
`tools/js_math/record_chromium_math.cjs` the Chromium fixture.

## N7 — menus, settings, HUD, audio (2026-11-12)

No new build dependency: the decoders (stb_vorbis 1.22, dr_mp3 0.7.4, stb_truetype 1.26) are in
`native/third_party`, the fonts and sounds in `assets/n7` (installed with the game). SDL3's audio
and gamepad subsystems are used; a missing audio device is not an error.

```
cmake --build build -j
ctest --test-dir build -R "audio_webaudio_parity|ui_menus_settings_hud"      # no GPU
ctest --test-dir build -R window_menu_selftest                                # window + Vulkan (label gpu)
# by hand
build/cat_arena                                   # main menu
build/cat_arena --map bastion-keep                # straight to the loading screen
build/cat_arena --game-settings my-settings.txt   # settings file of your choice
build/cat_arena --offscreen 1280x720 --ui-capture out/ui [--touch] [--map q3-gallery]
SDL_AUDIO_DRIVER=dummy build/cat_arena --menu-selftest   # exercise the audio device path without a sound card
```

Settings are saved in `SDL_GetPrefPath("CatArena", "FeralClaw")/settings.txt`
(Windows `%APPDATA%\CatArena\FeralClaw`, Linux `~/.local/share/CatArena/FeralClaw`).

Reference tools (not product; Node >= 22 with Playwright + Chromium, run against `legacy/index.html`):
```
node tools/n7/legacy_ui_capture.cjs                 # docs/n7-evidence/legacy-ui (pages + element rectangles)
node tools/n7/legacy_settings_defaults.cjs          # native/tests/fixtures/n7/legacy_settings.json
python3 -I native/tools/n7/extract_n7_assets.py     # assets/n7 audio + backdrop, N7_ASSET_MANIFEST.json
node native/tools/n7/record_webaudio.cjs            # native/tests/fixtures/n7 decode.json, graph.f32/json
python3 -I native/tools/n7/set_browser_lengths.py   # browser decode lengths into bank.txt
python3 -I native/tools/n7/font_manifest.py assets/n7/fonts
```
