# Feature parity — preliminary audit tracker

This is Step 01 discovery only. The definitive per-feature priority/assets/replacement/verification matrix is Step 03 and is NOT_STARTED. No existing feature is reclassified optional. Native implementation for every row is NOT_STARTED; runtime parity is unverified.

| Discovered feature group | Legacy location in index.html | Native status | Known issue / future verification |
|---|---|---|---|
| Four maps, stacked floors, stairs/ramps, bridges, jump pads | 6859–8205, 9700–9767 | NOT_STARTED | Compare each source map separately; heights differ |
| Movement, sprint/crouch/slide/jump/air control | 5161–5171, 12111–12220, 8461–8665 | NOT_STARTED | Deterministic trajectories; effective collider height differs from nominal |
| 16 weapons, reload, hitscan/projectiles, explosions/recoil | 5140–5156, 11811–12108 | NOT_STARTED | Preserve actual firing path and splash formulas |
| 20 enemy types, flyers, layered navigation and attack phases | 6136–6673, 11443–11500, 12231–12305 | NOT_STARTED | Real floors/paths and target selection |
| Waves, health, death, pickups, final-five reveal | 11477 onward, 12344, 13300 onward | NOT_STARTED | Counts/scaling/deck checks and wipe flow |
| Materials/lightmaps/sky/fog/portals/curves/MD3/effects | SOURCE_AUDIT module table | NOT_STARTED | Equivalent visual settings and complete assets |
| Marine/enemy animations and hit volumes | 10559–10781, 13268–13327, model ZIP | NOT_STARTED | No general runtime GLB mixer; remote/local hit paths differ |
| Multiplayer browser/host/join/snapshot/revive | 15495–16096 | NOT_STARTED | Matching server source not established |
| Audio samples, fallback synthesis, streamed music/mix | 11602–11810, 807–826 | NOT_STARTED | Primary embedded music versus v0.18 stream unresolved |
| Gothic menu/HUD/parallax/settings/pause/return | 1–806, 14616–15007, 15262–15494, 16364–17100 | NOT_STARTED | Preserve final layered styling and input routes |
| Touch/KBM/layout/fullscreen/resolution | 13538–13548, 14909–15007, 16364–16529 | NOT_STARTED | Device/safe-area behavior not verified |
| Metrics and graphics changes | 13421–13567, 14680–14834 | NOT_STARTED | Measured vs estimated scopes documented in audit |

Legacy files are byte-preserved. No features removed, adjusted or merged. Jump/landing animation requirements must be reconciled with the actual exported clips and procedural behavior in Step 03.

## Step 02 evidence update

No gameplay/rendering feature changed. All runtime parity remains unverified. Matching relay source now available; mock protocol test passed only. v0.18 input/settings patch remains separate and unapplied. This is not the Step 03 definitive matrix.
