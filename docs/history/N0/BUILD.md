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
