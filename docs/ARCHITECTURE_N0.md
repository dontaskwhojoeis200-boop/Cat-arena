# Native N0 architecture

`cat_arena_core` owns no platform or graphics objects and includes no SDL/Vulkan
headers. Settings, paths, time, coordinates, RNG and action state are ordinary
C++20 types. Simulation uses exact rational integer phase (nanoseconds × 120),
with the legacy 55ms frame clamp, 60ms accumulator bound and seven-step limit.
Elapsed excess is observable as dropped duration. The callback receives exactly
1/120 seconds; interpolation is the residual fraction, not a second simulation.

`main.cpp` owns the SDL lifetime, window, event pump and renderer, in that order;
reverse destruction tears Vulkan down before the window and SDL. Input edges
persist until consumed by a simulation tick. Focus loss releases held actions.
SDL scancodes are translated at this boundary. Escape closes the N0 test shell;
full gameplay pause and captured mouse look are later milestones.

`Renderer` is a noncopyable PImpl. Construction allocates its implementation
before initialization, so exceptions clean up every successfully created handle.
It accepts a const Snapshot and does not mutate simulation. N0 geometry is static;
tick/interpolation fields are reserved for N1+ render state and are not yet used
by its shader. There is no placeholder gameplay or fabricated world state.

Vulkan profile: loader/device >=1.1, swapchain extension, a queue supporting both
graphics and presentation, color attachment surface usage, supported surface
format/present mode/composite alpha. Instance version is the lesser of loader
support and 1.4; device support is separately checked and logged. No optional
features are enabled. The 1.1 fallback is intentional for this small pipeline;
future render features must extend the capability contract explicitly.

One CPU frame is in flight. A fence protects the command buffer/acquire semaphore.
Present semaphores belong to swapchain images, not CPU frames. Acquisition errors
are handled before resetting the fence. Resize/out-of-date marks the swapchain
dirty; recreation waits idle and rebuilds all format/extent-dependent objects.
Zero pixel extent or minimization defers work; the application sleeps while
minimized. Surface/device loss exits through structured error logging; recovery
is not implemented. N0 favors simple inspectable ownership over throughput.

Render pass: clear/store one color attachment, implicit attachment/present layout
transitions, external color-output dependency, triangle-list raster pipeline,
dynamic viewport/scissor, no culling, one sample, no depth or vertex buffers.
SPIR-V is built from GLSL, checked for alignment/magic on load, and required at
runtime. Missing shaders fail loudly. Modules are released after pipeline creation.

Validation layer availability is enumerated. If installed/requested, validation
and debug-utils diagnostics are enabled when supported; otherwise a warning says
they are absent. FIFO is the VSync default; VSync-off selects mailbox then immediate,
falling back to FIFO with a warning. Frame cap 0 disables CPU throttling.

Asset/world/collision/gameplay/AI/audio/UI/network modules are not yet authored.
They will be introduced as real vertical slices rather than empty source folders.
SDL/Vulkan are the only runtime dependencies beyond the C++ standard library;
VMA/glm/tinygltf/stb are deferred until a milestone needs them.

API implementation references:
- https://wiki.libsdl.org/SDL3/SDL_Vulkan_GetInstanceExtensions
- https://wiki.libsdl.org/SDL3/SDL_Vulkan_CreateSurface
- https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
