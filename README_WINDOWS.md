CAT ARENA: FERAL CLAW — native Windows build (milestone N6)
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
3. The game opens on Helios Crucible with a panel listing the controls. Click into the window to
   capture the mouse and start playing.

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
  1 2 3 4               change map: Helios Crucible, Bastion Keep, Reactor Bastion, Q3 Gallery
  F5                    restart the current map at wave 1
  F11                   fullscreen on / off
  Esc                   release the mouse and pause; click to resume
  Esc (while paused)    quit (closing the window also quits)

Playing
-------
Enemies come in waves (20 types, including three flyers). Each wave is larger and stronger
(+5 % health, speed and damage per wave). The wave box at the top shows the wave, how many enemies
are left and how many are active; when five or fewer are left they are outlined through walls.
Medkits restore health. If you die, you respawn and the waves start again at wave 1 (or press F5).

Command-line options (optional; from a Command Prompt in this folder)
---------------------------------------------------------------------
  CatArena.exe --map bastion-keep     start on another map (helios-crucible, bastion-keep, reactor-bastion, q3-gallery)
  CatArena.exe --wave 5               start at wave 5
  CatArena.exe --enemies practice     static practice targets instead of waves
  CatArena.exe --no-vsync             do not wait for the display refresh

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
- No sound or music yet (milestone N7), no menus beyond the in-game help panel, no settings screen.
- Mirrors / portal views, detailed prop models and some lens effects of the original are not drawn yet.
- The HUD shows the original's information in a simpler layout.
- This Windows build is produced by the GitHub Actions workflow in the source package. Its gameplay
  was verified on Linux (the same source); the Windows executable itself has not yet been run on
  Windows hardware by the developers. Please report what you see (and attach CatArena.log).
- Third-party licences: THIRD_PARTY_LICENSES.txt.
