# N6 pictures — native vs the unmodified legacy game (1280x720, lavapipe on both sides)

Tape frames: the legacy `render()` after the tick of the Helios enemy tape (tools/n6/legacy_enemies.cjs --frames, HUD hidden, lens flares off, Math.random = 0.5 while drawing) vs `cat_arena --enemy-fixture ... --capture-ticks` (build-desktop-release).
21 frames: 42.5-63.6 dB, median 52.0 dB. During the work the first native run of the same frames gave 19.9-59.4 dB (docs/DEVELOPMENT_LOG_N6.md section 4).

| Frame | PSNR dB |
|---|---|
| death_and_restart_00250 | 54.16 |
| death_and_restart_00700 | 59.45 |
| fight_spin_00300 | 58.29 |
| fight_spin_00530 | 63.55 |
| fight_spin_00560 | 58.80 |
| fight_spin_00620 | 60.05 |
| fight_spin_00900 | 57.34 |
| fight_spin_01300 | 45.99 |
| flyers_wave5_00200 | 58.83 |
| flyers_wave5_00500 | 52.00 |
| flyers_wave5_00900 | 56.03 |
| flyers_wave5_02003 | 46.83 |
| sprint_circuit_00600 | 43.43 |
| sprint_circuit_01277 | 46.49 |
| stand_still_00400 | 45.68 |
| stand_still_00571 | 50.43 |
| stand_still_01000 | 50.73 |
| wave_progression_00300 | 44.72 |
| wave_progression_00400 | 42.52 |
| wave_progression_01250 | 44.83 |
| wave_progression_02400 | 62.08 |

Staged scenes (tools/n6/legacy_enemies.cjs --lineup-dir vs cat_arena --enemy-lineup, every random number 0.5):

| Scene | PSNR dB |
|---|---|
| idle | 51.42 |
| windup | 48.48 |
| strike_final5 | 52.77 |
| bodies | 52.28 |
| marines | 50.87 |

Sheets: tape_frames_legacy_left_native_right.jpg and staged_legacy_left_native_right.jpg (order in sheet_order.txt). The remaining differences in the tape frames are mostly world texture filtering (N2/N4).
