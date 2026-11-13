#!/usr/bin/env node
// N7 legacy UI reference: runs the UNMODIFIED legacy/index.html in headless Chromium and records the
// menus, settings tabs, loading screen, in-game HUD (keyboard/mouse and touch layouts) and pause menu as
// screenshots, plus the computed geometry and styles of the UI elements (getBoundingClientRect,
// getComputedStyle) that the native UI is laid out from.
//
//   NODE_PATH=$(npm root -g) node tools/n7/legacy_ui_capture.cjs --out docs/n7-evidence/legacy-ui [--width 1280 --height 720]
//
// Runtime actions (page state only, never the file): real button clicks (#catPlay, #catSettings, tabs,
// #catMultiplayer, #catExit, #start, #loadReady), a keydown Escape for the pause menu, and selecting
// "TOUCH" in the control-mode select (value set, its own input/change handlers run) for the touch layout.
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { chromium } = require('playwright');

const opt = { out: null, width: 1280, height: 720 };
for (let i = 2; i < process.argv.length; i++) {
  const k = process.argv[i], v = process.argv[++i];
  if (k === '--out') opt.out = v; else if (k === '--width') opt.width = +v; else if (k === '--height') opt.height = +v; else throw new Error('unknown ' + k);
}
if (!opt.out) throw new Error('--out <dir> required');

const ELEMENTS = ['menu', 'catHome', 'catMaps', 'catMapOptions', 'catMapImage', 'catPreviewCaption', 'catMapDescription', 'start', 'catPlay', 'catMultiplayer', 'catSettings', 'catExit',
  'fovPanel', 'viewTabButton', 'gfxTabButton', 'perfTabButton', 'touchTabButton', 'audioTabButton', 'uiTabButton', 'settingsResume',
  'hud', 'top', 'brand', 'score', 'waveDisplay', 'leftStats', 'hpnumber', 'hplabel', 'hpbar', 'weaponBox', 'weaponName', 'ammo', 'secondary', 'stanceLabel', 'cross', 'event',
  'mapbutton', 'fovToggle', 'mouseStatus', 'perfOverlay', 'voidPauseButton', 'voidPauseOverlay', 'voidPauseResume', 'voidPauseSettings', 'voidPauseCustomize', 'voidPauseChangeMap', 'catReturnMenu',
  'mobileControls', 'stick', 'knob', 'fire', 'jump', 'swap', 'touchSprint', 'touchCrouch', 'touchReload', 'touchRevive', 'loadOverlay', 'loadFrame', 'loadReady', 'catExitDialog'];

(async () => {
  const html = fs.readFileSync(path.resolve(__dirname, '../../legacy/index.html'));
  const sha = crypto.createHash('sha256').update(html).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy hash mismatch');
  fs.mkdirSync(opt.out, { recursive: true });
  const server = http.createServer((q, r) => { if (q.url.startsWith('/index.html') || q.url === '/') { r.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); r.end(html); } else { r.writeHead(404); r.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: opt.width, height: opt.height }, deviceScaleFactor: 1 });
  page.setDefaultTimeout(900000);
  const log = [];
  page.on('pageerror', e => log.push('pageerror: ' + String(e).slice(0, 200)));
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`, { waitUntil: 'load' });
  const shots = [];
  const settle = () => page.evaluate(() => new Promise(r => { let n = 0; const f = () => (++n >= 6 ? r() : requestAnimationFrame(f)); requestAnimationFrame(f); }));
  async function shot(name) { await settle(); await page.screenshot({ path: path.join(opt.out, name + '.png') }); shots.push(name); process.stdout.write('captured ' + name + '\n'); }
  async function geometry(tag) {
    return page.evaluate(([ids, tag]) => {
      const out = { tag, viewport: [innerWidth, innerHeight], elements: {} };
      for (const id of ids) {
        const e = document.getElementById(id); if (!e) continue;
        const r = e.getBoundingClientRect(), s = getComputedStyle(e);
        out.elements[id] = { visible: r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none', x: r.x, y: r.y, w: r.width, h: r.height,
          color: s.color, background: s.backgroundColor, border: s.border, fontFamily: s.fontFamily, fontSize: s.fontSize, fontWeight: s.fontWeight,
          letterSpacing: s.letterSpacing, opacity: s.opacity, clipPath: s.clipPath, text: (e.innerText || '').slice(0, 160) };
      }
      return out;
    }, [ELEMENTS, tag]);
  }
  const layouts = [];
  await shot('home'); layouts.push(await geometry('home'));
  await page.click('#catPlay');
  await page.waitForFunction(() => { const i = document.getElementById('catMapImage'); return i && !i.hidden && i.complete; }, null, { timeout: 900000, polling: 500 });
  await shot('maps'); layouts.push(await geometry('maps'));
  await page.click('#catMaps [data-back]');
  await page.click('#catMultiplayer'); await shot('multiplayer');
  await page.click('#catMulti [data-back]');
  await page.click('#catSettings');
  for (const [tab, button] of [['view', 'viewTabButton'], ['graphics', 'gfxTabButton'], ['performance', 'perfTabButton'], ['input', 'touchTabButton'], ['audio', 'audioTabButton'], ['hud', 'uiTabButton']]) {
    await page.click('#' + button); await shot('settings_' + tab);
    if (tab === 'view') layouts.push(await geometry('settings'));
  }
  await page.click('#settingsResume');
  await page.click('#catExit'); await shot('exit_dialog'); await page.click('#catExitCancel');
  await page.click('#catPlay');
  await page.waitForFunction(() => !document.getElementById('start').disabled);
  await page.click('#start');
  await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null; }, null, { timeout: 900000, polling: 1000 });
  await shot('loading_ready'); layouts.push(await geometry('loading'));
  await page.click('#loadReady');
  await page.waitForFunction(() => typeof active !== 'undefined' && active === true);
  await page.waitForTimeout(2500);
  await shot('hud_kbm'); layouts.push(await geometry('hud_kbm'));
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => !document.getElementById('voidPauseOverlay').hidden);
  await shot('pause'); layouts.push(await geometry('pause'));
  await page.click('#voidPauseResume');
  await page.evaluate(() => { const e = document.getElementById('inputMode'); e.value = 'touch'; for (const t of ['input', 'change']) e.dispatchEvent(new Event(t, { bubbles: true })); });
  await page.waitForTimeout(800);
  await shot('hud_touch'); layouts.push(await geometry('hud_touch'));
  const fonts = await page.evaluate(async () => { await document.fonts.ready; return [...document.fonts].map(f => f.family + ' ' + f.status); });
  fs.writeFileSync(path.join(opt.out, 'legacy_ui_layout.json'), JSON.stringify({ tool: 'tools/n7/legacy_ui_capture.cjs', sha256: sha, browser: browser.version(), viewport: [opt.width, opt.height],
    note: 'Fonts are the system fonts of the capturing machine (the legacy CSS embeds none): ' + JSON.stringify(fonts), shots, layouts, pageErrors: log }, null, 1) + '\n');
  await browser.close(); server.close();
})().catch(e => { console.error(e); process.exit(1); });
