CAT ARENA: FERAL CLAW — native Windows build (milestone N8)
=========================================================

A native C++20 / Vulkan game (no browser, no JavaScript runtime). This folder holds everything the
game needs except the graphics driver.

Requirements
------------
- Windows 10 or 11, 64-bit (x64).
- A graphics card and driver with Vulkan 1.2 or newer (NVIDIA, AMD and Intel drivers of the last
  several years install Vulkan automatically). Nothing else needs to be installed: no Visual C++
  Redistributable (the runtime is built in), no Vulkan SDK, no development tools.
- About 100 MB of disk space.

Starting the game
-----------------
1. Extract the ZIP (right-click > Extract All...). Keep the folder together: CatArena.exe needs
   SDL3.dll and the assets folder next to it. Do not run the game from inside the ZIP window.
2. Double-click CatArena.exe. Windows SmartScreen may warn that the program is unrecognized
   (it is not code-signed): choose "More info" > "Run anyway".
3. The game opens on the main menu (PLAY / MULTIPLAYER / SETTINGS / EXIT). PLAY shows the four
   arenas with a live view of each; ENTER ARENA loads the map, and ENTER THE ARENA on the loading
   screen captures the mouse and starts the game (the game switches to fullscreen when it starts;
   turn "Fullscreen when launching" off in SETTINGS > HUD / UI). Sound and music start with the
   first click on PLAY.
   The menus work with the mouse, the keyboard (arrows, Enter, Esc, Tab, Ctrl+Tab or PageUp /
   PageDown for the settings tabs), a touch screen or a gamepad (D-pad, A, B, bumpers).
   Settings are saved in %APPDATA%\CatArena\FeralClaw\settings.txt.

Controls
--------
  Mouse                 look / aim
  Left mouse button     fire (hold for automatic weapons)
  W A S D               move
  Shift                 sprint
  Space                 jump
  Ctrl                  crouch
  C                     slide
  R                     reload
  Q or Tab              swap to the other weapon (a kill replaces the weapon in hand, as in the original)
  F (hold)              multiplayer: revive a downed teammate (stand next to them for 4 seconds)
  1 2 3 4               change map: Helios Crucible, Bastion Keep, Reactor Bastion, The Arsenal
  V                     settings
  F5                    restart the current map at wave 1
  F9                    renderer debug views (lightmap only, fullbright, normals, off)
  F11                   fullscreen on / off
  Esc                   pause menu: resume, settings, customize HUD, choose another map, main menu
                        (Esc again resumes; EXIT on the main menu quits, so does closing the window)
  Gamepad               left stick move, right stick look, RT / RB fire, A jump, B crouch,
                        right stick click slide, X reload, Y / LB swap, left stick click sprint, Start pause,
                        D-pad down (hold) revive

Playing
-------
Enemies come in waves (20 types, including three flyers). Each wave is larger and stronger
(+5 % health, speed and damage per wave). The wave box at the top shows the wave, how many enemies
are left and how many are active; when five or fewer are left they are outlined through walls.
Medkits restore health. If you die, you respawn and the waves start again at wave 1 (or press F5).

Multiplayer (up to 4 players, co-op against the waves)
------------------------------------------------------
MULTIPLAYER > HOST A SERVER: pick the arena, type your player name (and a server name), then
  - HOST ON THIS PC: the game runs the server itself on port 8787. Friends on the same network type
    this PC's address in SERVER ADDRESS (for example 192.168.1.20:8787) and press JOIN. Windows
    Firewall asks the first time whether CatArena.exe may accept connections: allow it for private
    networks. Over the internet the port has to be forwarded on your router (not tested).
  - CREATE SERVER: uses the relay server typed in SERVER ADDRESS (the original Cat Arena relay,
    reference/multiplayer/server.js, or `CatArena.exe --relay-server` on another computer).
MULTIPLAYER > JOIN A SERVER lists the servers on the SERVER ADDRESS relay (refreshed every 9 s);
press JOIN on one. You load the host's arena and spawn next to the host.
The host runs the enemies and the waves; everyone's health, kills and shots are synchronised. At 0
health you are DOWN: you can look around but not move or shoot until a teammate holds F next to you
for 4 seconds. The wave restarts at 1 only when every player is down. The menu (Esc) does not pause
a multiplayer game. If the host leaves, the server closes and the others return to the menu.

Command-line options (optional; from a Command Prompt in this folder)
---------------------------------------------------------------------
  CatArena.exe --map bastion-keep     skip the menu: go to the loading screen of this map (helios-crucible, bastion-keep, reactor-bastion, q3-gallery)
  CatArena.exe --wave 5               start at wave 5
  CatArena.exe --enemies practice     static practice targets instead of waves
  CatArena.exe --no-vsync             do not wait for the display refresh
  CatArena.exe --relay-server         run only the multiplayer server (no window; port 8787, or --port N)

Troubleshooting
---------------
- Every run writes CatArena.log next to CatArena.exe (Vulkan device, driver, loading times, errors).
  If the folder is not writable (e.g. inside Program Files), move the game folder to Documents or Desktop.
- "Vulkan is not available: vulkan-1.dll ... was not found": install or update the graphics driver.
- An error naming a Vulkan device or feature: the GPU or driver does not offer Vulkan 1.2; update the
  driver. On PCs with two GPUs, make sure the dedicated one is used.
- A message box shows any error that stops the game; the same text is in CatArena.log.

Known limitations of this build
-------------------------------
- Multiplayer has been tested with four game clients on one computer (automated tests, also with the
  original relay server.js); play between separate Windows PCs, over the internet, through NAT or
  with a secure wss:// relay has not been tested yet.
- A server keeps the map it was created on (CHOOSE ANOTHER MAP and the 1-4 keys are disabled in it).
- Some settings are kept and saved but do not change the picture yet (they say so on the settings
  page): scene resolution, multisample / FXAA anti-aliasing, adaptive resolution, the three world
  culling modes, texture quality and filtering, lightmap filter, curve detail, mirrors (milestone N10).
- Mirrors / portal views, detailed prop models and some lens effects of the original are not drawn yet.
- If Windows has no sound device the game runs silent and the menus say so.
- This Windows build is produced by the GitHub Actions workflow in the source package, which also
  runs the game's GPU-free tests on Windows (including the replays of recordings of the original
  game). The runner has no graphics card, so the game has not yet been played on Windows hardware by
  the developers. Please report what you see (and attach CatArena.log).
- Third-party licences: THIRD_PARTY_LICENSES.txt.
