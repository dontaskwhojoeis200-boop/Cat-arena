# Cat Arena: Feral Claw — N10 gameplay checkpoint

N10 continues native gameplay parity on the N9 Windows baseline. It was done in two parts:
the gameplay work below (branch `n10-gameplay`), then a merge onto the final N9 (crash reports,
EXE resources, the WinHTTP close fix) with a review against the original game, two co-op rules
restored, the weapon tapes extended to all four maps and multiplayer re-validated (branch `n10`,
see [the development log](docs/DEVELOPMENT_LOG_N10.md)). The existing
maps, collision and navigation data, assets, and two relay options are carried
forward. See [N9](README_N9.md) for platform options and
[the acceptance matrix](docs/N10_ACCEPTANCE.md) for hardware checks.

## Gameplay fixes

- Co-op guests see host and teammate rockets, grenades and rift bolts in flight,
  plus their detonations. The firing guest retains its immediate local prediction.
  The additional messages are optional to older clients and use the same relay
  protocol. Existing relay routing and room capacity still apply. The host
  includes up to 96 in-flight explosives in each snapshot. Collision, damage,
  and scoring remain authoritative on the host.
- Guest frags go to the shooter. The host no longer receives the kill or hit
  marker for a guest's hit, including a projectile from a departed guest. Wave
  kill progress still counts team kills.
- Remote gunfire shows the Marine Cat's muzzle flash and dynamic light,
  with tracers or beams for the appropriate weapons. Cosmetic traces use
  the shooter's last known pose and the existing collision query. They leave
  damage and kill decisions on the host. The host also hears guest gunfire.
- HUD combat feedback shows damage, critical hits, and the most recent incoming
  hit. Solo and host hits include direction when available. Guests show the
  health loss from the host's snapshot. The co-op downed notice now reflects
  the revive mechanic.
- Fog strength, lightmap intensity and overbright settings take effect during
  play. The standard values match the previous renderer.
- Effects detail, marks, brass, blood, gibs and rail trail duration now apply
  during play. The normal preset retains the existing settings.

- The Arsenal renders the original animated flag and two tag-attached turrets.
  The turrets turn toward the player and cycle their barrel animation in range.
- The SDL input self-test schedules its events on simulation ticks and checks
  sprint, slide, jump and crouch during the intended traversal sequence.

## Review and parity checks (branch `n10`)

- Enemy shots aimed at a teammate follow the original game again: a shot at a downed teammate
  stops on their body without damage, and a shot at a player who left stops on the host without
  damage (the first N10 build let both fly through).
- A guest's kill counts only in that guest's frags. The original host counted every team kill in
  its own score; this is kept as a deliberate difference and listed in the known limitations.
- The 29 weapon scenarios recorded from the original game now cover all four maps (Helios Crucible
  since N5; Bastion Keep, Reactor Bastion and The Arsenal added): all 3 × 18,220 new ticks are bit-identical to the original, with no code change needed.
- Multiplayer after the protocol additions: cross-play with the original browser game 13/13, an
  hour-long four-player soak, the original server.js relay and the window co-op self-test pass.
- The 17 settings that are still stored only are renderer features (texture filtering, MSAA,
  scene resolution, mirrors, culling modes …). Their in-game notes now name the tier that builds
  them (N13, N14 or N16), after the memory instrumentation of N12.

## Build

On Windows x64, with Visual Studio 2022 C++ Build Tools, the Windows SDK,
CMake 3.24+, and Git:

```powershell
cd native
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N10_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N10_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
```

Extract the Windows ZIP and run `CatArena.exe`. The game needs a graphics
driver with Vulkan 1.1 or newer. Follow the N8 or N9 instructions for hosting
and joining. The first-person weapon and third-person Marine Cat remain
separate.

## Acceptance still open

Physical Windows gameplay, live relay over the internet, and the GTX 660
driver still need validation. The live mirror and portal feeds and some rendering controls remain listed in
[FEATURE_PARITY.md](docs/FEATURE_PARITY.md). N11's performance baseline and
the measurement against the 192 MiB memory target remain open.

See [the test report](docs/N10_TEST_REPORT.md) for the commands, evidence, and
verified scenarios for this checkpoint.
