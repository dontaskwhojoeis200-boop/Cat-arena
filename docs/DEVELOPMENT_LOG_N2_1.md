# N2.1 development log — 2026-11-09 / 2026-11-10

Scope: Vulkan compatibility and scalability refactor on top of the completed N2 package. N3 was
not started.

1. **Audit.** Read the N0 initialization and the N2 renderer for every version-dependent item
   (table in N2_1_VULKAN_COMPATIBILITY.md section 1). Result: the run-time code called only Vulkan
   1.0 + WSI commands; the 1.4 dependency was `find_package(Vulkan 1.4 REQUIRED)`, the device
   minimum was 1.1, and nothing was detected at run time beyond `samplerAnisotropy`.
2. **Environment probe.** A small probe listed the device's versions, extensions and feature bits.
   Finding: `MESA_VK_VERSION_OVERRIDE` has no effect on this lavapipe build, so an older device
   could not be emulated through the driver. Decision: write a small test layer instead.
3. **Capability model** (`cat/vulkan_caps.hpp`, `vulkan_caps.cpp`): GPU-free, no Vulkan headers;
   `DeviceCaps` (detected), `Request` (allowed), `Path` (selected, with reasons), `select_path`.
   Unit-tested with synthetic devices before the renderer used it.
4. **Headers.** Vendored the Vulkan C headers (1.4.309, 1.3 MB, per-file hashes) so the build is
   independent of the installed SDK; the loader library is linked without the SDK include path.
5. **Renderer refactor in place.** Detection, selection, `VkPhysicalDeviceFeatures2`-based device
   creation and run-time entry-point resolution in `init()`; barrier/submit helpers with
   synchronization2 or legacy calls; dynamic rendering beside the render pass; extended dynamic
   state for material passes; push descriptors beside the dynamic-offset set; host image copy
   beside staging uploads. The N2 code paths are kept verbatim as the fallbacks.
   Compile issues fixed: a local `caps` shadowing the new member, and an enum/unsigned conditional
   under `-Werror`.
6. **First path check.** Auto, baseline, 1.2+extensions and 1.3-core renders of the seven poses were
   byte-identical to the N2 golden PNGs with 0 validation messages on the first run.
7. **Test instruments.** `VK_LAYER_CAT_device_sim` (clamps the API version, hides extensions,
   masks feature bits, rejects over-requesting in `vkCreateDevice`, hides commands using a table
   generated from `vk.xml`), a self-test of that layer, the path-matrix test, the link-import
   test (with a negative check), and `tools/n2_1` scripts for the 1280x720 golden comparison.
8. **Window runs.** Added `--resize-test`; ran six paths under Xvfb with FIFO + live resize and
   MAILBOX. This is the first execution of swapchain recreation (NOT_RUN in N2). Presented image
   equals the offscreen capture on baseline and auto.
9. **Extra coverage.** Sanitizer build now includes the GPU tests (13/13); clang 18 build; build
   with a 1.2.198 SDK header directory; simulated device whose entry points do not resolve, which
   exercises the run-time drop of all five features inside the renderer.
10. **Regression.** Core Debug/Release, desktop Debug/Release/ASan/clang all pass; legacy,
    reference and assets verified unchanged against the N2 manifest.

Things that did not work or were decided against
- Driver-level version override (step 2): unavailable, replaced by the layer.
- Changing the shader target to `vulkan1.2`: not needed (SPIR-V 1.3 is valid on 1.2) and it would
  have changed the shader binaries for no gain.
- Loading *all* Vulkan commands dynamically (volk-style): not needed; linking 1.0 + WSI is safe on
  every loader and is now enforced by a test.
- Frame-time measurements: not taken; a CPU rasterizer would not represent GPU behaviour.

Not done
- No hardware GPU, real 1.2/1.3 driver, Windows or Android run. No EXE/APK.
