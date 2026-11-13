# Cat Arena: Feral Claw — N8 multiplayer

The N8 Windows build adds four-player co-op using the built-in LAN relay or the
existing Cat Arena relay. The server browser lists live compatible rooms. The
host runs the enemies, waves, damage and revive rules. Guests receive world
snapshots and simulate their own traversal. Teammates appear as Marine Cats
with name tags.

## Play

Extract the Windows ZIP and run `CatArena.exe`. For LAN play, open
**Multiplayer → Host a Server**, select a map, then choose **Host on This PC**.
Guests open **Multiplayer → Join a Server**, enter the host's LAN IP and port
`8787`, and select the room. For an existing relay, enter its address in both
clients' multiplayer settings and use **Create Server** on the host. Hold
**F** near a downed teammate for four seconds. The squad restarts at wave 1
after every player is down.

See [README_WINDOWS.md](README_WINDOWS.md) for controls and troubleshooting.
See [docs/WINDOWS_ROADMAP.md](docs/WINDOWS_ROADMAP.md) for N9–N20, Windows
exclusivity, and the 480 MiB measurement target.

## Reproduce the Windows build

From the `native` directory on Windows x64 with Visual Studio 2022 C++ Build
Tools, the Windows SDK, CMake 3.24+, Git, and network access for the pinned
build dependencies:

```powershell
cmake --preset windows-release -DCAT_MILESTONE=N8
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows-release --config Release --prefix ../dist/CAT_ARENA_FERAL_CLAW_N8_WINDOWS
cmake -DPACKAGE_DIR=../dist/CAT_ARENA_FERAL_CLAW_N8_WINDOWS -DPLATFORM=windows -P tools/package/validate_package.cmake
```

Node.js and its `ws` module are needed only for the original-relay interop
tests. Pass the directory containing `ws` as `-DCAT_NODE_WS_DIR=...`. The
Windows build includes its own relay and has no Node.js runtime dependency.

## Verification

- **Implemented** — built-in and external hosting, browser/join, four-player
  snapshots, remote combat, health, revive, wipe, names and disconnect handling.
- **Compiled / Executed / Tested** — GitHub Windows x64 CI run
  [43359148828](https://github.com/dontaskwhojoeis200-boop/Cat-arena/actions/runs/43359148828)
  compiled the executable, validated the package and passed all 42 GPU-free
  tests, including native and WinHTTP co-op and the original Node.js relay.
  The packaged executable was launched for the expected no-GPU error smoke test.
- **Tested locally** — the untouched N7 archive passed 35 core tests. The N8
  GitHub source passed 39 GPU-free tests and the Helios and Reactor Bastion
  offscreen tests on Linux using software Vulkan.
- **Unverified** — interactive Windows play on the GTX 660, separate Windows
  machines, WAN/NAT, live Render and TLS/WebSocket interoperability. A room's
  map stays fixed for its lifetime. The 480 MiB, 60 FPS and 120 FPS targets
  are pending representative hardware measurement.

The Windows artifact contains the binary from GitHub commit
`a0fd083bd4f8d78bdbf81e19b9b19a8796a23d14`, SDL3, 23 SPIR-V shaders,
four maps, 16 weapon models, 20 creature meshes, UI, fonts, sound and notices.
The source archive adds the Windows roadmap and milestone documentation
to that build's source.
