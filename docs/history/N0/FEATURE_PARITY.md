# Current native feature parity — N0

Original audit tracker preserved at docs/history/pre-N0/docs/FEATURE_PARITY.md.
No native gameplay/visual parity is verified. Legacy/reference files are unchanged.

| Feature | Native status | Target / verification |
|---|---|---|
| C++20 core/CMake/tests | Implemented, headless tested | N0 |
| 120Hz time foundation | Implemented/tested; same source clamp/bounds | Gameplay trajectories still N3 |
| SDL3 desktop boot/input bridge | Compiled; headless path tested | Visible window/input NOT_RUN |
| Vulkan triangle pipeline | Compiled/linked; shaders compiled | Visible render/resize/validation NOT_RUN |
| Settings/paths/log/RNG/basic math | Implemented/tested core | Complete user settings UI N7 |
| Extracted native asset format | Not started | N1 |
| Helios Crucible | Not started | N2/N3 |
| Bastion Keep, Reactor Bastion, The Arsenal | Not started | N4; original IDs retained |
| Stacked collision/movement/blast jumps | Not started | N3/N5 |
| All 16 weapons/viewmodels/projectiles | Not started | N5 |
| All 20 enemy types/navigation/waves | Not started | N6 |
| Real Cat Marine skin/clips/hitboxes | Not started; source retained | N6 |
| Gothic menu/settings/pause/HUD/audio | Not started | N7 |
| Four-player relay-compatible native client | Not started; server unchanged | N8; real 2/4-client tests required |
| Android native ARM64 application | Not started | N9 |
| Full materials/lightmaps/fog/sky/portals/effects | Not started | N2 then N10 fidelity |
| Performance metrics/optimization | Not started | N10; no invented FPS |
| Windows EXE/Android APK release | Not built | N11 |
