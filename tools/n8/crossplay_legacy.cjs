#!/usr/bin/env node
// N8 cross-play check: the native multiplayer client and relay against the ORIGINAL browser game
// (legacy/index.html, unmodified, hash-checked) running in headless Chromium.
//
//   NODE_PATH=$(npm root -g) node tools/n8/crossplay_legacy.cjs --build <native build dir> --assets assets/n1 --out docs/n8-evidence/crossplay
//
// A. native host (cat_arena_coop_crossplay host: native relay + native host simulation) + browser guest
// B. browser host + native guest, through `cat_arena --relay-server` (the relay mode of the game executable)
//
// Browser actions are real clicks on the legacy buttons (MULTIPLAYER, JOIN A SERVER / HOST A SERVER,
// JOIN, CREATE SERVER, ENTER THE ARENA) and real key presses (W). The relay address, player name and
// server name are put in localStorage before the page loads (the legacy page keeps them there).
// Page globals (netMode, netPlayers, bots, player, waveNumber) are read with page.evaluate; in B the
// browser host's own remoteApplyDamage() is called once to damage the native guest.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto'), cp = require('child_process'), net = require('net');
const { chromium } = require('playwright');

const opt = { build: null, assets: null, out: null };
for (let i = 2; i < process.argv.length; i++) { const k = process.argv[i].replace(/^--/, ''); opt[k] = process.argv[++i]; }
if (!opt.build || !opt.assets || !opt.out) throw new Error('usage: --build <dir> --assets <assets/n1> --out <dir>');
fs.mkdirSync(opt.out, { recursive: true });
const crossplay = path.join(opt.build, 'cat_arena_coop_crossplay'), game = path.join(opt.build, 'cat_arena');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const freePort = () => new Promise(res => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const results = { legacySha256: null, checks: [], scenarios: {} };
function check(ok, what, detail) { results.checks.push({ ok: !!ok, what, detail }); console.log(`${ok ? 'PASS' : 'FAIL'} ${what}${detail !== undefined ? ' — ' + JSON.stringify(detail) : ''}`); }

// a native child process that prints one JSON object per line
function native(args, label) {
  const p = cp.spawn(args[0], args.slice(1), { stdio: ['pipe', 'pipe', 'pipe'] });
  const lines = []; let buf = '';
  p.stdout.on('data', d => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); try { lines.push(JSON.parse(l)); } catch { lines.push({ text: l }); } } });
  p.stderr.on('data', d => lines.push({ stderr: String(d) }));
  const waitFor = async (pred, ms, what) => { const t = Date.now(); while (Date.now() - t < ms) { const hit = lines.find(pred); if (hit) return hit; await sleep(100); } throw new Error(`${label}: timed out waiting for ${what}`); };
  const lastStatus = () => [...lines].reverse().find(l => l.status)?.status;
  return { p, lines, waitFor, lastStatus, label };
}

