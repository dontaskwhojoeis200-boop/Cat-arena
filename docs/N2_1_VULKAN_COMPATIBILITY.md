# N2.1 — Vulkan compatibility and scalability

**Vulkan 1.2 is the mandatory device baseline. Vulkan 1.3 and 1.4 functionality is optional,
detected at run time, selected feature by feature, and every optional feature has a Vulkan 1.2
fallback.** All paths issue the same draws with the same state, so the rendered image does not
depend on the path: on the test device every path reproduces the N2 golden frames byte for byte.

Tested on one device only (Mesa lavapipe, a Vulkan 1.4 software rasterizer). Older devices were
*simulated* with a test layer. No hardware GPU, Windows or Android run exists; see "Not tested".

## 1. Audit of N0–N2 (state before this milestone)

| Area | N0–N2 state | Finding | N2.1 change |
|---|---|---|---|
| Build: `find_package(Vulkan 1.4 REQUIRED)` | Needed a 1.4 SDK just to compile | The only unconditional 1.4 dependency in the project | Pinned C headers vendored (`native/third_party/vulkan-headers`, 1.4.309); any loader library links. Verified with a 1.2.198 SDK header directory |
| Instance: `apiVersion = min(loader, 1.4)` | Asked for 1.4 although nothing above 1.0 was called | Version requested without negotiation | `apiVersion = min(request cap, 1.4)`; device version negotiated separately |
| Device minimum | `apiVersion >= 1.1` | Below the required baseline | `>= 1.2`, older devices skipped with a logged reason; clear error if none remains |
| Capability detection | `samplerAnisotropy` only; device extensions never enumerated | No basis for optional paths | Versions, extension list, core and extension feature bits, host-copy format/layout/performance checks, driver identification (`cat::vk::DeviceCaps`) |
| Feature enabling | `pEnabledFeatures` | Cannot express 1.2+ feature structs | `VkPhysicalDeviceFeatures2` chain; only selected features are enabled |
| Render targets | `VkRenderPass` + framebuffers | Already 1.0-compatible | Kept as the baseline; dynamic rendering added as an option |
| Synchronization | `vkCmdPipelineBarrier`, `vkQueueSubmit` | Already 1.0-compatible | Kept as the baseline behind `image_barrier` / `buffer_barrier` / `submit`; synchronization2 optional |
| Pipelines | One static pipeline per state key | Already 1.0-compatible | Kept; extended dynamic state optional |
| Per-pass uniforms | Allocated set, `UNIFORM_BUFFER_DYNAMIC` offset | Already 1.0-compatible | Kept; push descriptors optional |
| Texture upload | Staging buffer + `vkCmdCopyBufferToImage` | Already 1.0-compatible | Kept; host image copy optional |
| Linked commands | All Vulkan 1.0 + WSI (`vkEnumerateInstanceVersion` was already looked up through `vkGetInstanceProcAddr`) | Fine, but nothing enforced it | Enforced by the `vulkan_link_imports` test (89 imports, all 1.0 + WSI) |
| Shaders | SPIR-V 1.3 (`--target-env vulkan1.1`) | Valid on every 1.2 device | Unchanged, so shader binaries and frames stay identical |
| Swapchain, depth format choice, anisotropy fallback | Runtime-selected | Fine | Unchanged |

So N0–N2 had no run-time dependency on 1.3/1.4 commands; the dependency was at build time, and the
1.1 minimum and missing detection had to change. No system was rebuilt: `renderer.cpp` was edited in
place (455 lines added, 81 replaced) and one GPU-free module was added (`cat_arena_vkcaps`, 258 lines).

## 2. Version negotiation

| Value | Meaning | Rule |
|---|---|---|
| Loader API | `vkEnumerateInstanceVersion` | Must be >= 1.1 (physical-device query entry points). Only instance-level |
| Instance API | `VkApplicationInfo::apiVersion` | `min(request cap, 1.4)`. With a 1.2 cap the validation layer checks the run as a 1.2 application |
| Device API | `VkPhysicalDeviceProperties::apiVersion` | Must be >= 1.2, else the device is skipped |
| Negotiated API | `min(device, request cap, 1.4)` major.minor | Core commands and aggregated feature structs up to this version may be used |

Device choice among baseline-capable devices is unchanged from N0 (discrete > integrated > virtual >
CPU, graphics queue, plus swapchain + present support for a window).

## 3. Optional features and their fallbacks

