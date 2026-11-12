# N6 fixtures — recorded from the unmodified legacy game

`tools/n6/legacy_enemies.cjs` calls the legacy `update(dt)` itself for every 1/120 s tick (seeded
`Math.random`; presentation code on a second and third seeded stream; clock pinned; adjacency cache
filled before recording). Six scenarios per map: stand_still, fight_spin, sprint_circuit,
wave_progression, flyers_wave5, death_and_restart (Helios at full length, the other maps at half).

| File | Content | Read by |
|---|---|---|
| `<map>_enemies.bin` | "CATENMY1": per scenario the start state, the input tape (key mask, mouse x, y) and 64 values per tick (player, weapons, wave counters, sums over enemies, enemy projectiles, route statistics, bodies, decision accumulator) | `tests/enemy_tests.cpp`, `cat_arena --enemy-fixture` |
| `<map>_poses.bin` | "CATPOSE1" version 2: 81 values per tick (pose sums, weighted pose sums, hands, attack progress, body states, the local Marine Cat pose, animation and cosmetic random draws) | `tests/enemy_fx_tests.cpp` |
| `frames/<scenario>_<tick>.png` | the legacy `render()` after that tick of the Helios tape (1280x720, HUD hidden, lens flares off, Math.random = 0.5 while drawing) | CTest `render_enemy_frame_*` |
| `lineup/<shot>.png`, `lineup/lineup.txt` | five staged scenes at Helios spawn 0 (all types standing; all in their wind-up with projected shadows; the final five; ten bodies; two Marine Cats), every random number 0.5 | CTest `render_enemy_lineup_*`, `cat_arena --enemy-lineup` |

Recording: `node tools/n6/legacy_enemies.cjs --map <id> [--scale 0.5] --out <map>_enemies.bin --pose-out <map>_poses.bin`
(pictures: `--frames "scenario:tick,..." --frames-dir <dir> --width 1280 --height 720`; scenes: `--lineup-dir <dir>`).
