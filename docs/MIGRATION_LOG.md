# Native migration log

| Tier | Result |
| --- | --- |
| N7 | Preserved the submitted ZIP. Rebuilt it locally and passed all 35 GPU-free checks. |
| N8 | Verified the existing native co-op implementation at GitHub commit `a0fd083bd4f8d78bdbf81e19b9b19a8796a23d14`. Windows CI passed 42 GPU-free checks and packaged the executable. Local Linux verification passed 39 GPU-free tests and two offscreen software-Vulkan tests. |
| N9 | Windows platform integration. See the current tier's development report. |

The [Windows roadmap](WINDOWS_ROADMAP.md) governs N8–N20. For the N8 feature
list, commands, and outstanding hardware checks see [README_N8](../README_N8.md).
