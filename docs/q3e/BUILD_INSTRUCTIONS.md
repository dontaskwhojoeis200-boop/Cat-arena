# Build instructions — Quake3e foundation (N10-Q3E)

The N10 game itself keeps building as before (docs/BUILD.md, branch `n10`).

## Windows x64 (reference build, CI)

Workflow `.github/workflows/q3e-windows.yml` (runs on pushes to `migration/**` that touch
`engine/` or the workflow). Locally, from a "x64 Native Tools Command Prompt for VS 2022":

```
cd engine\quake3e
msbuild code\win32\msvc2017\quake3e-ded.vcxproj -p:TargetName=quake3e.ded,Configuration=Release,Platform=x64 /p:PlatformToolset=v143
msbuild code\win32\msvc2017\renderervk.vcxproj -p:Configuration=Release,Platform=x64 /p:PlatformToolset=v143
msbuild code\win32\msvc2017\quake3e.vcxproj -p:TargetName=quake3e,Configuration=Release,Platform=x64,UseWasapi=USE_WASAPI=0 /p:PlatformToolset=v143
```

Output: `code\win32\msvc2017\output\quake3e.exe` (client, Vulkan renderer linked in) and
`quake3e.ded.exe`. The order matters: the client project links whichever renderer library was
built last (upstream's CI does the same).

Run without id data: put the executable next to a `catarena` folder containing `default.cfg`, then
`quake3e.exe +set fs_basegame catarena`. Until the Cat Arena modules exist (Q3E-B) it stops at
"VM_Create on UI failed".

## Linux (development host, lavapipe)

Packages cannot be installed in the development container, so two dependencies were built or
fetched from source:

```
# SDL2 2.30 (libsdl-org/SDL release-2.30.x, 0aef2c36)
cmake -S SDL -B sdl2-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/deps/sdl2-install -DSDL_TEST=OFF
cmake --build sdl2-build -j8 && cmake --install sdl2-build
# OpenGL headers (needed by the client's GL loader even for Vulkan): NVIDIA/libglvnd include/ (606f6627)
export PATH=$HOME/deps/sdl2-install/bin:$PATH PKG_CONFIG_PATH=$HOME/deps/sdl2-install/lib/pkgconfig
make -C engine/quake3e -j8 USE_SDL=1 USE_CURL=0 CFLAGS="-I$HOME/deps/libglvnd/include"
```

On a normal Ubuntu machine: `sudo apt install libsdl2-dev mesa-common-dev` then
`make USE_CURL=0` (upstream BUILD.md).

Output in `engine/quake3e/build/release-linux-x86_64/`: `quake3e.x64`, `quake3e.ded.x64`,
`quake3e_vulkan_x86_64.so`, `quake3e_opengl_x86_64.so`. Start with
`+set cl_renderer vulkan +set fs_basegame catarena`.

## Audit probes

`docs/q3e/tools/mapstats.cpp` and `meshstats.cpp` print the collision and mesh sizes of the N10
maps (SOURCE_AUDIT §3). Build against an N10 build directory:

```
g++ -std=c++20 -Inative/include docs/q3e/tools/mapstats.cpp <build>/libcat_arena_physics.a <build>/libcat_arena_assets.a <build>/libcat_arena_core.a -o mapstats
./mapstats assets/n1 helios-crucible bastion-keep reactor-bastion q3-gallery
```
