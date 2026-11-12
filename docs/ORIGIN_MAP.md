# N0 origin map — inspected 2026-11-09

Primary: `legacy/index.html`, 18,320,927 bytes, 55 script blocks. Read in full for
hashing/indexing; focused semantic inspection used the subsystem entry points
below, the full script index, source audit, relay and character archive metadata.
This is not a claim of line-by-line gameplay review or dynamic parity testing.
Logical JS paths are inline labels, not existing standalone files.

| Concern | Primary HTML lines / logical module | Native destination/status |
|---|---|---|
| Embedded images/music | 807–814 | N1 inventory/extraction pending |
| Encoded enemy/gun meshes and SFX | 815–826 | N1 pending |
| Materials, shaders, textures, lightmaps | 827–5132 | N0 triangle only; original rendering retained as reference |
| Weapon definitions | 5140–5156, `WEAPONS` (16 entries) | N5 pending; no values invented |
| Movement constants, moveMarine, Stage 4 collision, pads, view motion | 5161–5171, 8225–8665, 9759–9785, 10443–10555, 12111–12231, 14945–14976 | **N3 implemented** (native/src/physics_world.cpp, player.cpp; docs/N3_PHYSICS.md) |
| Enemy types/navigation | 6136–6673, 11443–11500 | 20-type parity requirement, N6 pending |
| World primitives | 6859–7112, `js/world/world-kit.js` | N1 must preserve callbacks/links and collision/art separation |
| Reactor Bastion | 7113–7419 | `reactor-bastion`, floors -4/0/5/10.4 |
| The Arsenal | 7420–7581 | `q3-gallery`, floors -3/0/4.5/10 |
| Bastion Keep | 7582–7873 | `bastion-keep`, floors -4/0/7/11 |
| Helios Crucible | 7874–8035, lightmaps 8036–8039 | `helios-crucible`, floors -4/0/7/12 |
| World compilation/map IDs | 8040–8205 | N1 format; N2/N4 map rendering |
| Collision, broad phase | 8206–8864 | N3 pending, stacked surfaces required |
| World renderer/gameplay bridge | 8865–9767 | N1/N2; triggers/spawns must not be lost |
| Visibility, portals, MD3 | 9768–10442 | Preserve for N10 fidelity |
| Animation/effects | 10443–11315, marine 13268–13327 | N6, GLB clips differ from actual JS pose evaluation |
| Weapon/movement update | 11811–12347 | N3/N5/N6 |
| Fixed timestep | 14803–14813 | N0 exact 120Hz, .055 elapsed clamp, .06 accumulator, 7-step ceiling |
| Desktop/touch input | 14909–15007, 16364–16529 | N0 basic key/button bridge; N7/N9 full input |
| Multiplayer | 15495–16096 | N8 native client pending |
| Menus/HUD/entry | 1–806, 14616+, 16097–17100 | N7 pending; N0 window title only |

Actual movement constants observed: walk 7.6, crouch multiplier .80, sprint 1.50;
slide duration 1.12, cooldown .72, boost 1.085; ground/air acceleration 17.5/1.85;
friction ground 6.0/crouch 7.3/slide .62; stop speed 2.9; jump impulse 7.65;
max skill speed 20.5; buffer .115, coyote .09, landing-slide buffer .30;
grade penalty .38/downhill bonus .13; radius .30, standing height 1.80,
nominal crouch height 1.20, gravity 18. Effective runtime crouch collision still
needs reconciliation before N3. None of these movement mechanics is implemented
by N0; only timing is ported.

World coordinates: X-right/Y-forward/Z-up. N0 view-basis conversion is
(x,y,z)->(x,z,-y). GLB hitbox metadata uses +Y-up/+Z-forward and model meters,
so N1/N6 must explicitly resolve model orientation and winding; do not assume
its axes match the world or blindly swap axes without checking handedness.

Character ZIP entries: rigged GLB, two 256px textures, preview, model metadata,
hitbox JSON, generation/integration/APK Python scripts and README. Metadata reports
274 triangles, 21 bones, one mesh/seven primitives, seven clips (idle, walk, run,
shoot, melee, hurt, death), two textures. Hitbox source references head/torso boxes,
limb capsules and disables self hitboxes. These are source facts, not native imports
or measured visual/animation correctness. Original model is retained unchanged.

Current relay `reference/multiplayer/server.js`: Node >=20, package 0.17.1,
`ws ^8.18.3`, `/rooms`, `/health`, `/ws`; maximum four players; build
`VA-MODULAR-RB-011`; map wire IDs are the four keys above plus `-v1`. Host
simulation is relayed, not performed on the server. Preserve create/join/ready,
roster/peer/relay/error semantics, 128KiB packet cap and host departure behavior.
N0 does not install/change the relay or claim network tests.

Unapplied v0.18 mouse/settings patch and divergent v0.18 soundtrack remain
separate. Legacy APK remains WebView-based. Historical Step 02 archive is retained
inside reference/history. No branch merge, JS translation or legacy edit occurred.
Hashes and ZIP-member CRC/inventory: `N0_SOURCE_INVENTORY.json` (reproducible via
native/tools/inventory_sources.py); initial handoff hashes are separately recorded.
