# Step 02 — reference build report

Phase 0 / Step 02. Checkpoint **migration-0.2-reference-blocked**. Status: **BLOCKED**.
A reference configuration and capture tooling exist; no successful game execution, representative game screenshots or performance baseline has been obtained. The acceptance criterion is not met. Do not advance to Step 03 on the strength of this report.

## Reference identity and new source evidence

The baseline remains the unmodified v0.17 CAT MARINE game at `legacy_web/index.html`, 18,320,927 bytes, SHA-256 `e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393`, internal build `VA-MODULAR-RB-011`. It matches the HTML in the audited APK and the newly available Render-Multiplayer archive byte for byte. Original Step 01 inputs remain unchanged.

The two additional packages are preserved under `reference/step02/`; their hashes are in `baseline/evidence/additional_sources.json`.

- Render-Multiplayer supplies a matching four-player relay, build validation and four protocol map IDs ending in `-v1`. This resolves the Step 01 missing-current-relay-source finding. It does not establish end-to-end multiplayer compatibility.
- The newer v0.18 mouse/settings ZIP is a patch/build project with an empty game directory, not a finished playable successor. Its Python patch expects the v0.17 HTML; it adds Android pointer capture, settings layout and keyboard navigation. It remains unapplied to preserve the audited baseline. Its Android wrapper is still WebView-based, package `com.catarena.feralclaw`, SDK target 35, unlike the audited APK identity. Electron/Gradle workflows are supplied but unbuilt here. Windows workflow copies into an absent `desktop/game` directory; it needs that directory created when exercised. Dependencies are not locked. This is a separate unverified branch, not a native migration deliverable.

## Actual environment and attempted execution

See `baseline/evidence/environment.json`, `attempts.json` and `browser_launch_failure.txt` for evidence. Linux x86_64, kernel 6.18.57, glibc 2.41; visible CPU Intel Xeon Platinum 8573C (9 logical CPUs reported). CPU quota, physical GPU, driver and usable memory budget were not established. `/dev/dri` is absent in this container; that is not a physical host GPU inventory. Installed Node is 24.22.1; Chromium executable reports 156.0.8078.79; Playwright is 1.62.1.

Local Chromium terminated before navigation with `socket() failed: Operation not permitted`. The diagnostic attempt requested SwiftShader but it never initialized; no software-rendering benchmark occurred. Managed-browser HTTP navigation reached an authentication error, not the game. Native desktop text entry failed because AT-SPI was unavailable. Managed-browser local-file navigation was rejected by the browser URL security policy; no workaround was attempted after rejection. The temporary static server was stopped. No Android device, adb or emulator was available.

| Area | Result | Remaining evidence |
|---|---|---|
| Startup / loading | BLOCKED before game execution | Loaded menu, console and loading completion |
| Map selection / rendering | UNVERIFIED, all four maps | Representative menu, world and vertical-geometry screenshots |
| Keyboard, mouse, movement, combat | UNVERIFIED | Input responses, trajectories, damage and weapon behavior |
| Enemy spawning / waves | UNVERIFIED | Initial enemies, combat and wave progression |
| HUD, menus, settings | UNVERIFIED | Navigation, pause/resume, return, repeated settings changes |
| Multiplayer | Mock relay smoke test PASSED | Real WebSocket connections and gameplay sessions |
| Audio | UNVERIFIED | Human listening and playback/pause checks |
| Android | NOT BUILT / NOT INSTALLED / NOT RUN | Device identity, installation, touch and lifecycle tests |
| Vulkan validation | Not applicable | This reference uses WebGL; native engine not started |
| FPS / CPU / GPU / memory benchmarks | NOT MEASURED | Controlled actual runs |

## Configured reference conditions (not executed)

`tools/baseline/reference_config.json` pins the source hash and four existing map preview camera poses. Map URLs use `helios-crucible`, `bastion-keep`, `reactor-bastion`, `q3-gallery`; these differ from wire protocol IDs with `-v1` suffixes. Viewport 1280×720, device pixel ratio 1; target actual drawing buffer 1280×720. Normal preset, adaptive resolution off, MSAA 0, FXAA off, FOV 105, balanced culling, in-game cap value 241 (unlimited), VSync preference on. Browser compositor pacing may still cap FPS. Preserve these same settings in future comparisons; this config is not evidence of runtime application or a graphical parity claim.