(async () => {
  const html = fs.readFileSync(path.resolve(__dirname, '../../legacy/index.html'));
  results.legacySha256 = crypto.createHash('sha256').update(html).digest('hex');
  if (results.legacySha256 !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy hash mismatch');
  const server = http.createServer((q, r) => { if (q.url.startsWith('/index.html') || q.url === '/') { r.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); r.end(html); } else { r.writeHead(404); r.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const pageUrl = `http://127.0.0.1:${server.address().port}/index.html?map=reactor-bastion`;
  const browser = await chromium.launch();
  const open = async (relayPort, name) => {
    const ctx = await browser.newContext({ viewport: { width: 480, height: 270 }, deviceScaleFactor: 1 });
    await ctx.addInitScript(([relay, name]) => { localStorage.setItem('cat.relay', relay); localStorage.setItem('cat.name', name); localStorage.setItem('cat.serverName', 'Browser Server'); }, [`ws://127.0.0.1:${relayPort}/ws`, name]);
    const page = await ctx.newPage();
    page.setDefaultTimeout(600000);
    const log = [];
    page.on('pageerror', e => log.push('pageerror: ' + String(e).slice(0, 200)));
    await page.goto(pageUrl, { waitUntil: 'load' });
    await page.waitForSelector('#catMultiplayer', { state: 'visible' });
    return { ctx, page, log };
  };
  const state = page => page.evaluate(() => ({
    netMode, room: netRoom, players: [...netPlayers.values()].map(p => ({ name: p.name, x: +p.x.toFixed(2), y: +p.y.toFixed(2), hp: p.hp, downed: !!p.downed })),
    bots: bots.length, wave: waveNumber, hp: player ? player.hp : null, downed: player ? !!player.downed : null, x: player ? +player.x.toFixed(2) : null, y: player ? +player.y.toFixed(2) : null, active,
    loading: loadingInProgress, socket: netSocket ? netSocket.readyState : -1, snapshots: { ...netSnapshotStats }, queued: loadingNetRelayQueue.length
  }));
  const enter = async page => { await page.waitForSelector('#loadReady', { state: 'visible' }); await page.click('#loadReady'); await sleep(1000); };
  // the headless page renders the whole game with software WebGL, so it is slow: measure, and poll with timeouts
  const fps = page => page.evaluate(() => new Promise(r => { let n = 0; const t = performance.now(); const f = () => { n++; if (performance.now() - t < 3000) requestAnimationFrame(f); else r(+(n * 1000 / (performance.now() - t)).toFixed(1)); }; requestAnimationFrame(f); }));
  const until = async (fn, ms) => { const t = Date.now(); let last; while (Date.now() - t < ms) { last = await fn(); if (last && last.ok) return last; await sleep(250); } return last || {}; };
  const send = (proc, cmd) => proc.p.stdin.write(cmd + '\n');

  // ---------------- A. native host + browser guest ----------------
  {
    const port = await freePort();
    const host = native([crossplay, 'host', opt.assets, String(port), '400'], 'native host');
    await host.waitFor(l => l.event === 'ready', 20000, 'ready');
    const { ctx, page, log } = await open(port, 'Browser Guest');
    await page.click('#catMultiplayer'); await page.click('#catJoinChoice');
    await page.waitForSelector('#serverList .serverRow button:not([disabled])');
    const row = await page.textContent('#serverList .serverRow');
    check(/Native Server/.test(row) && /Native Host/.test(row) && /1\/4/.test(row), 'A: the browser server list shows the native host\'s room', row);
    await page.click('#serverList .serverRow button');
    await enter(page);
    await host.waitFor(l => l.event === 'first_pose', 120000, 'first pose from the browser');
    results.scenarios.A = { browserFps: await fps(page) };
    let r = await until(async () => { const s = await state(page), hs = host.lastStatus(); return { ok: s.bots > 0 && hs && s.bots === hs.enemies && s.wave === hs.wave, s, hs }; }, 30000);
    check(r.ok && r.s.netMode === 'guest' && r.s.players.some(p => p.name === 'Native Host'), 'A: browser guest sees the native host and its enemies (snapshots)', r.s && { browserBots: r.s.bots, nativeEnemies: r.hs && r.hs.enemies, wave: r.s.wave, players: r.s.players });
    await page.screenshot({ path: path.join(opt.out, 'A_browser_guest.png') }).catch(e => console.log('screenshot failed: ' + e.message));
    send(host, 'freeze'); await host.waitFor(l => l.event === 'frozen', 10000, 'frozen');
    r = await until(async () => { const s = await state(page); return { ok: s.bots === 0 && s.hp === 100 && !s.downed, s }; }, 30000);
    check(r.ok, 'A: the native host clears the wave and restores the guest (browser: 0 enemies, 100 HP)', r.s && { bots: r.s.bots, hp: r.s.hp, downed: r.s.downed });
    const before = await state(page);
    await page.keyboard.down('KeyW'); await sleep(2500); await page.keyboard.up('KeyW');
    r = await until(async () => { const s = await state(page), hs = host.lastStatus(); const v = hs && hs.others.find(o => o.name === 'Browser Guest');
      const moved = Math.hypot(s.x - before.x, s.y - before.y); return { ok: moved > 1 && v && Math.hypot(v.x - s.x, v.y - s.y) < .5, moved, s, v }; }, 30000);
    check(r.ok, 'A: the browser guest\'s movement reaches the native host', { movedInBrowser: r.moved && +r.moved.toFixed(2), browser: r.s && { x: r.s.x, y: r.s.y }, nativeView: r.v });
    send(host, 'damage');
    r = await until(async () => { const s = await state(page); return { ok: s.hp === 70 && !s.downed, s }; }, 30000);
    check(r.ok, 'A: native host damage (30) reaches the browser guest', r.s && { hp: r.s.hp });
    send(host, 'down');
    r = await until(async () => { const s = await state(page); return { ok: s.hp === 0 && s.downed === true, s }; }, 30000);
    check(r.ok, 'A: the browser guest is downed by the native host', r.s && { hp: r.s.hp, downed: r.s.downed });
    await page.screenshot({ path: path.join(opt.out, 'A_browser_guest_downed.png') }).catch(e => console.log('screenshot failed: ' + e.message));
    Object.assign(results.scenarios.A, { browser: await state(page), native: host.lastStatus(), pageErrors: log });
    await ctx.close();
    const left = await host.waitFor(l => l.event === 'announce' && /TEAMMATE LEFT/.test(l.text), 30000, 'guest_left').catch(() => null);
    check(!!left, 'A: closing the browser tab is seen by the native host (guest_left)');
    host.p.kill(); await sleep(300);
    fs.writeFileSync(path.join(opt.out, 'A_native_host.jsonl'), host.lines.map(l => JSON.stringify(l)).join('\n') + '\n');
  }

  // ---------------- B. browser host + native guest, relay = cat_arena --relay-server ----------------
  {
    const port = await freePort();
    const relay = cp.spawn(game, ['--relay-server', '--port', String(port), '--bind', '127.0.0.1'], { stdio: ['ignore', 'pipe', 'pipe'] });
    let relayLog = ''; relay.stdout.on('data', d => relayLog += d); relay.stderr.on('data', d => relayLog += d);
    for (let i = 0; i < 100 && !/relay \(/.test(relayLog); i++) await sleep(50);
    const { ctx, page, log } = await open(port, 'Browser Host');
    await page.click('#catMultiplayer'); await page.click('#catHostChoice');
    await page.waitForSelector('#coopHost', { state: 'visible' });
    await page.click('#coopHost');
    await enter(page);
    results.scenarios.B = { browserFps: await fps(page) };
    const guest = native([crossplay, 'guest', opt.assets, `127.0.0.1:${port}`, '400'], 'native guest');
    const listed = await guest.waitFor(l => l.event === 'listed', 20000, 'server list');
    check(listed.title === 'Browser Server' && listed.host === 'Browser Host' && listed.players === 1, 'B: the native server list shows the browser host\'s room', listed);
    await guest.waitFor(l => l.event === 'ready', 20000, 'ready');
    let r = await until(async () => { const s = await state(page), gs = guest.lastStatus();
      return { ok: s.netMode === 'host' && s.players.some(p => p.name === 'Native Guest') && gs && gs.others && gs.others.some(o => o.name === 'Browser Host' && o.host), s, gs }; }, 30000);
    check(r.ok, 'B: browser host and native guest see each other', r.s && { browser: r.s.players, native: r.gs && r.gs.others });
    r = await until(async () => { const s = await state(page), gs = guest.lastStatus();
      return { ok: gs && gs.enemies > 0 && gs.enemies === s.bots && gs.wave === s.wave && gs.worldAgeMs >= 0 && gs.worldAgeMs < 3000, s, gs }; }, 60000);
    check(r.ok, 'B: the native guest shows the browser host\'s enemies (snapshots)', r.gs && { nativeEnemies: r.gs.enemies, browserBots: r.s.bots, wave: r.gs.wave, worldAgeMs: r.gs.worldAgeMs, browserSnapshotRounds: r.s.snapshots });
    send(guest, 'stop'); await guest.waitFor(l => l.event === 'stop', 5000, 'stop'); await sleep(1500);
    r = await until(async () => { const s = await state(page), gs = guest.lastStatus(); const v = s.players.find(p => p.name === 'Native Guest');
      return { ok: v && Math.hypot(v.x - gs.x, v.y - gs.y) < .5, v, gs }; }, 60000);
    check(r.ok, 'B: the native guest\'s position reaches the browser host', { browserView: r.v, native: r.gs && { x: r.gs.x, y: r.gs.y } });
    await page.screenshot({ path: path.join(opt.out, 'B_browser_host.png') }).catch(e => console.log('screenshot failed: ' + e.message));
    const id = await page.evaluate(() => [...netPlayers.entries()].find(([, p]) => p.name === 'Native Guest')?.[0]);
    await page.evaluate(id => remoteApplyDamage(id, 30, 0, 0), id);
    r = await until(async () => { const gs = guest.lastStatus(); return { ok: gs.hp === 70, gs }; }, 60000);
    check(r.ok, 'B: the browser host\'s damage reaches the native guest', r.gs && { hp: r.gs.hp });
    send(guest, 'walk');
    Object.assign(results.scenarios.B, { browser: await state(page), native: guest.lastStatus(), pageErrors: log, nativeVolleysSeenByGuest: guest.lines.filter(l => l.event === 'volley').length });
    await ctx.close();   // the browser host quits
    const gone = await guest.waitFor(l => l.event === 'disconnected', 30000, 'host left').catch(() => null);
    check(gone && /Host disconnected/.test(gone.text), 'B: the browser host quitting closes the native guest\'s server', gone);
    guest.p.kill(); relay.kill();
    fs.writeFileSync(path.join(opt.out, 'B_native_guest.jsonl'), guest.lines.map(l => JSON.stringify(l)).join('\n') + '\n');
    fs.writeFileSync(path.join(opt.out, 'B_relay.log'), relayLog);
  }
  results.chromium = browser.version(); results.viewport = '480x270';
  await browser.close(); server.close();
    fs.writeFileSync(path.join(opt.out, 'crossplay.json'), JSON.stringify(results, null, 2) + '\n');
  const failed = results.checks.filter(c => !c.ok).length;
  console.log(`${failed ? 'FAIL' : 'PASS'} cross-play with the original browser game (${results.checks.length - failed}/${results.checks.length} checks)`);
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); fs.writeFileSync(path.join(opt.out, 'crossplay.json'), JSON.stringify({ ...results, error: String(e.stack || e) }, null, 2)); process.exit(1); });
