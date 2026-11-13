# Current and historical known limitations

N10 checkpoint ([test report](N10_TEST_REPORT.md))
- Physical Windows GPU play, exclusive fullscreen, raw input, playback-device changes, and four-player LAN/WAN are open. Windows CI covers GPU-free loopback. Linux software Vulkan covers the co-op UI and rendering path.
- The HUD now draws damage numbers and incoming hit feedback. Guest health feedback reflects the change between host snapshots and has no source direction. Fog strength, lightmap intensity, and overbright are active.
- Live mirrors and portals, Arsenal MD3 props, internal scene resolution, anti-aliasing, and several texture and effects controls remain open. No FPS or combined memory result exists for the Pentium G2020 and GTX 660.

Menus, settings, HUD, audio (N7, docs/N7_UI_AUDIO.md)
- 25 of the 65 legacy settings are shown and saved but do not change the game yet (N10): scene
  resolution, multisample AA, FXAA, adaptive resolution and FPS target, the three world culling
  modes, texture quality / bits / filter / anisotropy, gamma, overbright, flares, effect detail,
  marks, brass, blood, gibs, rail trail, lightmap filter / intensity, curve detail, detail textures,
  mirrors, portal resolution. The renderer draws at window resolution without anti-aliasing.
  Partial: lighting (lightmap only), fog (off / standard), sky (clouds on / off), preset, RAM rows.
- Not heard on a real sound card: the build machine has no audio device. The mixer matches
  Chromium's Web Audio on recorded graphs (docs/N7_UI_AUDIO.md §2) and the SDL device path ran with
  SDL's dummy driver; loudness, latency and drivers on Windows / Android are unverified.
- Sound bank decoding and resampling take about 1 s at start (before the first menu frame).
- Sound has its own random stream (the legacy's sound calls drew from the gameplay generator):
  a deliberate difference that keeps replays independent of audio.
- Fonts: Liberation (metric-compatible with Times New Roman / Arial / Courier New) instead of the
  browser's choice among Noto Serif / Georgia / system fonts; glyph shapes differ slightly.
- N8 multiplayer uses the native or original Cat Arena relay. Separate PCs, live Render, WAN/TLS, and physical Windows graphics/audio are unverified.
- Android pointer capture, touch controls and safe areas are unit-tested and drawn at phone sizes,
  in the historical desktop tests. Touch input on desktop Linux was not exercised with a
  real touch screen.
- Gamepad support is a native addition (the legacy has none); it was not tested with a real pad.
- The menu shell (interactive runs, `--menu-selftest`) uses the N7 HUD; the automation paths
  (`--frames`, `--input-selftest`, `--controls-selftest`, offscreen captures) keep the N5 HUD so that
  earlier picture comparisons stay valid.
- In the build tree the shaders next to cat_arena are copied when cat_arena is relinked; after a
  shader-only change pass `--shaders <build>/shaders` (installed packages are not affected).

Windows (N6.5)
- CatArena.exe is built, packaged and tested (GPU-free tests) by GitHub Actions, but has not been run
  on Windows with a GPU: rendering, window, input, fullscreen and performance on Windows are unverified.
  GitHub-hosted runners have no GPU.
- The executable is not code-signed (SmartScreen warns). F11 fullscreen was not confirmed anywhere.
- Math: sin / cos / tan / atan2 / pow are correctly rounded like Chromium; Chromium's pow (glibc's on
  Linux) differs from the correctly rounded value in about 1 of 30,000 arguments, so such a pow result
  can differ by 1 ulp from the legacy game. Helios enemy replay: 790 of 19,200 ticks differ from the
  legacy by at most 1.8e-15 (not yet traced).

Enemies (N6)
- The route search stops after 110 expansions per tick; the legacy additionally stops after 1.8 ms
  of wall time (machine-dependent). The recordings pin the legacy clock; see docs/N6_ENEMIES.md §3.
- Pictures were compared on Helios Crucible only (21 tape frames, 5 staged scenes). On the other
  maps the simulation, animation and effect random draws are compared, not pictures.
- Not compared with a legacy picture: projected shadows away from spawn 0, the Marine Cat moving.
- The local Marine Cat is visible only in the native third-person debug view; the legacy shows it
  only in mirrors (not implemented, N10). Other players' Marine Cats are drawn when present (N8
  networking will supply them).
- The Marine Cat's seven GLB clips are not played (the legacy runtime does not play them either).
- Creature JPEG textures are decoded by stb_image (as in N5).
- Fixture actions KILL / HURT drive waves and deaths in the recordings; they are not game inputs.


Weapons, effects, HUD (N5)
- Dynamic lights (N5.1) light world, material surfaces and (N6) creatures. Lens flares are not
  implemented.
- N7: the HUD follows the legacy layout and look. Damage numbers and the incoming-hit indicator are
  still missing.
- Practice targets (`--enemies practice`) are static boxes with the legacy hit boxes.
- The N5 weapon tapes cover Helios Crucible only. (N6: player death, the scored respawn point and
  the wave restart are compared on all four maps.)
- Effect settings (detail, marks, brass, blood, rail-trail time) are fixed at the legacy defaults
  (N7: the viewmodel calibration is a setting now).
- Weapon textures are JPEG decoded by stb_image; the browser's decoder differs slightly.
- A press and release inside one simulation tick counts as one tick of input (latched).
- Preserved legacy behaviour (not defects of the port): level shots are headshots; world hits are
  sampled every 0.19 m; no reserve ammo and no ammo pickups; blast impulse ignores spawn
  protection. See docs/N5_WEAPONS.md section 4.
