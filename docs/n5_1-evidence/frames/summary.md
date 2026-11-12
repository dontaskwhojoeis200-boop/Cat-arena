# Frames vs legacy, before (N5) and after (N5.1)

1280x720, lavapipe on both sides, HUD hidden. PSNR over the whole frame, in dB.
"Targets in view": the legacy draws an enemy model where the native draws a box (N6), so those frames stay low.

| Frame (tape_tick) | N5 | N5.1 | Note |
|---|---|---|---|
| fire_while_moving_00014 | 20.9 | 20.9 | targets in view |
| fire_while_moving_00060 | 17.5 | 17.5 | targets in view |
| fire_while_moving_00200 | 17.6 | 17.6 | targets in view |
| fire_while_moving_00430 | 39.1 | 50.5 | targets in view |
| grenade_arcs_00024 | 25.9 | 25.9 | targets in view |
| grenade_arcs_00060 | 25.9 | 25.9 | targets in view |
| grenade_arcs_00100 | 25.9 | 25.9 | targets in view |
| health_pickup_00005 | 32.5 | 57.4 |  |
| health_pickup_00060 | 59.2 | 59.2 |  |
| rocket_and_grenade_jump_00042 | 20.0 | 57.0 |  |
| rocket_and_grenade_jump_00044 | 22.1 | 57.2 |  |
| rocket_and_grenade_jump_00048 | 20.7 | 57.1 |  |
| rocket_and_grenade_jump_00060 | 27.2 | 57.1 |  |
| rocket_and_grenade_jump_00090 | 38.6 | 57.3 |  |
| rocket_and_grenade_jump_00120 | 54.0 | 57.1 |  |
| rocket_and_grenade_jump_00404 | 24.5 | 57.6 |  |
| rocket_and_grenade_jump_00430 | 30.0 | 58.5 |  |
| rocket_at_feet_moving_00059 | 48.9 | 50.6 |  |
| rocket_at_feet_moving_00062 | 24.7 | 50.2 |  |
| rocket_at_feet_moving_00070 | 27.8 | 50.0 |  |
| rocket_at_feet_moving_00090 | 29.5 | 49.4 |  |
| rocket_at_feet_moving_00110 | 36.5 | 49.4 |  |
| rocket_at_feet_moving_00204 | 23.9 | 45.1 |  |
| rocket_at_feet_moving_00230 | 25.6 | 41.0 |  |

| Weapon view | N5 | N5.1 |
|---|---|---|
| view_00 | 47.4 | 52.2 |
| view_01 | 45.5 | 52.5 |
| view_02 | 46.4 | 52.2 |
| view_03 | 46.1 | 52.0 |
| view_04 | 45.4 | 52.1 |
| view_05 | 47.3 | 51.8 |
| view_06 | 47.1 | 51.5 |
| view_07 | 47.0 | 52.3 |
| view_08 | 45.4 | 52.2 |
| view_09 | 47.4 | 52.1 |
| view_10 | 47.4 | 52.1 |
| view_11 | 47.5 | 52.4 |
| view_12 | 45.4 | 51.9 |
| view_13 | 47.4 | 52.1 |
| view_14 | 46.7 | 52.2 |
| view_15 | 46.1 | 51.9 |

Frames without targets: 17, minimum 41.0 dB, median 57.1 dB. Weapon views: 51.5-52.5 dB.
