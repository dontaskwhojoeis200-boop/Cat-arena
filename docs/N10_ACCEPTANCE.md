# N10 gameplay acceptance

Use the N9 executable and unchanged maps/assets as the comparison build.
Check these scenarios on Windows before marking N10 complete.

| Scenario | Existing evidence | Windows verification |
| --- | --- | --- |
| Move, sprint, crouch, slide, jump and air strafe | Legacy traversal tapes cover all four maps | Exercise each move using keyboard and mouse |
| Weapons, blasts and knockback | Combat tapes and weapon effect image regressions | Use all weapons and test airborne shotgun recoil and explosive jump |
| Ground and flying enemies, health and waves | Enemy replay tapes, wave and animation checks | Fight through early waves and continue to the final-five highlight |
| Reactor Bastion collision and navigation | Layered map, traversal and navigation checks | Cross both bridges, enter maintenance tunnels, traverse ramps and test the undersides |
| Local character and viewmodel | Marine Cat pose, model and weapon-view regressions | Check teammate poses, labels and distinct first-person viewmodel |
| Pause, settings and audio | Menu and audio tests with simulated input and dummy playback | Verify physical devices, focus changes and settings persistence |
| Multiplayer combat and revive | Four-player automation on Windows loopback and software-Vulkan Linux | Test at least two separate Windows PCs and four clients where available |

Current parity gaps include live portal/mirror feeds, three Arsenal MD3 props,
and some legacy graphics controls. See [FEATURE_PARITY.md](FEATURE_PARITY.md)
for details. Keep the existing map layout and comparison recordings while
fixing issues found during play. Use the N11 build to capture the hardware
baseline after the gameplay checks.
