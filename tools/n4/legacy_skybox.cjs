#!/usr/bin/env node
// N4: extracts a procedurally generated sky box from the UNMODIFIED legacy game.
//
//   node tools/n4/legacy_skybox.cjs --map q3-gallery --out assets/n4/skybox-env-gallery_nebula.png [--report file.json]
//
// The Arsenal's sky material is `skyParms env/gallery_nebula 192 -`. The legacy has no image files
// for that box: Q3Tex.skyBoxes['env/gallery_nebula'](suffix, 512) paints each face in JavaScript
// at load and uploads it to the six-layer array texture (uploadSkyBox, legacy/index.html 4366).
// This tool calls the same generator with the same arguments and stores the six 512x512 RGBA
// faces, stacked top to bottom in the legacy layer order rt, bk, lf, ft, up, dn, as one PNG.
// Nothing is repainted or filtered. The generator is called twice and must return identical bytes.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto'), zlib = require('zlib');
const { chromium } = require('playwright');
function png(width, height, rgba) {
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0); return Buffer.concat([len, body, crc]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc(height * (width * 4 + 1));
  for (let y = 0; y < height; y++) rgba.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
(async () => {
  const v = process.argv.slice(2); let out = null, reportFile = null, map = 'q3-gallery';
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') out = v[++i]; else if (v[i] === '--report') reportFile = v[++i]; else if (v[i] === '--map') map = v[++i]; else throw new Error('unknown option ' + v[i]); }
  if (!out) throw new Error('usage: --out <file.png> [--map id] [--report file.json]');
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => { const url = new URL(req.url, 'http://localhost'); if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  try {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${map}`, { waitUntil: 'load', timeout: 900000 });
    // Start a solo game as the other reference tools do, so the map's materials are registered.
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: 900000, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true, null, { timeout: 900000, polling: 500 });
    await page.evaluate(() => { fovPanelOpen = true; });
    const info = await page.evaluate(() => {
      const shader = (WORLD.meta.sky || {}).shader || 'sky/reactor_bastion', m = Q3MAT.get(shader);
      return { activeMap: window.VOID_ACTIVE_MAP, shader, farbox: m && m.sky && m.sky.farbox, cloudHeight: m && m.sky && m.sky.cloudHeight, skyRotate: m && m.skyRotate, suffix: Q3Tex.SKY_SUFFIX,
        generators: Object.keys(Q3Tex.skyBoxes || {}), horizonFog: (WORLD.meta.sky || {}).horizonFog ?? 0 };
    });
    if (!info.farbox || info.farbox === '$baked' || !info.generators.includes(info.farbox)) throw new Error('map ' + map + ' has no generated sky box: ' + JSON.stringify(info));
    const N = 512, faces = [];
    for (const sx of info.suffix) {
      const b64 = await page.evaluate(([name, sx, N]) => {
        const a = Q3Tex.skyBoxes[name](sx, N), b = Q3Tex.skyBoxes[name](sx, N);
        if (!a || a.length !== N * N * 4) throw new Error('unexpected face size for ' + sx);
        for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) throw new Error('generator is not deterministic for ' + sx);
        let s = ''; const u = new Uint8Array(a.buffer, a.byteOffset, a.length);
        for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768));
        return btoa(s);
      }, [info.farbox, sx, N]);
      faces.push(Buffer.from(b64, 'base64'));
    }
    const rgba = Buffer.concat(faces), file = png(N, N * 6, rgba);
    fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true }); fs.writeFileSync(out, file);
    const report = { tool: 'tools/n4/legacy_skybox.cjs', source: 'legacy/index.html', sha256: sha, ...info, faceSize: N, layout: 'six faces stacked vertically in suffix order',
      file: path.basename(out), fileSha256: crypto.createHash('sha256').update(file).digest('hex'), rgbaSha256: crypto.createHash('sha256').update(rgba).digest('hex'),
      faceSha256: faces.map(f => crypto.createHash('sha256').update(f).digest('hex')), deterministic: true };
    if (reportFile) fs.writeFileSync(reportFile, JSON.stringify(report, null, 1) + '\n');
    console.log('wrote', out, file.length, 'bytes', JSON.stringify({ farbox: info.farbox, suffix: info.suffix, skyRotate: info.skyRotate, horizonFog: info.horizonFog }));
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
