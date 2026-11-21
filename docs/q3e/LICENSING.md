# Licensing — Cat Arena on Quake3e

Not legal advice; a record of what the sources say and what the migration has to respect.
Status: nothing from the migration branch has been distributed. The CI artifact
`CAT_ARENA_Q3E_ENGINE_WINDOWS_x64` is the unmodified upstream engine with its licence and is kept
in this private build pipeline.

## Components

| Component | Licence | Notes |
|---|---|---|
| Quake3e engine (`engine/quake3e`, dec793de) | GNU GPL v2 (`COPYING.txt`), id Software and contributors | Modified copies must carry prominent change notices and stay GPL v2 |
| Quake III game code (ioquake3 52088b9) | GNU GPL v2 | Imported into `game/ioq3` (stage B) as the base of `qagame` / `cgame`; files keep their headers; Cat Arena changes are marked `CAT_ARENA_STANDALONE` |
| libjpeg (bundled) | IJG licence (permissive, with notice) | `engine/quake3e/code/libjpeg/README` |
| libogg, libvorbis (bundled) | BSD-style (Xiph.org) | `COPYING` in each folder |
| libcurl (bundled **prebuilt static libraries** for MSVC / MinGW) | curl licence (MIT-style) | Prebuilt binaries of unrecorded version, **still linked into the stage-B Windows build** (upstream project settings). Cat Arena does not need in-game HTTP downloads: remove `USE_CURL` from the MSVC projects before any release (Q3E-F) |
| SDL2 (bundled MinGW libraries; not used by the MSVC build) | zlib licence | Linux builds here used SDL2 2.30 built from source |
| q3map2 (NetRadiant-custom 8216133, build tool only, not shipped) | GPL v2 | Built with a one-line GCC 13 fix (`tools/q3e/q3map2-gcc13.patch`). Tool output (BSP data) is not covered by the tool's licence |
| Liberation Mono Bold (`assets/n7/fonts`) | SIL Open Font License 1.1 | Rendered into `gfx/2d/bigchars.png`; the OFL allows embedding rendered glyphs; licence text to ship with the release |
| Cat Arena C++20 game code (`game/catarena`) | GPL v2 or later (decision D3) | Headers state it |
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
- Weapon textures taken unchanged from N10 / the legacy game (`assets/n5/weapons`): the ROCKET
  LAUNCHER and HAND CANNON textures carry an emblem that resembles the Quake logo (seen in the
  stage D part 4 pictures). The owner should confirm these textures may be shipped, or repaint
  the emblem.
- Map textures and models added in stage D part 5 (Helios Crucible, Bastion Keep, The Arsenal),
  all taken unchanged from N10's asset catalog (`assets/n1/catalog.json`, extracted in N1 from the
  legacy game archive): the Bastion kit (`models/bastion/BA_*.jpg`, `BA_08_glow.png`), the Arsenal
  textures (`textures/gallery/*`), the nebula sky box (`env/gallery_nebula_*`), the sky layers
  (`sky/*`) and the Arsenal MD3 props (`models/gallery/*.md3`, see the table above). The repository
  records no origin for them before the legacy game; the names (a numbered texture kit, "gallery")
  suggest a third-party kit or material modelled on Quake III maps. The owner should confirm the
  rights to each before a release. The baked sky boxes (`textures/catarena/sky/*_box_*.jpg`) are
  rendered by `tools/q3e/n10materials.py` from N10's sky mesh and its textures.
- Sounds and soundtrack (stage D part 6): rendered from N10's sample bank and the ASHEN DRIVE
  soundtrack (`assets/n7`, extracted in N7 from the legacy game, which states them as original;
  not independently verified, see `assets/n7/N7_ASSET_MANIFEST.json`). The owner confirms the rights
  before a release, as for the other assets.
