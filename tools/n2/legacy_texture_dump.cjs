#!/usr/bin/env node
// N2 texture audit, step 1: read back the textures the UNMODIFIED legacy game actually samples
// while rendering a map, straight from its WebGL2 texture objects (level 0, raw upload order).
//
//   node tools/n2/legacy_texture_dump.cjs --out <dir> [--map helios-crucible]
//
// Writes raw RGBA8 files: <dir>/world_array_gpu.rgba (16 x 128px layers, 4x4 grid, upload order),
// <dir>/material/<n>.rgba, <dir>/kit/<n>.rgba (Q3MAT arrays), <dir>/lightmap_gpu.rgba and
// <dir>/texture_dump.json (names, layer indices, sizes, source hash, renderer).
// Step 2 (tools/n2/audit_textures.py) compares these with the N1 bake and writes the runtime
// world atlas used by N2. Requires Node >= 22 + playwright Chromium with WebGL2.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');
const { chromium } = require('playwright');

(async () => {
  const a = { out: null, map: 'helios-crucible' };
  const v = process.argv.slice(2);
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') a.out = v[++i]; else if (v[i] === '--map') a.map = v[++i]; else throw new Error('unknown option ' + v[i]); }
  if (!a.out) throw new Error('usage: --out <dir> [--map id]');
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch');
  for (const d of ['', 'material', 'kit']) fs.mkdirSync(path.join(a.out, d), { recursive: true });
  const server = http.createServer((q, r) => { r.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); r.end(bytes); });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${a.map}`, { timeout: 900000 });
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => active === true, null, { polling: 500 });
    const dump = await page.evaluate(() => {
      const fb = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
      // Raw bytes, base64. NOT via a 2D canvas: canvases store premultiplied alpha, which zeroes RGB
      // under alpha 0 and quantises translucent texels (an earlier revision of this tool did that).
      const png = (w, h, px) => { let s = ''; for (let i = 0; i < px.length; i += 32768) s += String.fromCharCode.apply(null, px.subarray(i, i + 32768)); return { w, h, b64: btoa(s) }; };
      const layer = (tex, n, l) => { gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, tex, 0, l); if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) throw new Error('incomplete fb'); const px = new Uint8Array(n * n * 4); gl.readPixels(0, 0, n, n, gl.RGBA, gl.UNSIGNED_BYTE, px); return px; };
      const out = { world: null, material: [], kit: [], lightmap: null };
      // world array: 16 x 128
      const grid = new Uint8Array(512 * 512 * 4);
      for (let m = 0; m < 16; m++) { const px = layer(Q3R.worldArray, 128, m); for (let y = 0; y < 128; y++) grid.set(px.subarray(y * 512, y * 512 + 512), (((m >> 2) * 128 + y) * 512 + (m & 3) * 128) * 4); }
      out.world = png(512, 512, grid);
      // Q3MAT material array (256 px): names resolved through the renderer's own layer table
      const names = [...Q3MAT.materials.values()].flatMap(m => m.stages.filter(s => s.src === 0).flatMap(s => (s.maps || [s.map]).filter(Boolean)));
      for (const name of [...new Set(names)]) { const idx = Q3MAT.textureLayer(name); out.material.push({ name, layer: idx, png: png(256, 256, layer(Q3MAT.texArray, 256, idx)) }); }
      for (const [name, idx] of Q3MAT.kitLayers) out.kit.push({ name, layer: idx, png: png(512, 512, layer(Q3MAT.kitArray, 512, idx)) });
      // lightmap atlas
      const info = Q3LM.info; gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, Q3LM.texture, 0);
      const px = new Uint8Array(info.w * info.h * 4); gl.readPixels(0, 0, info.w, info.h, gl.RGBA, gl.UNSIGNED_BYTE, px);
      out.lightmap = { w: info.w, h: info.h, hash: info.hash, source: info.source, png: png(info.w, info.h, px) };
      gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.deleteFramebuffer(fb);
      const e = gl.getExtension('WEBGL_debug_renderer_info'); out.renderer = gl.getParameter(e ? e.UNMASKED_RENDERER_WEBGL : gl.RENDERER);
      return out;
    });
    const save = (rel, raw) => fs.writeFileSync(path.join(a.out, rel), Buffer.from(raw.b64, 'base64'));
    save('world_array_gpu.rgba', dump.world);
    save('lightmap_gpu.rgba', dump.lightmap.png);
    const index = { tool: 'tools/n2/legacy_texture_dump.cjs', source: 'legacy/index.html', sha256: sha, map: a.map, renderer: dump.renderer,
      format: 'headerless RGBA8, rows in GPU upload order (row 0 = first uploaded row = texture t=0); sizes: world 512x512 (4x4 layers), material 256, kit 512, lightmap w x h',
      lightmap: { w: dump.lightmap.w, h: dump.lightmap.h, hash: dump.lightmap.hash, source: dump.lightmap.source }, material: [], kit: [] };
    dump.material.forEach((m, i) => { const f = `material/${i}.rgba`; save(f, m.png); index.material.push({ name: m.name, layer: m.layer, file: f }); });
    dump.kit.forEach((m, i) => { const f = `kit/${i}.rgba`; save(f, m.png); index.kit.push({ name: m.name, layer: m.layer, file: f }); });
    fs.writeFileSync(path.join(a.out, 'texture_dump.json'), JSON.stringify(index, null, 2) + '\n');
    console.log(`dumped world array, ${index.material.length} material, ${index.kit.length} kit textures, lightmap ${index.lightmap.w}x${index.lightmap.h}`);
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
