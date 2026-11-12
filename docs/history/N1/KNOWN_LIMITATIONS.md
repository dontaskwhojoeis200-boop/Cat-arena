# Current known limitations — N1

- N1 is an offline conversion and native IO milestone. The Vulkan renderer still
  draws the N0 triangle; it does not yet load/render this map data.
- N0 graphical startup failed with No available video device. No native scene
  screenshot, GPU benchmark, validation-layer clean run or gameplay parity exists.
- Windows x64 and Android ARM64 builds were not performed. No new EXE/APK supplied.
- Navigation export includes authored connectivity, walkable slabs and spatial
  metadata, not a baked dynamic A* graph. Collision queries, physics and enemy
  pathfinding must be implemented and tested in their later milestones.
- All four lightmap hashes match. This validates layout/data compatibility, not
  perceptual lighting parity or actual Vulkan sampling orientation.
- Canvas textures are rasterized with pinned Skia. Two runs here are byte-identical;
  browser-vs-Skia and cross-OS pixel parity remain unverified.
- Legacy fallback light-volume vertex colors are not recomputed. Authored colors
  and kit fallback colors are preserved; the matched shipped lightmap/vertex-luxel
  path is exported. Reconstruct fallback lighting if the native app exposes it.
- Material stages/deforms/portals/MD3 rigs and sprite data are exported, not native
  shader/runtime implementations. Full texture-alpha/portal/fog fidelity remains.
- Cat Marine skeleton/clips/hitboxes are the original assets. Native playback and
  local-first-person exclusion are not implemented until N6.
- Kit glTFs are diffuse inspection exports of fitted embedded buffers. The original
  upstream GLB files referenced by source hashes are not present; none are invented.
- Source source-order declarations are extracted, not all runtime gameplay effects.
  Actual weapon/AI/movement behavior must still be reconstructed from source.
- Asset readers have explicit bounds/CRC checks; no security certification or
  arbitrary-untrusted-JS execution support is claimed. Offline VM accepts only
  the hash-pinned project. Native runtime uses no JS.
- ASan/UBSan checks passed with leak detection disabled; LeakSanitizer remains
  unavailable in this runner. Original archives remain private reference inputs.
