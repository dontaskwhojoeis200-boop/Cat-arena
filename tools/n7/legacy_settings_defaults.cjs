#!/usr/bin/env node
// N7: every settings control of the UNMODIFIED legacy game with its default value (fresh browser
// profile, after the page's own initialisation), range, options and label. The native settings model
// (native/src/game_settings.cpp) is checked against this file by tests/ui_tests.cpp.
//   NODE_PATH=$(npm root -g) node tools/n7/legacy_settings_defaults.cjs native/tests/fixtures/n7/legacy_settings.json
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { chromium } = require('playwright');

(async () => {
  const html = fs.readFileSync(path.resolve(__dirname, '../../legacy/index.html'));
  const sha = crypto.createHash('sha256').update(html).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy hash mismatch');
  const server = http.createServer((q, r) => { r.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); r.end(html); });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  page.setDefaultTimeout(900000);
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`, { waitUntil: 'load' });
  await page.waitForFunction(() => window.VOID_UI_READY === true);
  const controls = await page.evaluate(() => {
    const out = [];
    for (const panel of ['viewSettings', 'graphicsSettings', 'performanceSettings', 'touchSettings', 'audioSettings', 'uiSettings']) {
      const root = document.getElementById(panel);
      for (const e of root.querySelectorAll('input,select')) {
        const label = (e.labels && e.labels[0] ? e.labels[0].innerText : (e.closest('label') ? e.closest('label').innerText : e.getAttribute('aria-label') || '')).replace(/\s+/g, ' ').trim();
        const c = { panel, id: e.id, type: e.tagName === 'SELECT' ? 'select' : e.type, label: label.slice(0, 120) };
        if (e.type === 'checkbox') c.value = e.checked; else c.value = e.value;
        if (e.type === 'range') { c.min = +e.min; c.max = +e.max; c.step = e.step === '' ? 1 : +e.step; }
        if (e.tagName === 'SELECT') c.options = [...e.options].map(o => ({ value: o.value, label: o.textContent.trim() }));
        out.push(c);
      }
    }
    // Benchmark toggles that mobile-hud.js moved from the performance tab into graphics are found above under their new panel.
    return out;
  });
  fs.mkdirSync(path.dirname(process.argv[2]), { recursive: true });
  fs.writeFileSync(process.argv[2], JSON.stringify({ tool: 'tools/n7/legacy_settings_defaults.cjs', sha256: sha, browser: browser.version(), controls }, null, 1) + '\n');
  console.log(controls.length + ' controls');
  await browser.close(); server.close();
})().catch(e => { console.error(e); process.exit(1); });
