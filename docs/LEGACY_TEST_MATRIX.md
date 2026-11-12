# Test matrix

Checkpoint migration-0.1-audit; 2026-11-08. Environment: Linux container, Node v24.22.1 and Python 3. Results below are limited to static checks performed during Step 01.

| Check | Result | Scope / evidence |
|---|---|---|
| Primary HTML inline scripts | TESTED: 55/55 parse | V8 vm.Script syntax compilation only |
| Primary HTML combined scripts | TESTED: parse passes | No execution / DOM / WebGL |
| Music branch inline scripts | TESTED: 55/55 parse | Same syntax check |
| Music branch combined scripts | TESTED: parse passes | No execution |
| Five preserved archives | TESTED: ZIP CRC passes | ARCHIVE_INVENTORY.json |
| Primary HTML versus APK asset | TESTED: byte-identical | STATIC_CHECKS.json |
| Marine GLB structure | TESTED: length/version/indices/skin/clip metadata | 274 triangles, 21 joints, 7 clips; no visual/animation test |
| APK compiled manifest | Inspected | APK_MANIFEST.json; no installation |
| New audit Python tool | Executed | inventory.py produced evidence |
| Existing game runtime / all maps | NOT_STARTED | Step 02 |
| Gameplay / collisions / AI / audio / UI smoke tests | NOT_STARTED | Step 02 and later fixtures |
| Live multiplayer | BLOCKED pending compatible server setup | Current matching server not identified |
| Android installation/device/lifecycle | NOT_STARTED | No device test performed |
| Windows native compilation/execution | NOT_STARTED | No native code in this step |
| Android ARM64 compilation | NOT_STARTED | No native code in this step |
| Vulkan validation / shaders / GPU runtime | NOT_STARTED | No Vulkan implementation |
| Benchmarks / screenshots | NOT_STARTED | Step 02; no performance claims |

Do not interpret parsing as behavior validation. Prior archives contain historical test reports; they have not been rerun against this reference and do not populate this matrix as passes. APK cryptographic signature validation was not repeated in this step; archive CRC and HTML identity are distinct checks.

## Step 02 checkpoint

| Check | Result | Scope |
|---|---|---|
| Capture driver Node syntax | PASS | Parsing only |
| Original eight source hashes | PASS | Preservation |
| New relay HTML equality | PASS | Exact reference bytes |
| Supplied relay mock smoke | PASS | Create/discovery/join/map rejection/relay/health; mocked sockets |
| Browser game launch | BLOCKED | Process restrictions and browser access policy |
| Maps, movement, combat, AI, UI, audio | UNVERIFIED | No game session |
| Real multiplayer | UNVERIFIED | No client sessions |
| Android / Windows package build or launch | NOT RUN | No platform validation |
| Performance / screenshots | NOT COLLECTED | No rendered game |
| Native compilation / Vulkan validation | NOT APPLICABLE | Native work not started |

See BASELINE_REPORT.md for reproducible commands and closure checklist.
