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

The engine is branded (`CAT_ARENA_STANDALONE`): base folder `catarena`, no id data. The game
modules and the package are built as below; CI does all of it (`q3e-windows.yml`).

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

## Game modules (stage B)

`game/` is a CMake project: `qagame` and `cgame` (ioquake3 GPL game code in C + Cat Arena C++20),
`ui` (Cat Arena C++20). Output names follow Quake3e: `qagamex86_64.dll` / `.so` etc.

```
cmake -S game -B build-game -G "Visual Studio 17 2022" -A x64      # Windows
cmake --build build-game --config Release
cmake -S game -B build-game -DCMAKE_BUILD_TYPE=Release              # Linux
cmake --build build-game -j8
```

## Base data and maps

```
python3 tools/q3e/make_base.py          # charset, textures, crosshairs, shaders, default.cfg, maps/*.map (needs Pillow)
tools/q3e/build_maps.sh <q3map2>        # .map -> .bsp (BSP -meta, VIS, LIGHT); the .bsp is committed
```

q3map2 comes from NetRadiant-custom (GPL), commit `8216133984031afaa9a857b56ea66dd9c3d54b26`:

```
# deps here: libjpeg-turbo 3.0.4 (f29eda64) and libxml2 v2.13.5 (de918d45) built static into $P
git apply tools/q3e/q3map2-gcc13.patch            # GCC 13 rejects one designated initializer
make BUILD=release ASSIMP_INTERNAL=yes DEPENDENCIES_CHECK=off PKG_CONFIG_PATH=$P/lib/pkgconfig \
     CPPFLAGS_JPEG=-I$P/include LIBS_JPEG=$P/lib/libjpeg.a binaries-q3map2
# result: install/q3map2 (+ libassimp_.so next to it)
```

On Ubuntu with packages: `apt install libglib2.0-dev libxml2-dev libjpeg-dev libpng-dev` and the
same `make` line without the dependency overrides.

## Package

```
python3 tools/q3e/package.py --engine-dir <engine output> --client quake3e-vulkan.x64.exe \
    --server quake3e.ded.x64.exe --modules-dir build-game/Release --out dist/CatArena
```

Layout: `CatArena.exe`, `CatArena-server.exe`, `catarena/pak0.pk3` (built reproducibly from
`game/base/catarena` without map sources), `catarena/*x86_64.dll`, `COPYING.txt`, `README.txt`,
`SOURCE.txt` (commit of the build). On Linux add `--renderer quake3e_vulkan_x86_64.so`.

## Models and gameplay tests (stage C)

```
python3 tools/q3e/mesh2md3.py assets/n5/weapons/00_nailgun.gun game/base/catarena/models/catarena/weapons/nailgun.md3 models/catarena/weapons/nailgun 10
python3 tools/q3e/mesh2md3.py assets/n6/enemies/02_fiend.emesh game/base/catarena/models/catarena/enemies/fiend.md3 models/catarena/enemies/fiend 32 -24
CatArena-server +set dedicated 1 +map ca_test +ca_selftest +quit          # server self-test
python3 tools/q3e/gameplay_test.py write <homepath>/catarena/catest.cfg
CatArena +set ca_waves 0 +devmap ca_test +wait 150 +exec catest.cfg       # scripted run, then:
python3 tools/q3e/gameplay_test.py check <console log>
```

## Converted maps (stage D)

```
python3 tools/q3e/n10map2q3.py . reactor-bastion ca_reactor     # .map + world OBJ + shaders + textures
python3 tools/q3e/n10map2q3.py . helios-crucible ca_helios      # (stage D part 5) + material mesh OBJ
python3 tools/q3e/n10map2q3.py . bastion-keep ca_bastion
python3 tools/q3e/n10map2q3.py . q3-gallery ca_arsenal
tools/q3e/build_maps.sh <q3map2> ca_reactor ca_helios ca_bastion ca_arsenal
# BSP / VIS / LIGHT, then LIGHT again scaled to N10's lightmap brightness: about 1 min for
# Reactor Bastion and The Arsenal, 4-5 min each for Helios Crucible and Bastion Keep (130-160k
# material triangles). Needs numpy and Pillow. The .bsp files are committed.
python3 tools/q3e/map_test.py write . reactor-bastion ca_reactor <homepath>/catarena/maptest.cfg
CatArena +set ca_waves 0 +devmap ca_reactor +wait 150 +exec maptest.cfg   # then:
python3 tools/q3e/map_test.py check . reactor-bastion ca_reactor <console log>
```

## Weapons (stage D, part 3)

```
python3 tools/q3e/convert_weapons.py      # 16 N10 viewmodels -> MD3 + textures, cgame model / muzzle table
python3 tools/q3e/make_base.py            # also writes the effect textures and shaders (glow, beam)
CatArena-server +set dedicated 1 +map ca_reactor +ca_selftest +quit       # horde + weapon self-test
python3 tools/q3e/weapons_test.py write <homepath>/catarena/weapons.cfg
CatArena +set ca_waves 0 +set ca_loadout "0 1" +devmap ca_test +wait 150 +exec weapons.cfg   # then:
python3 tools/q3e/weapons_test.py check <console log>
```

Enemies (stage D, part 4):

```
python3 tools/q3e/convert_enemies.py      # 20 N10 creature meshes (.emesh) + base textures + shaders
python3 tools/q3e/n10map2q3.py . reactor-bastion ca_reactor && tools/q3e/build_maps.sh <q3map2> ca_reactor
python3 tools/q3e/enemies_test.py write <homeA>/catarena/lineup.cfg <homeB>/catarena/waves.cfg
CatArena +set fs_homepath <homeA> +set ca_waves 0 +set ca_log 1 +devmap ca_test +wait 150 +exec lineup.cfg
CatArena +set fs_homepath <homeB> +set ca_waves 0 +set ca_log 1 +set ca_loadout "11 0" +devmap ca_reactor +wait 150 +exec waves.cfg
python3 tools/q3e/enemies_test.py check <homeA log> <homeB log>
```

`ca_lineup [off]` (cheat) puts all 20 types in rows in front of the player, held still, playing
their attack wind-up every 2.5 s; `ca_log 1` prints enemy spawns, hits on the player and kills;
`ca_enemies` lists the living enemies.

Test cvars and commands (cheats, `devmap`): `ca_loadout "a b"` fixes the two spawn weapons (N10
indices 0-15; empty = random, as N10); `ca_give n` puts weapon n in hand; `ca_place x y z yaw
[pitch]`; `ca_state` prints the loadout (`slot w0 w1 inhand clip clip2`) and `vz`.
