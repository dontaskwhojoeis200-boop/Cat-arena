# N5.1 — dynamic lights and medkit billboards (2026-11-10)

Closes the first open item of N5: muzzle flashes, projectiles and explosions now light the map
as in the legacy game. While comparing frames a second missing piece showed up and is closed too:
the medkit itself (the legacy draws a small billboard, N5 drew only its glow).

## Legacy behaviour (source study)

| Step | Legacy (`legacy/index.html`) | Rule |
|---|---|---|
| Request | `Q3R.addLight` 2892, called by the effect code | at most 64 lights per frame |
| Selection | `Q3R.uploadLights(camera)` 2895 | score = radius x (r + g + b) / (1 + 0.15 x distance to the camera); stable sort, best 16 |
| World surfaces | main fragment shader 958-966, 1164 | `light += dlights(P, N) x 0.84`, before the debug views; attenuation `(1 - d / radius)^2`, normal term `clamp(N·L x 0.7 + 0.3)` |
| Material surfaces | Q3MAT fragment shader 3955-3958, `drawMaterial` 4132 / 4144 | only materials that are lightmapped and not translucent; `rgb x (1 + sum x 0.84)`, clamped, before fog; for a multi-pass material only in the first pass |
| Dynamic geometry | same main shader with `isGun = 2` | flat 0.80 light plus the lights without a normal term |
| Weapon model | returns before the lights | lit by the muzzle-flash glow only (already in N5) |
| Medkit | `renderGeometry` 12353, `billboard` 5395 | 0.65 m quad on world-atlas layer 11, turning with the view yaw, bobbing `0.20 + 0.06 sin(2 now + x)` above the pickup, alpha cut-out |

Sources of lights in N5: muzzle flash, plasma / rail / fire impacts, plasma and flame tracers,
the start of a rail trail, the thunderbolt (both ends), rockets, blinking grenades, rift bolts,
explosions.

## Native implementation

* `fx::select_lights` (weapon_fx.cpp) — the legacy selection. `Session::build` converts the result
  to `cat::DynamicLight` and hands it to the renderer through `FrameView::lights`.
* Frame uniform block: `ivec4 dl; vec4 dlPos[16]; vec4 dlCol[16]` appended (144 -> 672 bytes).
  `dlights()` lives in `frame.glsl`.
* `world.frag`: one guarded line at the legacy position. `material.frag`: the Q3MAT block, enabled
  per pass by `D.h1.w`, which the renderer sets to `lightmapped && !translucent && first pass`.
* New pipeline `dynamic` (`dynamic.vert/.frag`, legacy `isGun = 2` branch) for 12-float world-atlas
  vertices; `fx::billboard` builds the medkit quads into `Frame::atlas`.
* With no lights the shaders take the old path: the N2 golden frames are still byte-identical.
* `--features -dlights` turns the lights off (`r_dynamiclight 0`).

## Verification

| Check | Result |
|---|---|
| Light selection vs legacy: the recorder now also stores what `uploadLights` sends (count, position and colour sums) for every tick of the 29 tapes | equal on all 18,220 ticks; 8,603 lights uploaded |
| Recorder self-check | combat fixture byte-identical to N5; the first 40 values per tick of the effect fixture (now 43) identical to the N5 fixture |
| Selection beyond 16 lights, viewer dependence, tie order (the tapes never exceed 4 lights) | unit tests |
| Frames during effects vs legacy, 17 frames without targets | 41.0-59.2 dB, median 57.1 (N5: 20.0-59.2, median 27.8) |
| Rocket fired / rocket blast / medkit frames | 24.7 -> 50.2 dB, 22.1 -> 57.2 dB, 32.5 -> 57.4 dB |
| Weapon views, 16 weapons | 51.5-52.5 dB (N5: 45.4-47.5; the medkits in view were missing) |
| Lights off (`-dlights`) | back to 24-26 dB on the rocket frames |
| Automatic Vulkan path vs 1.2 baseline, frames with lights | byte-identical |
| N2 golden frames, both paths | 7/7 byte-identical |
| CTest | 3 new frame tests against legacy pictures (minimum 40 dB); weapon-view minimum raised to 40 dB |

The remaining difference in the lowest frame (41 dB, a tiled column next to a blast) is confined
to texture edges; the lit surface itself agrees. It is the same edge sampling difference that the
N2 comparisons show on close oblique surfaces.

Frames with practice targets are at 17.5-50.5 dB depending on how much of the picture the target
covers: the legacy draws an enemy model there (N6).

## Not covered

* Lights on enemy and player models (legacy 1005) come with those models in N6.
* Lens flares / coronas (`r_flares`) and enemy-effect lights are not part of N5.
* Pixel comparisons are on lavapipe on both sides; no hardware GPU was available.
