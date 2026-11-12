# Native rewrite progress

2026-11-09 — N0 IMPLEMENTED; headless acceptance verified, graphical behavior unverified.

Completed: read authoritative master/start/phase prompts; inspect primary/reference
subsystems; preserve original references; create independent C++20/CMake targets;
structured logs, typed settings/read-write validation, paths, exact fixed clock,
Z-up basis, seeded RNG, input actions, move-only cleanup and snapshot boundary;
SDL3 lifecycle/event bridge; Vulkan capability selection, validation callback,
surface/device/queue, swapchain/recreation, render pass, real triangle shaders,
raster pipeline, acquire/submit/present synchronization and teardown; automated
tests, build presets, inventory utility, dependency record and documentation.

Executed: GCC14.2 Debug/Release core; Linux desktop link; GLSL->SPIR-V; all
headless tests; ASan/UBSan with leak detection disabled; source integrity checks.
Graphical startup attempted and failed at SDL video initialization. Full
LeakSanitizer run failed because the runner does not support its tracing needs.
Neither failure is relabeled a pass. Details in TEST_MATRIX and evidence logs.

No native game maps, weapons, enemies, networking, UI or audio have been claimed.
No Windows binary or Android APK was built. Windows and Android device testing,
Vulkan validation execution, visual output and resize/minimize checks remain open.

N1 handoff: use this complete source tree, keep legacy/reference immutable, and
follow CLAUDE_PHASE_PROMPTS.md N1. Build a reproducible offline extractor and
versioned native data format, preserving original meshes/materials/lightmaps,
map callbacks, collision/art distinction, spawns/triggers and navigation links.
Add loader/serialization tests. Do not substitute a test room or claim N2 map
rendering before it exists. Keep N0 checks running. The graphical blocker does
not prevent asset extraction or core testing; validate the triangle on a real
desktop before relying on this renderer for N2.
