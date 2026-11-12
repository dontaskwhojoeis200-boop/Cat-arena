| Configuration | Device API | Negotiated | DR | Sync2 | EDS | Push | HostCopy | Render passes / framebuffers | Material pipelines | Descriptor sets | Frames == N2 golden | Validation errors / warnings | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| auto | 1.4.318 | 1.4 | core | core | core | core | core | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| baseline | 1.4.318 | 1.2 | - | - | - | - | - | 1 / 1 | 5 | 2 | 7 / 7 | 0 / 0 | PASS |
| cap12_extensions | 1.4.318 | 1.2 | ext | ext | ext | ext | ext | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| cap13_core | 1.4.318 | 1.3 | core | core | core | - | - | 0 / 0 | 4 | 2 | 7 / 7 | 0 / 0 | PASS |
| cap13_extensions | 1.4.318 | 1.3 | core | core | core | ext | ext | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| cap14_core | 1.4.318 | 1.4 | core | core | core | core | core | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| no_dynamic_rendering | 1.4.318 | 1.4 | - | core | core | core | core | 1 / 1 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| no_dynamic_state | 1.4.318 | 1.4 | core | core | - | core | core | 0 / 0 | 5 | 1 | 7 / 7 | 0 / 0 | PASS |
| no_host_image_copy | 1.4.318 | 1.4 | core | core | core | core | - | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| no_push_descriptor | 1.4.318 | 1.4 | core | core | core | - | core | 0 / 0 | 4 | 2 | 7 / 7 | 0 / 0 | PASS |
| no_synchronization2 | 1.4.318 | 1.4 | core | - | core | core | core | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 0 | PASS |
| sim_vk11_rejected | 1.1 (simulated) | - | - | - | - | - | - | - | - | - | - | - | PASS: device rejected |
| sim_vk12_bare | 1.2.318 | 1.2 | - | - | - | - | - | 1 / 1 | 5 | 2 | 7 / 7 | 0 / 2 | PASS |
| sim_vk12_dr_bit_off | 1.2.318 | 1.2 | - | ext | ext | ext | ext | 1 / 1 | 4 | 1 | 7 / 7 | 0 / 2 | PASS |
| sim_vk12_extensions | 1.2.318 | 1.2 | ext | ext | ext | ext | ext | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 2 | PASS |
| sim_vk13_bare | 1.3.318 | 1.3 | core | core | core | - | - | 0 / 0 | 4 | 2 | 7 / 7 | 0 / 2 | PASS |
| sim_vk13_no_bits | 1.3.318 | 1.3 | - | - | core | - | - | 1 / 1 | 4 | 2 | 7 / 7 | 0 / 2 | PASS |
| sim_vk14_no_hostcopy | 1.4.318 | 1.4 | core | core | core | core | - | 0 / 0 | 4 | 1 | 7 / 7 | 0 / 2 | PASS |