Optional proposed sampling: 10-second warmup then three 60-second samples per map at fixed authored camera poses, zero enemies, simulation paused through the existing settings gate without its overlay. This is explicitly a static rendering scenario, never a combat benchmark. The driver alters in-memory state only. It has not been runtime validated; residual effect state and random procedural state require review before treating runs as reproducible. Combat needs a separately documented fixed scenario and controlled spawning/randomness, without changing enemy counts between implementations.

The driver records raw requestAnimationFrame intervals; average FPS is frame count / elapsed time, and 1% / 0.1% lows use the reciprocal of the mean slowest fraction of intervals. It withholds lows with insufficient samples. These are browser callback pacing measurements, not proof of GPU rendering throughput. In-game CPU readings are rolling samples, GPU time is optional, JS heap is not process RAM, tracked GPU allocation is an estimate rather than hardware consumption or device budget. No values are reported in this checkpoint.

## Reproduction on a supported development machine

From the project root, serve the unchanged HTML for manual reference testing:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory legacy_web
```

Open `http://127.0.0.1:8000/index.html?map=helios-crucible` in a local hardware-accelerated browser. Record browser/OS versions, GPU and driver, monitor refresh, power/thermal conditions, render settings, drawing-buffer resolution and source hash. Use a fresh profile with no extensions and keep the window foregrounded. Do not compare results from software and hardware rendering.

For the proposed capture driver:

```sh
cd tools/baseline
npm install
npx playwright install chromium
npm run check
npm run capture
node capture.cjs --measure
```

Playwright is pinned to 1.62.1; no dependency lockfile was generated or dependency installation tested in this checkpoint. Preserve the generated lockfile and browser revision for any validated run. `CHROMIUM_PATH` can select an installed browser; `BASELINE_OUTPUT` selects a result directory. Default is headed browser rendering. `BASELINE_HEADLESS=1` is available for diagnostics but must be labeled as a different configuration. Reports and screenshots are written under `docs/baseline/runs/` by default. A capture result requires manual review; movement/fire observations are not correctness assertions. This driver is IMPLEMENTED and syntax-checked, not runtime-tested.

The supplied relay test expects `tests/relay_smoke_test.js` adjacent to `relay/server.js`, whereas the downloaded ZIP placed both files at root. Copies under `tools/baseline/` restore the expected layout without changing the supplied files:

```sh
node tools/baseline/tests/relay_smoke_test.js
```

This test passed here: create room, HTTP discovery, join, incompatible-map rejection, guest-to-host payload relay and HTTP health. WebSocket objects are mocked; it does not establish real network handshakes, four-client capacity, damage, revive, enemy synchronization or device compatibility. It opens a temporary HTTP listener on port 19284. For actual relay use, copy `legacy_web/index.html` to `tools/baseline/relay/game.html`, run `npm install` there, then `npm start`; defaults to port 8787. No relay deployment or live session was performed.

## Manual verification required to close this step

1. Start from fresh settings. Capture main menu, map picker, loading, gameplay and pause screens. Record startup duration and console errors.
2. For each of the four maps, capture the configured view plus ramps, stairs, bridges and overlapping floors. Record collision observations and lighting/fog/material appearance without simplifying geometry.
3. Exercise every source-defined movement action and binding shown by the game, mouse capture/release, weapon selection, hitscan/projectile/explosion damage and blast movement. Record inputs and observations; numerical trajectories belong to Step 08.
4. Observe enemies, flying enemies and wave transitions. Record enemy counts and randomness; do not substitute a zero-enemy scene for combat testing.
5. Exercise every menu/settings tab, pause/resume, return to menu, resolution changes, audio controls and mobile HUD customization. Listen to effects and music; muted browser execution cannot validate audio quality.
6. Run real host/join sessions for two and four clients using the same map/build. Verify player/enemy/damage/revive synchronization, disconnect and reconnection behavior. Preserve logs with device/client identities.
7. Install the preserved legacy APK on a compatible test device only after checking package/signature compatibility. Record model, Android/WebView versions, display density, graphics settings, touch input, audio, orientation and background/resume. Do not uninstall an existing app automatically to solve a signing mismatch.
8. Validate the capture driver and controlled scene state, then collect benchmark samples with fixed hardware/settings. Archive raw measurements and screenshots. Mark Step 02 complete only once a repeatable executed reference configuration exists; explicitly retain unavailable platform coverage as limitations.

No representative screenshot or new executable/APK is included because none was produced successfully. The source checkpoint contains original binaries as reference inputs only.
