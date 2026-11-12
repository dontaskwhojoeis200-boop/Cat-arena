# N0 known limitations

- Engine boot milestone only. Four-map/16-weapon/20-enemy and multiplayer/UI/audio
  parity are future requirements, not implemented features.
- No graphics runtime verification. This runner lacks a usable SDL video device;
  its SDL build disabled unavailable X11/Wayland backends. No configured Vulkan
  ICD was present. Loader/header availability alone does not imply a GPU.
- Windows and Android were not compiled. No native EXE/APK deliverable exists.
  N9 is the real Android application/lifecycle milestone; N0 documents provisional
  cross-building of the platform-independent core only.
- LeakSanitizer cannot complete under this runner; ASan/UBSan passed with leak
  detection disabled. Resource leak freedom has not been proven on graphics paths.
- Renderer deliberately requires a combined graphics/present queue and Vulkan1.1+.
  It has no optional advanced features, depth buffer, textures, descriptor sets,
  asset buffers, GPU timings, multithreaded submission or pipeline cache yet.
- Vulkan1.4 header build baseline is checked; runtime request is negotiated up to
  1.4. This does not mean any 1.4-specific rendering feature is implemented.
- Resize uses an idle-wait, destroy/rebuild strategy appropriate to this shell;
  advanced present-fence/retirement support and performance tuning are future work.
  Device/surface loss exits with an error, without automatic recovery.
- Snapshot timing is exposed but the triangle is static. There is no gameplay
  state interpolation, full mouse-look capture, touch, gamepad or pause UI yet.
- Existing archival dependencies/signing material/assets remain private references.
  Do not publish this complete private handoff as a shipping asset package. N11
  must separately curate dependencies, rights and release material.
- SDK1.4.363.1 mentioned in the master specification was not installed here;
  the tested headers/loader are1.4.309. No claim of testing the latest SDK is made.
