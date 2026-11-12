# Cat Arena: Feral Claw — N2.1: Vulkan 1.2 baseline, optional 1.3/1.4 paths

N2.1 implemented and tested, 2026-11-10. This package contains N0, N1, N2 and N2.1 complete; the
original references are unchanged. **N3 has not been started.**

**The engine now requires a Vulkan 1.2 device and treats everything newer as optional.** At start
it reads the device's versions, extensions and feature bits and picks, per feature, the highest
supported route; whatever is missing falls back to the Vulkan 1.2 code. The picture is the same on
every route: all of them reproduce the N2 Helios Crucible frames pixel for pixel.

| Optional feature | Used when | Otherwise (Vulkan 1.2) |
|---|---|---|
| Dynamic rendering | 1.3 core or `VK_KHR_dynamic_rendering`, bit reported | Render pass + framebuffers |
| Synchronization2 | 1.3 core or `VK_KHR_synchronization2`, bit reported | Classic barriers and submit |
| Extended dynamic state | 1.3 core or `VK_EXT_extended_dynamic_state` | One pipeline per state |
| Push descriptors | 1.4 core bit or `VK_KHR_push_descriptor` | Dynamic-offset descriptor set |
| Host image copy | 1.4 optional bit or `VK_EXT_host_image_copy`, and useful for our textures | Staging-buffer upload |

## Start

```sh
cmake -S native -B build-core -G Ninja -DCAT_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Debug   # no GPU needed
cmake --build build-core && ctest --test-dir build-core --output-on-failure

# Desktop: needs SDL3 3.2.10, a Vulkan loader library and glslangValidator/glslc (docs/BUILD.md, N2.1 section)
./build-desktop/cat_arena --vk-info        # what the device offers and what was selected
./build-desktop/cat_arena                  # Helios Crucible, highest supported path
./build-desktop/cat_arena --vk-baseline    # same picture on the mandatory Vulkan 1.2 path
```

## What to read

- docs/N2_1_VULKAN_COMPATIBILITY.md — audit, rules, fallbacks, tests, what was not tested
- docs/TEST_MATRIX.md and docs/n2_1-evidence/ — every result with logs
- docs/DEVELOPMENT_LOG_N2_1.md, docs/N2_1_DIFF.md — what changed and why
- docs/KNOWN_LIMITATIONS.md

Verified here: Linux x86_64, Mesa lavapipe (Vulkan 1.4 software rasterizer), validation clean on
every path; Vulkan 1.1/1.2/1.3 devices simulated with a test-only layer. NOT run: hardware GPUs,
real 1.2/1.3 drivers, Windows, Android. No EXE/APK is supplied.

Next: **N3 — physics and first-person traversal**.
