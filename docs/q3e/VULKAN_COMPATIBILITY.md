# Vulkan compatibility — Quake3e renderer (`renderervk`)

Read from `engine/quake3e/code/renderervk/vk.c` at dec793de; runs as noted.

## Requirements found in the source

| Item | Release build | Debug build |
|---|---|---|
| Instance `apiVersion` | Vulkan **1.0** (`vk.c` 1350) | 1.1 (`vk.c` 1348) |
| Required device feature | `fillModeNonSolid` — device creation fails without it (`vk.c` 1767) | same |
| Optional device features (used when present, `vk.c` 1782–1801) | `wideLines` (axis drawing), vertex + fragment stores and atomics, `samplerAnisotropy` (when `r_ext_texture_filter_anisotropic`) | plus `shaderInt64` |
| Device extensions | `VK_KHR_swapchain` | plus timeline semaphore, memory model, buffer device address, 8-bit storage (all inside `#ifdef _DEBUG`) |
| Offscreen features (`r_fbo 1`) | MSAA, supersampling, HDR targets, bloom, gamma; extra render targets — memory cost to be measured (MEMORY_BUDGET.md) | — |

So the release renderer needs less than the handoff's Vulkan 1.1 target: a Vulkan 1.0 instance,
one widely supported core feature and the swapchain. Cat Arena additions must keep this: anything
newer goes behind a runtime check with a fallback, as in N9.

## GTX 660 (Kepler)

- NVIDIA's last driver branch for Kepler GeForce cards is R470 (security updates 473.xx / 474.xx
  after support ended in 2021). R470 drivers expose Vulkan 1.2 (471.11: Vulkan 1.2.175).
  Sources: [Geeks3D, 471.11](https://geeks3d.com/20210623/nvidia-geforce-471-11-graphics-driver-released-vulkan-1-2-175),
  [TechPowerUp, 473.47](https://techpowerup.com/294913/nvidia-releases-security-update-473-47-whql-driver-for-kepler-gpus),
  [TechSpot, end of Kepler support](https://www.techspot.com/news/91723-nvidia-officially-ends-driver-support-kepler-gpus-windows.html).
- `fillModeNonSolid` and `samplerAnisotropy` are core features of desktop NVIDIA hardware since
  long before Kepler; expected present, **not verified** on the owner's card.
- **Status: Unverified.** No run on a GTX 660 yet. The first Cat Arena-branded build (Q3E-B) logs
  `\vkinfo`-style device data at start so the owner's first run records driver version, API version
  and features.

## Runs so far

| Device | Result | Status |
|---|---|---|
| lavapipe (Mesa 25.2.8, llvmpipe, Vulkan 1.4.318), Linux, Xvfb | instance, device, swapchain (IMMEDIATE, 3 images, B8G8R8A8_UNORM, D24S8), renderer init, UI drawn | Executed |
| Windows CI runner (no GPU) | client built; not started (no Vulkan device) | Compiled |
| GTX 660, Windows | — | Unverified |
