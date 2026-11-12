# N4 development log — 2026-11-10

1. First step was to measure, not to assume: rendered the three other maps with the N3 build and
   captured legacy frames at spawn poses on every floor. Bastion Keep and Reactor Bastion already
   matched at N2 quality (44-63 dB). The Arsenal did not (22-31 dB): wrong sky.
2. Cause: The Arsenal's sky is an image sky box that the legacy paints procedurally at load. Took
   the same route as the N2 world atlas: extract the generated data from the running legacy
   (`tools/n4/legacy_skybox.cjs`), ship it as an asset with provenance, and port the box shader.
   First run of the tool failed because materials are only registered after a game starts; the
   tool now starts a solo game like the other reference tools. First shader build failed on a
   missing include directive. Result: 47.9 dB on two poses; the third (33.5 dB) shows a missing
   MD3 model and a portal surface.
3. Collision: extended the N3 trajectory tool with `--map` and recorded fixtures for the three
   maps. All replayed within 1.8e-15 on the first run, including real ceiling contacts, which
   Helios could not provide in N3.
4. Navigation: read the legacy NAV3 code, ported graph build, links, nearest-node choice, A* and
   smoothing, and wrote `tools/n4/legacy_navigation.cjs` to dump the legacy graph, adjacency hashes
   and routes. The legacy search loop is tied to enemy objects and a time budget, so the tool
   transcribes that loop with the legacy heap, heuristic, adjacency tables and buffers; this is
   stated in the tool. All four maps matched on the first comparison.
5. Map selection: map table from the legacy `VOID_MAPS`, `--list-maps`, in-window switching.
   The N3 input self-test fails on The Arsenal's first spawn ("slid=no": a wall is closer than on
   Helios); it is a Helios-specific script, not a movement difference, and stays Helios-only.
6. The sanitizer build found a real defect in my new test reader (zero-length `memcpy` from a null
   pointer for empty arrays). Fixed in both test readers; product code was not affected.
7. Decided against starting MD3 models, portal/mirror rendering and PVS in this delivery: each is
   a separate renderer feature, and the measured state is documented instead of a partial port.

Regression: N2 golden frames byte-identical on the automatic and the Vulkan 1.2 paths after the
shader interface change (one uniform vec4 and one sampler added).