Selection is per feature (`cat::vk::select_path`, `native/src/vulkan_caps.cpp`), highest source first:

1. **core** — the feature is promoted at the negotiated API **and** its feature bit is reported;
2. **extension** — negotiated API is older, the extension (and its dependencies) is advertised
   **and** its feature bit is reported;
3. otherwise **unavailable** — the Vulkan 1.2 fallback runs.

| Feature | Core since | Extension | Used for | Vulkan 1.2 fallback | Effect when used |
|---|---|---|---|---|---|
| `dynamic_rendering` | 1.3 (`dynamicRendering` bit) | `VK_KHR_dynamic_rendering` | Frame targets, pipeline creation | `VkRenderPass` + one framebuffer per target image | No render-pass/framebuffer objects; no framebuffer rebuild on resize |
| `synchronization2` | 1.3 (`synchronization2` bit) | `VK_KHR_synchronization2` | All barriers and queue submits | `vkCmdPipelineBarrier`, `vkQueueSubmit` | Same dependencies, expressed per barrier |
| `extended_dynamic_state` | 1.3 (mandatory, no bit) | `VK_EXT_extended_dynamic_state` (bit) | Cull mode, depth write, depth compare of material passes | One static pipeline per state combination | Helios: 4 material pipelines instead of 5 |
| `push_descriptor` | 1.4 (`pushDescriptor` bit) | `VK_KHR_push_descriptor` (no bit) | Per-pass `Draw` uniform block | Allocated set with dynamic uniform offset | 1 allocated descriptor set instead of 2 |
| `host_image_copy` | 1.4 (`hostImageCopy` bit, optional) | `VK_EXT_host_image_copy` (+`VK_KHR_copy_commands2`, `VK_KHR_format_feature_flags2` below 1.3) | Texture uploads | Staging buffer + `vkCmdCopyBufferToImage` | Helios: 35,177,752 texture bytes copied without staging buffers or submissions |

Rules that keep "version" and "support" apart:

- A feature bit is always read; the version alone enables nothing. Core bits come from
  `VkPhysicalDeviceVulkan13Features` / `14Features`, which are queried only when the negotiated API
  defines them; extension bits come from the extension structs, queried only when the extension is
  advertised. The two kinds are queried in separate chains (mixing them is invalid).
- `host_image_copy` additionally needs `HOST_IMAGE_TRANSFER` support for `R8G8B8A8_UNORM`,
  `SHADER_READ_ONLY_OPTIMAL` as a copy destination layout and `optimalDeviceAccess == true`
  (otherwise host-transfer images would sample slower, so staging is kept).
- Only selected features are enabled on the device, and only their extensions.
- Entry points newer than 1.0 are resolved with `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`
  (core name for core, KHR/EXT name for extension). If one is missing the feature is dropped before
  any resource exists and the fallback runs; the reason is logged and reported.

Visual quality is identical on every path by design. The optional paths reduce object counts and
upload work; **no frame-rate benefit is claimed** because the only test device is a CPU rasterizer.

## 4. Controlling and inspecting the path

```sh
cat_arena --vk-info                      # detected capabilities + selected path (JSON), then exit
cat_arena --vk-baseline                  # force the mandatory Vulkan 1.2 path
cat_arena --vk-max-api 1.3               # cap the API (1.2 | 1.3 | 1.4)
cat_arena --vk-no-extensions             # core-promoted features only
cat_arena --vk-disable dynamic_rendering,host_image_copy
```

All options work in window and `--offscreen` mode. The log prints the device, driver, the three
API versions, the selected path and one reason line per feature; `--report` files contain the same
under `"vulkan"`. In code: `cat::Renderer(..., cat::vk::Request)`; `Renderer::report()` returns
`caps` (detected) and `path` (selected). Default is the highest path the device supports.

## 5. Platform notes

- **Simulation**: `cat_arena_core` is untouched; the 120 Hz fixed step and its tests are unchanged.
- **Windows x64 / Linux**: only Vulkan 1.0 + WSI symbols are imported, so any loader starts the
  program; a pre-1.2 driver produces the "No Vulkan 1.2+ device" error instead of a load failure.
- **Android ARM64**: the same property matters more there, because `libvulkan.so` exports only what
  the platform release knows. The baseline is the path a 1.2 phone would run. The Android target
  itself is still N9 (the CMake file rejects `CAT_BUILD_DESKTOP` on Android), so nothing was compiled
  or run for Android.
