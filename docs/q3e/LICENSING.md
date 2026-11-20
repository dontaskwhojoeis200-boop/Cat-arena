# Licensing — Cat Arena on Quake3e

Not legal advice; a record of what the sources say and what the migration has to respect.
Status: nothing from the migration branch has been distributed. The CI artifact
`CAT_ARENA_Q3E_ENGINE_WINDOWS_x64` is the unmodified upstream engine with its licence and is kept
in this private build pipeline.

## Components

| Component | Licence | Notes |
|---|---|---|
| Quake3e engine (`engine/quake3e`, dec793de) | GNU GPL v2 (`COPYING.txt`), id Software and contributors | Modified copies must carry prominent change notices and stay GPL v2 |
| Quake III game code (id 2005 release / ioquake3) | GNU GPL v2 | Used only as reference and for the module interface unless code is copied into `game/`; any copied file keeps its header and licence |
| libjpeg (bundled) | IJG licence (permissive, with notice) | `engine/quake3e/code/libjpeg/README` |
| libogg, libvorbis (bundled) | BSD-style (Xiph.org) | `COPYING` in each folder |
| libcurl (bundled **prebuilt static libraries** for MSVC / MinGW) | curl licence (MIT-style) | Prebuilt binaries of unrecorded version. Cat Arena does not need in-game HTTP downloads: plan is to build with curl disabled (`USE_CURL=0` / no `USE_CURL` define) so no opaque binary is linked |
| SDL2 (bundled MinGW libraries; not used by the MSVC build) | zlib licence | Linux builds here used SDL2 2.30 built from source |
| q3map2 (planned map compiler, build tool only) | GPL v2 | Tool output (BSP data) is not covered by the tool's licence |
| Cat Arena code (`native/`, tools) | **No licence file in the repository** | See below |
| Cat Arena assets (textures, models, sounds, music, maps) | Owner's | Provenance per asset in `assets/*/PROVENANCE*`; the Arsenal MD3 props (`assets/n1/models/md3`, internal paths `models/gallery/*.md3`) have no recorded origin — confirm before packaging |

## What the GPL means for Cat Arena

- The engine executable is a modified Quake3e: whoever receives the game must be able to get the
  complete corresponding source of that executable (GPL v2 §3), including Cat Arena's changes.
- The game modules (`qagame`, `cgame`, `ui` DLLs) are loaded into the GPL engine through its
  module interface; the Quake III community and id's own release treat game code as GPL, and any
  module built from GPL game code is GPL. Planned Cat Arena modules contain Cat Arena's gameplay
  code (ported from `native/`), so **distributing the game means publishing that code under GPL v2
  as well**. Decision D3 (2026-11-19): the owner accepts GPL v2 for the code; assets keep the
  owner's terms.
- Data files (textures, sounds, music, maps in `.pk3`) are not program code; they can stay under the
  owner's own terms, as id did with the Quake III data.
- No id Software data (`baseq3/pak*.pk3`, Quake III art, sounds, maps) is used or shipped. The base
  folder is `catarena`; `FS_CheckIdPaks` only inspects `baseq3` / `demoq3` and is not triggered.
- Renaming the executable changes nothing about these obligations.

## Release checklist (for Q3E-F)

- `COPYING.txt` (GPL v2) and a third-party notices file in the package.
- Source offer or source archive for the exact engine + module commit of the build.
- Change notices: `engine/quake3e/UPSTREAM.txt` + `git log <import>..HEAD -- engine/quake3e`.
- Asset provenance confirmed for every shipped file.
