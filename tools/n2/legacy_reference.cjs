#!/usr/bin/env node
// N2 legacy reference frames: runs the UNMODIFIED legacy/index.html in headless Chromium (WebGL2)
// and captures the canvas at fixed camera poses for comparison with native Vulkan captures.
//
//   node tools/n2/legacy_reference.cjs --poses tools/n2/helios_poses.txt --out <dir> [--time 10]
//
// Requirements: Node >= 22 and the `playwright` package with Chromium. In the N2 runner WebGL2 ran
// on Mesa lavapipe (set CHROMIUM_WEBGL_RENDERER=lavapipe for that runner's Chromium wrapper).
//
// What the script changes at runtime (page state only, never the file): it presses the real
// "PLAY OFFLINE SOLO" button handler, then holds the simulation with the legacy's own `fovPanelOpen`
// switch, places `player` at each pose, sets the material clock `now`, hides the HUD and the
// first-person gun/body (`setHudVisibility(false)`, `__SHOWCASE_HIDE_FP`), removes enemies,
// pickups and particles, and turns lens flares off (`Q3R.S.flares`) because N2 renders the
// static map only. Every such change is listed in the output JSON.
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { chromium } = require('playwright');

function args() {
  const a = { poses: null, out: null, time: 10, map: 'helios-crucible', width: 1280, height: 720, timeoutMs: 900000 };
  const v = process.argv.slice(2);
  for (let i = 0; i < v.length; i++) {
    const k = v[i], n = () => { if (++i >= v.length) throw new Error('missing value for ' + k); return v[i]; };
    if (k === '--poses') a.poses = n(); else if (k === '--out') a.out = n(); else if (k === '--time') a.time = Number(n());
    else if (k === '--map') a.map = n(); else if (k === '--width') a.width = Number(n()); else if (k === '--height') a.height = Number(n());
    else throw new Error('unknown option ' + k);
  }
  if (!a.poses || !a.out) throw new Error('usage: --poses <file> --out <dir> [--time s] [--map id]');
  return a;
}

(async () => {
  const opt = args();
  const root = path.resolve(__dirname, '../..');
  const html = path.join(root, 'legacy/index.html');
  const bytes = fs.readFileSync(html);
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const poses = fs.readFileSync(opt.poses, 'utf8').split('\n').map(l => l.trim()).filter(l => l && !l.startsWith('#'))
    .map(l => { const [name, x, y, z, yaw, pitch] = l.split(/\s+/); return { name, x: +x, y: +y, z: +z, yaw: +yaw, pitch: +pitch }; });
  fs.mkdirSync(opt.out, { recursive: true });

  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); }
    else { res.writeHead(404); res.end(); }
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  const browser = await chromium.launch({ env: { ...process.env } });
  const log = [];
  try {
    const page = await browser.newPage({ viewport: { width: opt.width, height: opt.height }, deviceScaleFactor: 1 });
    page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') log.push(m.type() + ': ' + m.text().slice(0, 300)); });
    page.on('pageerror', e => log.push('pageerror: ' + String(e).slice(0, 300)));
    page.setDefaultTimeout(opt.timeoutMs);
    await page.goto(`http://127.0.0.1:${port}/index.html?map=${opt.map}`, { waitUntil: 'load', timeout: opt.timeoutMs });
    const renderer = await page.evaluate(() => { const e = gl.getExtension('WEBGL_debug_renderer_info'); return gl.getParameter(e ? e.UNMASKED_RENDERER_WEBGL : gl.RENDERER); });
    // The menu hides #start until PLAY -> map select; invoke the same button's click handler
    // (legacy: $('start').addEventListener('click',()=>{void launchGameWithLoading(0)})).
    await page.evaluate(() => document.getElementById('start').click());
    // Loading ends at "AWAITING DEPLOYMENT"; the visible ENTER THE ARENA button (#loadReady) deploys.
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: opt.timeoutMs, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true, null, { timeout: opt.timeoutMs, polling: 500 });
    const info = await page.evaluate(() => ({ map: window.VOID_ACTIVE_MAP, canvas: [canvas.width, canvas.height], fov: horizontalFov,
      q3: window.VOID_Q3 ? window.VOID_Q3() : null, lightQuality: lightQualitySetting, fogStrength: fogStrengthSetting,
      preset: Q3R.S.preset, overbright: Q3R.S.overbright, textureMode: Q3R.S.textureMode, anisotropy: Q3R.S.anisotropy, msaa: typeof msaaActive !== 'undefined' ? msaaActive : null }));
    const results = [];
    for (const p of poses) {
      await page.evaluate(([p, t]) => {
        fovPanelOpen = true;                       // legacy simulation hold (accumulator discarded)
        window.__SHOWCASE_HIDE_FP = true; setHudVisibility(false);
        bots.length = 0; pickups.length = 0; particles.length = 0; enemyShots.length = 0;
        Q3R.S.flares = false;
        if (typeof q3Reset === 'function') q3Reset();
        player.x = p.x; player.y = p.y; player.z = p.z; player.a = p.yaw; player.pitch = p.pitch; player.vx = 0; player.vy = 0; player.vz = 0;
        player.crouchOffset = 0; now = t;
      }, [p, opt.time]);
      // let several frames render with the frozen state
      await page.evaluate(() => new Promise(r => { let n = 0; const f = () => (++n >= 8 ? r() : requestAnimationFrame(f)); requestAnimationFrame(f); }));
      const file = path.join(opt.out, p.name + '.png');
      // Exact framebuffer pixels: run the legacy render() and read the default framebuffer in the
      // same task (before compositing; preserveDrawingBuffer is false). Rows are flipped to top-first.
      const dataUrl = await page.evaluate(() => {
        render();
        const w = canvas.width, h = canvas.height, px = new Uint8Array(w * h * 4);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
        const c = document.createElement('canvas'); c.width = w; c.height = h;
        const cx = c.getContext('2d'), img = cx.createImageData(w, h);
        for (let y = 0; y < h; y++) img.data.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
        for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255;   // canvas alpha:false
        cx.putImageData(img, 0, 0);
        return c.toDataURL('image/png');
      });
      fs.writeFileSync(file, Buffer.from(dataUrl.split(',')[1], 'base64'));
      const state = await page.evaluate(() => ({ x: player.x, y: player.y, z: player.z, a: player.a, pitch: player.pitch, now, canvas: [canvas.width, canvas.height] }));
      results.push({ name: p.name, file: path.basename(file), requested: p, state });
      process.stdout.write(`captured ${p.name}\n`);
    }
    const report = { tool: 'tools/n2/legacy_reference.cjs', source: 'legacy/index.html', sha256: sha, renderer, viewport: [opt.width, opt.height], time: opt.time,
      runtimeStateChanges: ['#start click handler invoked via element.click() (button is hidden behind the PLAY/map menus)', 'clicked the visible ENTER THE ARENA button (#loadReady)', 'fovPanelOpen=true (legacy simulation hold)', '__SHOWCASE_HIDE_FP=true', 'setHudVisibility(false)',
        'bots/pickups/particles/enemyShots emptied', 'Q3R.S.flares=false', 'q3Reset() (view bob state)', 'player pose and now set per capture', 'frame read with gl.readPixels right after a synchronous render() call'],
      info, frames: results, consoleWarningsAndErrors: log.slice(0, 200) };
    fs.writeFileSync(path.join(opt.out, 'legacy_reference.json'), JSON.stringify(report, null, 2) + '\n');
  } finally {
    await browser.close();
    server.close();
  }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
