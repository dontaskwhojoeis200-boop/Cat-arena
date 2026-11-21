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
CatArena +set ca_waves 0 +set ca_loadout "0 1" +devmap ca_test +wait 150 +exec catest.cfg   # scripted run, then:
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

Waves on the other maps (stage D part 5): `enemies_test.py write <lineup.cfg> <waves.cfg> . <N10 map id> [<god off at shot>]`
picks two far-apart N10 spawn spots open to the sky.

## Sounds (stage D, part 6)

```
python3 tools/q3e/convert_sounds.py .     # needs a C++20 compiler ($CXX, default g++) and ffmpeg with libvorbis
# builds tools/q3e/sound_bake.cpp against N10's audio code and renders the 48 N10 samples into
# 86 WAV files (sound/catarena, N10 levels / bus levels / voice tones and filters baked in),
# a silent sound/feedback/hit.wav, and the soundtrack as music/ashen_drive.ogg. All committed.
python3 tools/q3e/sound_test.py write <dir>          # levels.cfg, music.cfg, sounds.cfg
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=levels.raw CatArena +set fs_homepath <homeL> +set ca_music 0 +set ca_waves 0 +set ca_log 1 +set s_show 1 +devmap ca_test +wait 150 +exec levels.cfg
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=music.raw  CatArena +set fs_homepath <homeM> +set ca_music 1 +set ca_waves 0 +devmap ca_test +wait 150 +exec music.cfg
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=sounds.raw CatArena +set fs_homepath <homeS> +set ca_music 0 +set ca_waves 0 +set ca_log 1 +set s_show 1 +devmap ca_test +wait 150 +exec sounds.cfg
python3 tools/q3e/sound_test.py check . levels.log levels.raw music.log music.raw sounds.log sounds.raw
```

SDL's `disk` audio driver writes the mixed output to the file (16-bit stereo, 44.1 kHz), so the
check measures what Quake III plays. Audio cvars: `s_volume` (N10 master, default 0.7),
`s_musicvolume` (N10 music x master, default 0.322), `ca_music 0` (no soundtrack), `s_khz 44`.

## Medkits (stage D, part 7)

```
python3 tools/q3e/make_base.py            # also writes textures/catarena/medkit.png (N10 atlas tile 11) and its shader
CatArena-server +set dedicated 1 +map ca_reactor +ca_selftest +quit      # includes N10's medkit rule (11 checks)
python3 tools/q3e/medkit_test.py write . <homepath>/catarena/medkit.cfg
SDL_AUDIODRIVER=dummy CatArena +set fs_homepath <homepath> +set ca_waves 0 +set ca_log 1 +devmap ca_reactor +wait 150 +exec medkit.cfg
python3 tools/q3e/medkit_test.py check <console log> <homepath>/catarena/medkit.cfg
```

`ca_health <1-100>` (cheat) sets the player's health; `ca_log 1` prints every medkit taken.

## Co-op rules (stage E, part 1)

```
python3 tools/q3e/coop_test.py write <homepath>/catarena/coop.cfg
CatArena-server +set fs_homepath <homepath> +set dedicated 1 +set ca_waves 0 +set ca_log 1 +map ca_test +exec coop.cfg
python3 tools/q3e/coop_test.py check <server log>
python3 tools/q3e/coop_test.py view <homepath>/catarena/view.cfg        # pictures of the co-op HUD
SDL_AUDIODRIVER=dummy CatArena +set fs_homepath <homepath> +set com_maxfps 60 +set ca_waves 0 +set ca_log 1 +devmap ca_test +wait 150 +exec view.cfg
```

Server test commands (cheats not needed; for tests only): `ca_addplayer <name>` (a scripted player
in a bot slot, no bot AI), `ca_removeplayer <client>`, `ca_input <client> <forward> <right> <up>
<buttons>` (buttons: 1 attack, 4 use = F), `ca_moveplayer <client> <x> <y> <z> <yaw>`, `ca_hurt
<client> <damage>` (through `G_Damage`), `ca_coopstate` (one `CA_COOP` line per player). With
`ca_log 1` the server prints downs, revives and wipes, the client each new status line.

## Four clients and network conditions (stage E, part 3)

```
python3 tools/q3e/net_test.py run  <package dir> <out dir> [--delay MS] [--loss PCT]
python3 tools/q3e/net_test.py soak <package dir> <out dir> --minutes 60 --map ca_reactor
```

Starts a dedicated server and four clients (320 x 240 windows at 30 fps; a display and a Vulkan
driver are needed: lavapipe on Xvfb here) on 127.0.0.1:27960. `--delay` sets `cl_packetdelay` on
every client and `sv_packetdelay` on the server, `--loss` sets `net_dropsim` on every process. The
verdict is the last line of `<out dir>/checks.txt`; `data.json` / `soak.json` keep the figures.
Client console commands for this: `ca_netstat` (one `CA_NET` line: ping, entities in the current
and the largest snapshot, snapshots received and lost, prediction misses, the players seen);
server: `ca_entcount` (`CA_ENTS`: the entities a snapshot could carry now and the peak, counted
every server frame).

The server defaults are now 4 players (`sv_maxclients 4`, N10's room size) and 4 connections per
address (`sv_maxclientsPerIP 4`; Quake3e's default of 3 turned the fourth player of a household
away).

## Stage D review: everything at once

```
python3 tools/q3e/run_stage_d.py . <package dir> <out dir> [selftest coop gameplay weapons lineup waves map sound medkit views]
CatArena-server +set dedicated 1 +map ca_reactor +ca_fitsurvey 20 +quit     # Quake III player box vs N10's player
```

The runner writes every log, check and picture into `<out dir>` and a `summary.txt`; all tests take
about 95 minutes on two cores (needs a display, a Vulkan driver and ffmpeg). Without arguments it
runs everything.
