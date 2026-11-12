# N1 test matrix

| Check | Result | Evidence/limit |
|---|---|---|
| Debug core + asset targets | PASS4/4 | core-configure/build/tests.txt |
| Release core + asset targets | PASS4/4 | release-configure/build/tests.txt |
| ASan/UBSan core + asset targets | PASS4/4 | asan-*.txt; ASAN_OPTIONS=detect_leaks=0 |
| Linux desktop compilation + headless targets | PASS5/5 | desktop-*.txt; no GPU calls on test path |
| Two clean offline conversions | PASS | bake.txt, repeat-bake.txt |
| All577 generated content hashes identical | PASS | reproducibility.txt; full manifests equal |
| Four native maps/meshes loaded and checked | PASS | baked_maps CTest target: refs, indices, grids, ranges, schema |
| Independent JSON/binary equality | PASS | reproducibility.txt; Python decoder independent of JS writer/C++ |
| Four shipped lightmap layouts/dimensions | PASS | exact hashes/PNG sizes, source UV layouts retained |
| Stacked surface data | PASS | each map retains overlapping walkable elevations; not physics tests |
| Corrupt data/mesh rejection | PASS | all fixture truncations, checksum/version/tag/count/depth/NaN/index/path/schema errors |
| Original reference integrity | PASS | source-integrity.txt against unchanged N0 inventory |
| Image decode | PASS165 images | image-decode.txt; decoded, not visually matched to browser |
| glTF/GLB buffers, rig and hitboxes | PASS structural | fitted kit bounds/URIs; original Marine21 joints/7 clips |
| N0 foundation regression | PASS | included in each native test run |
| Native visible map/triangle rendering | NOT_RUN | no new graphical execution claim; N0 video blocker remains |
| Windows / Android build/device | NOT_RUN | no target binaries claimed |
| LeakSanitizer | NOT_RUN N1 | N0 runner failure retained; disabling leaks is not a leak-check pass |
| Native physics/AI/animation/audio/network parity | NOT_RUN | systems not implemented yet |

Evidence resides in docs/n1-evidence. Historical N0/legacy evidence is separate.
The map validation CLI reports data counts and matched hashes, never GPU/visual
correctness or invented performance. The preview is a source-geometry schematic.
