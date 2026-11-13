# Cat Arena: Feral Claw — N10 gameplay checkpoint

N10 continues native gameplay parity on the N9 Windows baseline. The existing
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
- HUD combat feedback shows damage, critical hits, and the most recent incoming
  hit. Solo and host hits include direction when available. Guests show the
  health loss from the host's snapshot. The co-op downed notice now reflects
  the revive mechanic.
- Fog strength, lightmap intensity and overbright settings take effect during
  play. The standard values match the previous renderer.

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
driver still need validation. The older mirror and portal feeds, Arsenal MD3
props and some rendering controls remain listed in
[FEATURE_PARITY.md](docs/FEATURE_PARITY.md). N11's performance baseline and
the 480 MiB acceptance measurement remain open.

See [the test report](docs/N10_TEST_REPORT.md) for the commands, evidence, and
verified scenarios for this checkpoint.
