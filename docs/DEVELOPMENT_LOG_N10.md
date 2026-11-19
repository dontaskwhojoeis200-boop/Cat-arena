# N10 development log — gameplay feature parity

N10 was done in two parts by two sessions. Every result below comes from a command that ran; the
first part's results are taken from its own report (docs/N10_TEST_REPORT.md) and Windows CI runs.

## Part 1 — branch `n10-gameplay` (2026-11-13, another session, commits 80428e7 … 9c57066)

Branched from `n9-windows` (Part 1 of N9).
- Co-op: guests see the host's and teammates' rockets, grenades and rift bolts in flight
  (`projectiles` array in the world snapshot, up to 96) and their detonations (`blast` message);
  `crouchOffset` in hero data. Older clients ignore the additions (same relay protocol).
- Frags of a guest's kill go to that guest; the host no longer counts them (see deviation below).
- Remote gunfire draws the Marine Cat's muzzle flash, light, tracers and beams from the last known
  pose; the host hears guest gunfire. Fire / shotFx messages with an invalid weapon index are dropped.
- HUD: damage numbers, incoming-hit indicator, downed notice that names the revive key; guests get
  the hit amount from the host's health change.
- Settings now applied: fog strength, lightmap intensity, overbright, effects detail, marks, brass,
  blood, gibs, rail trail time (8 of the 25 stored-only settings, plus fog quality).
- The Arsenal: the original animated flag and two tag-attached turrets (MD3, 502 triangles).
- SDL input self-test schedules its events on simulation ticks.
- Evidence: Linux 41/41 GPU-free + 30/30 software Vulkan; Windows CI runs 43460222820 and
  43543747717 (42/42 GPU-free tests).

## Part 2 — branch `n10` (2026-11-19, this session)

1. **Merge.** `n10-gameplay` (9c57066) merged onto the final N9 (`n9`, 45e9c73) as a6d1007. Linux
   75/75; the 15 solo replay lines identical to N9.
2. **Review of the merged code against the legacy game** (legacy/index.html).
   - Protocol additions: compatible; the original browser game ignores `blast`, `projectiles` and
     `crouchOffset` (confirmed again by the cross-play run below).
   - Weapon index validation on `fire` / `shotFx`: same effect as the legacy (`WEAPONS[i]`
     undefined → nothing happens).
   - **Enemy shots aimed at a teammate — two rules restored to the legacy.** Part 1 had changed
     them; the legacy (index.html 12285–12297) does:
     - a shot aimed at a *downed* teammate is still tested against that teammate's Marine Cat
       parts; it stops there and `remoteApplyDamage` ignores downed players (no damage);
     - a shot aimed at a player *who left* is tested against the host's box
       (`q = target || player`) and stops there without damage.
     Part 1 let both pass through. Restored (commit 2a7f02c); `Horde::stage_shot` and two co-op
     rule checks added — both fail on the Part 1 code and pass now.
   - **Frag attribution — kept as a deliberate difference.** The legacy host runs `score++` for every
     kill, including a guest's, so the host's frag counter counts the team's kills while the guest
     gets its own `frag` message. Part 1 credits only the shooter, which is what the HUD presents
     ("your frags"). Wave progress still counts every kill as in the legacy. Recorded in
     KNOWN_LIMITATIONS.md.
3. **Multiplayer re-validated after the protocol additions.**
   - Full Linux suite 75/75 after the fix (78/78 at the end of N10, with the three new weapon-tape
     tests), including `coop_multiplayer` (59 checks with the two new ones),
     `coop_interop_legacy_relay` (the original server.js), `net_relay_protocol`,
     `window_coop_selftest` (n10-evidence/part2/linux-ctest.txt).
   - Cross-play with the original browser game in headless Chromium: 13/13
     (n10-evidence/part2/crossplay/).
   - Soak, four players with live waves: 3,600 s of game time (87.6 s real time), 39,277 snapshot
     rounds, 216,024 poses, 446,419 relayed messages, 76 squad wipes, 128 kills by guests; host
     upload 117.4 KiB/s, guest upload about 4 KiB/s (n10-evidence/part2/soak-3600s.txt). N8 ran 900 s
     in CTest and one hour once; the snapshot now carries projectiles. A 600 s run passed first.
4. **Weapon tapes on all four maps.** The N5 weapon tapes were recorded on Helios Crucible only
   ("other maps not compared" in FEATURE_PARITY). `tools/n5/legacy_combat.cjs` gained `--map`; the
   same 29 scenarios (16 weapons, kills and reroll, rocket / grenade jumps, splash falloff, grenade
   arcs, shotgun recoil, headshots, medkit, firing while moving) were recorded from the unmodified
   legacy game on Bastion Keep, Reactor Bastion and The Arsenal (`q3-gallery`), each in its own
   longest firing lane. New CTests `combat_legacy_tapes_<map>`.
   Result: the native game reproduces every recorded value on all three maps without a code
   change — 3 × 18,220 ticks bit-exact (worst difference 0), all 16 weapons fired, 630 pellets
   and 31–32 detonations per map. With Helios: 72,880 ticks, four maps.

   | Map | Target hits | Detonations | Random draws | Native state hash |
   |---|---|---|---|---|
   | Helios Crucible (N5) | 272 | 32 | 1,614 | 36759acbc0ae3d1 |
   | Bastion Keep | 265 | 32 | 1,601 | a16b4b0792fab5dc |
   | Reactor Bastion | 282 | 31 | 1,619 | 2af0d77d803fe2b6 |
   | The Arsenal (q3-gallery) | 242 | 31 | 1,576 | b2eaffbeaf5bf986 |

   The first Bastion Keep recording crashed headless Chromium while the hour-long soak ran at the
   same time; it was rerun on an idle machine.
5. **Stored-only settings.** The 17 settings still stored only are renderer features: light
   flares, brightness, texture detail / bits / filter / anisotropy, lightmap filter, curve detail,
   detail textures, mirrors and portal resolution, world culling modes, scene resolution, MSAA,
   FXAA, adaptive resolution and its FPS target, plus the VERTEX / LIGHT VOLUME lighting modes.
   They are not gameplay, and each one adds memory (render targets, mip chains, extra passes) that
   the revised roadmap measures against the 192 MiB budget first (N12). Their in-game notes said
   "N10"; they now name the tier that does the work: N13 (flares, brightness, lighting modes),
   N14 (texture and lightmap filtering, texture detail, curves, detail textures, culling modes),
   N16 (scene resolution, MSAA, FXAA, adaptive resolution, mirrors / portals). Live portal and
   mirror feeds and PVS stay open with them.
6. **Final Linux check.** 78/78 tests (n10-evidence/part2/linux-ctest.txt); the 16 solo replay
   and rule lines are identical to N9 (n10-evidence/part2/replay-lines.txt).
7. **Windows CI, run 44560790833** (58195f3): build, package, 48/48 GPU-free tests (three more than
   N9: the new weapon tapes), crash tests with minidumps (320,171 and 283,954 bytes), WinHTTP stress
   15/15 rounds, `coop_multiplayer` 59 checks through both relays. The four-map weapon tapes and the
   15 replay lines the job prints are identical to Linux (and to N9). Artifact
   `CAT_ARENA_FERAL_CLAW_N10_WINDOWS` (id 14549960826, 35,498,398 bytes, expires 2026-12-19).
   Job log: n10-evidence/part2/windows-ci/.
