# Architecture — observed baseline, not a finalized native design

Step 04 is NOT_STARTED. SOURCE_AUDIT.md sections 3–4 contain the current subsystem inventory and observed dependency diagram.

Current engine: classic inline JavaScript scripts in one HTML file, shared globals, custom WebGL2 renderer, procedural WorldKit maps compiled into WORLD/LEVEL_V2, layered collision/navigation, fixed 120 Hz simulation and host-authoritative WebSocket multiplayer. UI and gameplay read shared state and DOM; renderer state is not isolated. Audio is WebAudio/HTML media. Android is a WebView Activity.

Migration constraints accepted from the user: gameplay, physics and networking must not depend on Vulkan; renderer takes explicit visual state; preserve legacy functionality/assets/values; C++20 native application with Windows and Android first, Linux secondary; no full external game engine. CMake/SDL3/VMA/GLM and Vulkan 1.1 are proposals pending dependency/capability evaluation. Resource lifetimes, interpolation and subsystem ownership are not finalized here.

Do not create placeholder modules or treat this record as approval to start Step 09. Final design must cover the audited material stages, portals, MD3 and procedural animation as well as the listed gameplay priorities.

## Step 02 note

Reference capture is external Node/Playwright tooling; it does not alter engine architecture. Matching legacy relay source is now preserved under reference/step02 and copied into tools/baseline/relay for reference tests. No native architecture implemented.
