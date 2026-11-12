# N2 development log — 2026-11-09

1. Continued from the supplied N1 package (CAT_ARENA_FERAL_CLAW_NATIVE_N1_SOURCE.zip); read the
   master/phase prompts, N1 progress, format spec, limitations and all native sources.
2. Read the legacy renderer at its source: main world shader (850-1181), atlas construction and
   upload (1195-1215, 2802-2846), Q3MAT program generation, stage state, static clusters and sky
   (3643-4462), wave/tcMod/colour evaluation (2149-2285), lightmap upload (4908-4931), frame order
   (13572-13662), camera (10513-10541), inView (5302-5317), themes, lightVertexColor (5226-5262).
3. Runner capabilities found: Mesa lavapipe (software Vulkan 1.4), Xvfb, headless Chromium with
   WebGL2 on lavapipe, git access through the proxy. No package installs. Built SDL3 3.2.10 (N1
   pin) with X11, glslang 15.2.0 and Vulkan-ValidationLayers from source.
4. Found an N1 data gap: the legacy multiplies world vertex colours by `lightVertexColor` before
   lightmap charting; N1 captured the stream earlier. Ported it exactly, including `levelWalkable`
   (footprint and massive-solid tests) for `mapCell`. Golden-value tests.
5. Wrote `cat_arena_scene` (GPU-free): typed material model covering all fields used by the four
   maps, legacy camera/projection/inView, wave/noise/tcMod evaluation, buildStatic clustering,
   texture arrays (256/512), sky dome. Vendored stb (pinned, hashed) for PNG/JPEG.
6. Rewrote the renderer: depth, staging uploads, CPU mips, texture arrays, descriptors, dynamic
   per-pass material UBO, sky/world/material pipelines, legacy frame order, offscreen target with
   readback; UNORM swapchain to match the WebGL canvas.
7. First frames looked plausible, but the normals winding test showed floor tops being culled:
   front face had been reasoned wrongly (COUNTER_CLOCKWISE). Corrected to CLOCKWISE with the spec
   justification; the test now guards it.
8. Built a legacy reference tool (unmodified HTML, hash-checked) for headless Chromium. Fixed its
   menu flow (hidden #start, ENTER THE ARENA deploy click) and replaced compositor screenshots
   (renderer crash) by readPixels after a synchronous `render()`.
9. First comparison: geometry and colour aligned, but floors/walls used different tiles. Ruled out
   in turn by measurement: world vertex data (GPU buffer read back: all 7,308 triangles identical
   except the expected vertex-light factor), program binding, sampler unit, portal state leaks.
   Root cause: the legacy world array is built once at startup (line 2996) from the original atlas;
   `paintWorldMaterials` (8980) repaints `texCanvas` afterwards but never reaches the array. N1 baked
   the repainted canvas. Captured the runtime array and audited every texture the game samples.
10. A first audit wrongly suggested Chromium premultiplied the lightmap; the cause was my dump
   tool encoding read-backs through a premultiplied 2D canvas. The full-frame comparison with exact
   PNG bytes (mean error 0.06) exposed it; the emulation was removed and the tool now moves raw bytes.
   The re-run audit shows lightmap and kit textures byte-identical, 43/45 material textures identical.
11. Final comparison at 7 poses: mean 0.03-0.17 levels/channel, PSNR 49-56 dB, >= 99.78% of pixels
   within 8 levels. Validation clean offscreen and in FIFO/MAILBOX window runs; presented window
   pixels equal the offscreen capture. Debug/Release/ASan+UBSan+LSan core suites and Debug/Release
   desktop suites pass. stb JPEG decode checked against the browser-equivalent decode (max 3).
12. Updated trackers, build docs, renderer design note, evidence and manifests. Original legacy,
   reference and N1 asset files are byte-identical to the N1 package.
