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
| Multiplayer combat and revive | Four-player automation through the native and original Node.js relays. The Linux software-Vulkan build exercised the hosting UI, name tags, remote muzzle flash, downed status, revive and leave. | Test at least two separate Windows PCs and four clients where available. Observe other players' tracers, beams, rockets, grenades and rift bolts in flight, check detonations and confirm only the shooter receives each elimination. |

Current parity gaps include live portal/mirror feeds, three Arsenal MD3 props,
and some legacy graphics controls. See [FEATURE_PARITY.md](FEATURE_PARITY.md)
for details. Keep the existing map layout and comparison recordings while
fixing issues found during play. Use the N11 build to capture the hardware
baseline after the gameplay checks. [N10 test evidence](N10_TEST_REPORT.md)
records the builds, commands and scenarios executed for this checkpoint.

## Packaged Windows smoke test

Extract the ZIP and open PowerShell in the directory containing `CatArena.exe`.
These commands use the packaged executable and save logs alongside it.

```powershell
$exe = (Resolve-Path .\CatArena.exe).Path
$probe = Start-Process -FilePath $exe -ArgumentList '--vk-info','--log-file','n10-vulkan.log' -Wait -PassThru
$probe.ExitCode
Get-Content .\n10-vulkan.log
$coop = Start-Process -FilePath $exe -ArgumentList '--coop-selftest','--vk-baseline','--log-file','n10-coop.log','--report','n10-coop.json' -Wait -PassThru
$coop.ExitCode
Get-Content .\n10-coop.json
```

The capability probe and the co-op test should both exit with code `0`. The
co-op log should show `threeGuests=yes`, `remoteFire=yes`, `revive=yes` and
`leave=yes`. This exercises the window, renderer and local relay on that
machine. Play the scenarios above separately for physical input, audio,
exclusive fullscreen and networked play on separate PCs. Record the Windows
version, GPU driver, display mode and which relay was used with each result.
