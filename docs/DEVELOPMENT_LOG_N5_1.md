# Development log — N5.1 (2026-11-10)

1. Read `Q3R.addLight / uploadLights`, `dlights()` in the main shader, the light block of the
   Q3MAT material shader and `drawMaterial` (which materials and which pass get lights).
2. Extended the legacy recorder (`--fx-out`, fixture version 2, 43 values per tick) with the result
   of `uploadLights`. Re-recorded: the combat fixture is byte-identical; the first 40 effect values
   per tick are identical to version 1.
3. `fx::select_lights`; replay equal on all ticks. A unit test of mine had the order wrong (the
   big distant light scores highest); corrected after computing the scores.
4. Shaders and renderer: light arrays in the frame block, world and material paths, per-pass flag.
   Rocket frames went from 20-30 dB to 45-58 dB.
5. The medkit frame was still at 32.5 dB: the legacy draws the medkit as a world-atlas billboard
   (`renderGeometry`), which N5 had missed. Added `fx::billboard`, `Frame::atlas`, the `dynamic`
   pipeline and shaders. Medkit frame 57.4 dB; weapon views rose from 45-47 to 51.5-52.5 dB
   because medkits are visible in that view.
6. Looked at the lowest remaining frame (41 dB): differences only along texture edges of a close
   oblique surface, lit areas equal. No change made.
7. Three frame tests added to CTest; regression matrix; fresh-extraction check.