- Pixel comparisons were made on lavapipe (CPU) on both sides.


Maps and navigation (N4)
- Not drawn: the three MD3 prop models of The Arsenal; portal, mirror and monitor surfaces show a
  black feed (three on The Arsenal, one on Helios). One of three compared Arsenal views is at
  33.5 dB for this reason.
- No precomputed visibility (PVS); clusters are culled with the N2 view-cone test only. Regions are
  data, not used for culling.
- The Arsenal's sky box is an extracted asset (assets/n4); without it the sky is the clear colour.
- Frame comparisons for the three new maps use 3-5 spawn views each, not a full tour.
- Navigation: the search, the time-sliced job queue and shared routes are ported (N6). The legacy's
  idle-time adjacency precompute is not: adjacency is computed on first use (same content). Graph
  build time was not measured.
- `RegionMap` has no legacy counterpart to compare with.
- The window input self-test script is written for Helios' first spawn.


Physics and traversal (N3)
- The legacy's effective crouch collider (1.506 m instead of the nominal 1.20 m) is preserved; see
  docs/N3_PHYSICS.md. Other preserved quirks are listed there (slab edges without radius,
  wall-strafe speed, step-down falls, position recovery).
- Bit-identical simulation was shown across compilers and optimisation levels on Linux/glibc only.
  `sin`/`cos`/`atan2` come from the platform library, so other platforms may differ in last bits.
- N7: sprint hold / toggle / auto and the touch stick are implemented; arrow-key look and edge-turn are not.
- No downed state handling beyond the legacy guard, no blast impulses or knockback (N5/N6).
- N7: footsteps, jumps, landings and slides play the legacy body sounds. The weapon viewmodel is not
  drawn (N5).
- The window renders the latest simulation tick without interpolation (as the legacy does).
- Keyboard/mouse capture was tested with synthetic SDL events under Xvfb; real devices and Windows
  were not run.
- The interactive window no longer has unrestricted movement by default; `--fly` restores it.


Vulkan compatibility (N2.1)
- One test device: Mesa lavapipe (Vulkan 1.4, CPU). Vulkan 1.1/1.2/1.3 devices and missing feature
  bits were simulated with the test-only layer `VK_LAYER_CAT_device_sim`; that proves what the
  engine requests and calls, not how a real older driver behaves.
- N9 accepts Vulkan 1.1 with baseline features. Physical Vulkan 1.1 driver behavior is unverified.
- The optional paths reduce objects and upload work; no frame-time benefit was measured or claimed.
  Extended dynamic state saves one pipeline on Helios; push descriptors save one descriptor set.
- Depth formats with stencil were not exercised on the dynamic-rendering path.
- Historical Android work: the renderer follows Android's constraints (1.0 + WSI imports,
  run-time lookup) but was never compiled for Android. Windows was not built either.
- The test layer and its self-test are Linux-only; on other systems the simulated-device tests are
  not built and the path matrix covers what the installed driver offers.
- Frames-in-flight is still 1 and there is one Vulkan allocation per resource (unchanged from N2).

Rendering and content (unchanged from N2)

- Only Helios Crucible is rendered. The material parser handles every field of all four maps, but
  Bastion Keep, Reactor Bastion and The Arsenal (sky boxes, mirrors, MD3, patches) are N4 work.
- Not rendered: portal/monitor live feed (black feed + scanlines), mirrors, MD3 models, lens
  flares, glass/sun legacy meshes (Helios has none), PVS (not needed for correctness; costs
  performance). (Effects, marks and dynamic lights were added in N5 / N5.1.) Light-volume and per-vertex lightmap modes not exposed.
- The sky geometry is drawn directly from the bake eye instead of through a 512 px cube map;
  equal geometry, slightly sharper edges than the legacy (part of the residual difference).
- Parity was measured with the same software rasterizer (lavapipe) for legacy and native.
  Hardware drivers will differ in filtering/precision details; no hardware GPU was available.
- `assets/n2/world-atlas-runtime.png` is captured from the running legacy (Chromium rasteriser)
  rather than regenerated by the N1 baker; regenerating it offline needs the N1 Node canvas
  toolchain (npm registry unreachable here). Provenance and audit in assets/n2.
- Helios cloud textures differ from the legacy runtime by ~0.3 levels (alpha-weighted); kept.
- The running legacy reports 161,062 static material triangles (Q3MAT.stats.staticTris) versus
  161,014 in the N1 bake. The 48-triangle difference is not yet identified; it produced no
  visible difference at the 7 compared poses. Investigate in N4 alongside portals/MD3.
- Native JPEG decoding (stb) differs from the browser by at most 3 levels on kit textures.
- Hardware GPUs, Windows and Android were not executed. No EXE/APK supplied. (Live swapchain resize
  was executed in N2.1 under Xvfb.)
- No performance numbers: lavapipe is a CPU rasteriser. CPU scene preparation (~5 s for Helios in
  Debug, including texture decode and mip generation) is unoptimised and should move offline.
- Memory: one Vulkan allocation per resource (no VMA yet); fine for a single map, revisit in N4/N10.
- Validation layers were a local developer build (Xlib variant needed a build-only Xrandr type
  stub); they are test tools, not shipped.
- Earlier limitations still apply: N1 data limits (no baked A* graph, kit glTFs diffuse-only),
  no gameplay, AI, audio, UI, networking or Android implementation.