- **Consequence of the 1.2 minimum**: devices that only offer Vulkan 1.1 (N0–N2 accepted them) are
  now rejected. That is the requested policy; it mainly affects older Android phones.
- Vulkan 1.2 itself is used for driver identification (`VkPhysicalDeviceVulkan12Properties`); the
  baseline render path needs only 1.0 device commands.

## 6. Tests performed (2026-11-10, logs in `docs/n2_1-evidence`)

Device: llvmpipe (LLVM 20.1.2), Mesa lavapipe, device API 1.4.318, loader 1.3.275, 161 device
extensions, Khronos validation layer 1.4.309 active.

| Test | What it proves | Result |
|---|---|---|
| `vulkan_path_selection` (GPU-free) | Selection rules against synthetic 1.0–1.5 devices: baseline, extension paths, missing bits, missing dependencies, caps, per-feature opt-out, run-time drop | PASS, 69 checks |
| `render_path_matrix` | 17 requests on the installed driver (auto, baseline, 1.2+ext, 1.3, 1.3 core, 1.4 core, each feature off, each feature alone, all off): selection matches the detected capabilities, created objects match the features, 4 frames byte-identical across paths, 0 validation errors | PASS 17/17 |
| `vk_sim_layer_selftest` | The test layer really hides versions, extensions, feature bits and commands, and rejects over-requesting | PASS, 31 checks |
| `render_path_simulated_devices` | Same checks on simulated devices: bare 1.2, 1.2 with extensions, 1.2 with an extension whose bit is off, bare 1.3, 1.3 without feature bits, 1.4 without `hostImageCopy`, 1.4 without any optional bit, 1.4 with unresolvable entry points, and a 1.1 device (rejected) | PASS 11/11 |
| `vulkan_link_imports` | Render library and executable import only Vulkan 1.0 + WSI commands (negative check: fails when a symbol is not on the list) | PASS, 89 imports |
| Golden frames, 1280x720, 7 poses, 17 rendering configurations | Every path, including the simulated bare 1.2 device, equals the N2 golden frames pixel for pixel | PASS 7/7 each (`matrix/matrix_summary.md`) |
| Window (Xvfb), 6 paths, FIFO 120 frames with a live resize, MAILBOX 300 frames | Swapchain path incl. recreation on baseline, auto, 1.2+ext, 1.3 core, dynamic-rendering-only, sync2-only; 0 validation errors/warnings | PASS |
| Presented window image vs offscreen capture (baseline, auto) | Maximum difference 0 | PASS |
| Full CTest: desktop Debug, Release, ASan+UBSan+LSan, clang 18 | N0–N2 regression plus the above, 13 tests each | PASS 13/13 each |
| Core-only Debug / Release | GPU-free build, 7 tests | PASS 7/7 each |
| Build with a Vulkan 1.2.198 SDK header directory | No dependence on SDK header version | PASS |

**How the 1.2 path was validated independently.** Three separate mechanisms, none of which runs any
1.3/1.4 code:
1. `--vk-baseline` on the real device: instance created as a 1.2 application, no optional extension
   or feature enabled; the validation layer would report any newer command or structure.
2. `VK_LAYER_CAT_device_sim` (test-only, `native/tests/vk_device_sim_layer.cpp`): the device reports
   API 1.2 with no optional extension; `vkCreateDevice` fails if a hidden extension or feature is
   requested, and every command newer than 1.2 resolves to NULL. Auto-selection on that device
   picks the baseline and renders the golden frames.
3. The link-import test: nothing newer than 1.0 can be called without an explicit run-time lookup.

## 7. Not tested

- Any hardware GPU or vendor driver (NVIDIA, AMD, Intel, Qualcomm, ARM, Apple/MoltenVK).
- A real Vulkan 1.2 or 1.3 driver. Simulation proves the engine neither requests nor resolves
  anything such a device lacks; it cannot reproduce old-driver behaviour or bugs.
- Windows x64 and Android ARM64 builds and runs (no toolchain or device in this environment).
- Depth formats with stencil (`D32_SFLOAT_S8`, `D24_S8`) on the dynamic-rendering path: lavapipe
  selects `D32_SFLOAT`, so the combined depth+stencil transition was not executed.
- A device where `optimalDeviceAccess` is false, or where the host-copy format/layout checks fail:
  covered by selection unit tests only.
- Present modes other than FIFO and MAILBOX; surface transforms; multiple GPUs.
- Performance. Object counts are measured; frame times are not.
