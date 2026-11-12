#!/usr/bin/env node
// N3 legacy movement reference: runs the UNMODIFIED legacy/index.html in headless Chromium and
// records the player state for every 1/120 s tick of scripted input tapes.
//
//   node tools/n3/legacy_trajectories.cjs --out native/tests/fixtures/n3/helios_trajectories.bin [--report file.json]
//   N4: [--map bastion-keep|reactor-bastion|q3-gallery] [--random 4] [--random-ticks 1500]  (defaults: helios-crucible, 6, 2400)
//
// How a tick is produced (page state only; the file is hash-checked and never modified):
//   1. key transitions are sent as real KeyboardEvents to `window`, so the legacy keydown/keyup
//      handlers run (jump buffer, slide start, landing-slide buffer, `keys[]`);
//   2. mouse motion goes through the legacy `aim(dx,dy)`;
//   3. the player part of the legacy `update(dt)` is executed in its original order with the
//      original functions: now+=dt; wish direction from keys and player.a; moveMarine();
//      stage4Movement(); worldTriggerUpdate(); q3PostMove().
// The rest of update() (weapons, enemies, waves, pickups) is NOT run: N3 is traversal only and
// enemies would push/damage the player. Between scenarios the legacy loop is held with its own
// `fovPanelOpen` switch. Everything the tool sets is listed in the report.
//
// Output (little endian): "CATTRAJ1", u32 version=2, u32 scenarios, u32 tapeWidth=3, u32 recordWidth=30;
// per scenario: u32 nameLength, name, u32 ticks, f64[8] start {x,y,z,yaw,pitch,now,vz,ground}, f64[6] contacts
// {horizontal,ceiling,landings,steps,iterations,recoveries}, f64[ticks*3] tape {keyMask,dx,dy},
// f64[ticks*30] records (see RECORD below; native/tests/physics_tests.cpp reads the same layout).
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { chromium } = require('playwright');

const KEYS = ['KeyW', 'KeyS', 'KeyA', 'KeyD', 'Space', 'ControlLeft', 'ShiftLeft', 'KeyC'];
const W = 1, S = 2, A = 4, D = 8, JUMP = 16, CROUCH = 32, SPRINT = 64, SLIDE = 128;
const RECORD = ['x', 'y', 'z', 'vx', 'vy', 'vz', 'ground', 'crouchOffset', 'yaw', 'pitch', 'slideTime', 'slideCooldown', 'slideBuffer', 'jumpBuffer',
  'coyoteTime', 'slopeAngle', 'fovScale', 'padCooldown', 'camZ', 'camForwardZ', 'camRoll', 'camUpZ', 'bobCycle', 'stepChange', 'landChange',
  'weaponPitch', 'weaponYaw', 'weaponRoll', 'weaponDrop', 'colliderHeight'];

function mulberry32(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
function tape(ticks) { return { ticks, data: new Float64Array(ticks * 3) }; }
function fill(t, from, to, mask, dx = 0, dy = 0) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) { t.data[i * 3] = mask; t.data[i * 3 + 1] = dx; t.data[i * 3 + 2] = dy; } }
function or(t, from, to, mask) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) t.data[i * 3] |= mask; }
// Random play: segments of 15-150 ticks with weighted key choices, jumps, slides and mouse turns.
function randomTape(seed, ticks) {
  const r = mulberry32(seed), t = tape(ticks);
  for (let i = 0; i < ticks;) {
    const len = 15 + Math.floor(r() * 136);
    let mask = 0;
    const m = r();
    if (m < .62) mask |= W; else if (m < .70) mask |= S; else if (m < .78) mask |= 0;
    else mask |= W;
    const s = r(); if (s < .22) mask |= A; else if (s < .44) mask |= D;
    if (r() < .45) mask |= SPRINT;
    if (r() < .14) mask |= CROUCH;
    const turn = r() < .55 ? (r() * 2 - 1) * (r() < .25 ? 14 : 4) : 0, look = r() < .12 ? (r() * 2 - 1) * 2 : 0;
    fill(t, i, i + len, mask, Math.round(turn * 100) / 100, Math.round(look * 100) / 100);
    if (r() < .50) { const at = i + Math.floor(r() * len), hold = r() < .35 ? 30 + Math.floor(r() * 90) : 2 + Math.floor(r() * 5); or(t, at, at + hold, JUMP); }
    if (r() < .16) { const at = i + Math.floor(r() * len); or(t, at, at + 2, SLIDE); }
    i += len;
  }
  return t;
}

(async () => {
  const v = process.argv.slice(2); let out = null, reportFile = null, mapId = 'helios-crucible', randomCount = 6, randomTicks = 2400;
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') out = v[++i]; else if (v[i] === '--report') reportFile = v[++i]; else if (v[i] === '--map') mapId = v[++i];
    else if (v[i] === '--random') randomCount = Number(v[++i]); else if (v[i] === '--random-ticks') randomTicks = Number(v[++i]); else throw new Error('unknown option ' + v[i]); }
  if (!out) throw new Error('usage: --out <file.bin> [--report file.json]');
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); }
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  const log = [];
  try {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 }, deviceScaleFactor: 1 });
    page.on('pageerror', e => log.push('pageerror: ' + String(e).slice(0, 300)));
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${mapId}`, { waitUntil: 'load', timeout: 900000 });
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: 900000, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true, null, { timeout: 900000, polling: 500 });
    await page.evaluate(() => { fovPanelOpen = true; });

    // World facts used to place the targeted scenarios (legacy queries, read only).
    const world = await page.evaluate(() => {
      const clearAt = (x, y, z) => stage4CapsuleClear(x, y, z, 1.8, .30);
      const ramps = WORLD.slabs.filter(s => s.plane && s.nav).map(s => ({ id: s.id, ...s.plane }));
      const ceilings = [];
      for (let iy = 0; iy < 234 && ceilings.length < 400; iy++) for (let ix = 0; ix < 234; ix++) {
        const x = ix * .5 + .25, y = iy * .5 + .25;
        for (const f of stage4SurfacesAt(x, y)) {   // every physical surface: floors, ramps, block/equipment tops
          if (!clearAt(x, y, f.z)) continue;
          let low = Infinity;
          for (const s of worldSlabsAt(x, y)) { const z = worldSlabZ(s, x, y), u = z - worldSlabThickness(s, z); if (u > f.z + 1.85 && u < low) low = u; }
          if (low - f.z > 1.9 && low - f.z < 4.2) ceilings.push({ x, y, z: f.z, clearance: low - f.z });
        }
      }
      return { map: window.VOID_ACTIVE_MAP, spawns: WORLD.spawns.map(s => ({ x: s.x, y: s.y, z: s.z, a: s.a, tag: s.tag })), pads: WORLD.pads.map(p => ({ id: p.id, x: p.x, y: p.y, z: p.z, r: p.r })),
        ramps, ceilings, sprintMode: document.getElementById('sprintMode')?.value || 'hold' };
    });
    const startNear = (x, y, z, tx, ty) => page.evaluate(([x, y, z, tx, ty]) => {   // a clear standing spot ~2.4 m from a target
      for (const d of [2.4, 1.8, 3.2, 1.3]) for (let k = 0; k < 16; k++) {
        const a = k * Math.PI / 8, px = tx + d * Math.cos(a), py = ty + d * Math.sin(a);
        const s = stage4Support(px, py, z, .1);
        if (!s || Math.abs(s.z - z) > .05 || !stage4CapsuleClear(px, py, s.z, 1.8, .30)) continue;
        let ok = true;
        for (let q = 1; q <= 8 && ok; q++) { const u = q / 8, qx = px + (tx - px) * u, qy = py + (ty - py) * u; ok = stage4CapsuleClear(qx, qy, z, 1.8, .30); }
        if (ok) return { x: px, y: py, z: s.z, a: Math.atan2(ty - py, tx - px) };
      }
      return null;
    }, [x, y, z, tx, ty]);

    const scenarios = [];
    const add = (name, start, t, vz = 0) => { if (start) scenarios.push({ name, start: { x: start.x, y: start.y, z: start.z, a: start.a, pitch: 0, now: 100, vz, ground: vz ? 0 : 1 }, tape: t }); };
    const byZ = z => world.spawns.filter(s => Math.abs(s.z - z) < .1);
    const levels = [...new Set(world.spawns.map(s => s.z))].sort((a, b) => a - b);
    // Six random-play tapes from spawns on every floor.
    let seed = 20261110;
    for (let i = 0; i < randomCount; i++) { const list = byZ(levels[i % levels.length]); add('random_' + i + '_z' + levels[i % levels.length], list[(i * 3) % list.length], randomTape(seed + i * 7919, randomTicks)); }
    // Ramps: walk up, stop, walk back down, then sprint up and jump.
    for (const [i, r] of world.ramps.slice(0, 3).entries()) {
      const t = tape(900); fill(t, 10, 330, W); fill(t, 380, 640, S); fill(t, 660, 900, W | SPRINT); or(t, 760, 764, JUMP);
      const back = .9, sx = r.ox - r.dx * back, sy = r.oy - r.dy * back;
      const st = await page.evaluate(([x, y, z, ox, oy]) => { for (const [px, py] of [[x, y], [ox, oy]]) { const s = stage4Support(px, py, z + .2, .5); if (s && stage4CapsuleClear(px, py, s.z, 1.8, .30)) return { x: px, y: py, z: s.z }; } return null; }, [sx, sy, r.z0, r.ox + r.dx * .4, r.oy + r.dy * .4]);
      if (st) add('ramp_' + i + '_' + r.id, { ...st, a: Math.atan2(r.dy, r.dx) }, t);
    }
    // Jump pads: walk onto the pad, fly the authored arc, land, keep walking.
    for (const p of world.pads) { const t = tape(700); fill(t, 5, 110, W); fill(t, 420, 700, W); add('pad_' + p.id, await startNear(p.x, p.y, p.z, p.x, p.y), t); }
    // Ceilings: Helios has no underside within reach of a standing jump (lowest clearance is
    // reported in the JSON), so these start with an upward velocity, as a blast jump would give,
    // under the lowest slab undersides: once standing still, once while moving.
    world.ceilings.sort((a, b) => a.clearance - b.clearance);
    for (const [i, c] of [world.ceilings[0], world.ceilings[Math.floor(world.ceilings.length / 2)]].filter(Boolean).entries()) {
      const t = tape(360); if (i) fill(t, 0, 200, W | SPRINT); or(t, 220, 224, JUMP);
      add('ceiling_impulse_' + i, { x: c.x, y: c.y, z: c.z, a: i * 1.3 }, t, 10.5);
    }
    const open = byZ(0)[0] || world.spawns[0];
    { const t = tape(800); fill(t, 5, 400, W | SPRINT); or(t, 130, 133, SLIDE); or(t, 300, 420, CROUCH); fill(t, 420, 620, W | CROUCH); fill(t, 620, 800, W | SPRINT); or(t, 700, 702, JUMP); or(t, 720, 760, CROUCH); add('slide_crouch', open, t); }
    { const t = tape(900); fill(t, 5, 100, W | SPRINT); for (let i = 100; i < 900; i++) { const right = Math.floor((i - 100) / 90) % 2 === 0; t.data[i * 3] = W | SPRINT | JUMP | (right ? D : A); t.data[i * 3 + 1] = right ? 3.5 : -3.5; } add('air_strafe_bunnyhop', byZ(0)[1] || open, t); }
    { const t = tape(500); fill(t, 5, 60, CROUCH); fill(t, 60, 300, W | CROUCH); fill(t, 300, 360, W); fill(t, 360, 500, W | CROUCH, 2, .5); add('crouch_walk', byZ(levels[0])[1] || open, t); }
    { const t = tape(400); add('idle', byZ(levels[levels.length - 1])[0] || open, t); }

    const results = [];
    for (const sc of scenarios) {
      const res = await page.evaluate(([sc, tapeData, KEYS, REC]) => {
        fovPanelOpen = false;
        bots.length = 0; enemyShots.length = 0; rockets.length = 0; grenades.length = 0;
        for (const code of Object.keys(keys)) if (keys[code]) window.dispatchEvent(new KeyboardEvent('keyup', { code, bubbles: true }));
        keys = {}; jumpHeld = false; touchStick = { x: 0, y: 0 };
        Object.assign(player, { x: sc.start.x, y: sc.start.y, z: sc.start.z, a: sc.start.a, pitch: sc.start.pitch, vx: 0, vy: 0, vz: sc.start.vz, ground: !!sc.start.ground, crouchOffset: 0, downed: false, hp: 100 });
        slideTime = 0; slideCooldown = 0; slideSpeed = 0; slideDirX = 0; slideDirY = 0; slideBuffer = 0; jumpBuffer = 0; coyoteTime = 0; movementSlopeAngle = 0; airStrafeGained = 0;
        moveFovScale = 1; worldPadCooldown = 0; q3Reset(); now = sc.start.now;
        STAGE4_CONTACTS = { horizontal: 0, ceiling: 0, landings: 0, steps: 0, iterations: 0, recoveries: 0 };
        const n = tapeData.length / 3, out = new Array(n * REC), dt = 1 / 120;
        let prev = 0;
        for (let i = 0; i < n; i++) {
          const mask = tapeData[i * 3], dx = tapeData[i * 3 + 1], dy = tapeData[i * 3 + 2];
          for (let b = 0; b < KEYS.length; b++) { const bit = 1 << b; if ((mask ^ prev) & bit) window.dispatchEvent(new KeyboardEvent(mask & bit ? 'keydown' : 'keyup', { code: KEYS[b], bubbles: true })); }
          prev = mask;
          if (dx || dy) aim(dx, dy);
          // ---- player part of legacy update(dt), original order ----
          now += dt;
          const f = (keys.KeyW ? 1 : 0) - (keys.KeyS ? 1 : 0) - touchStick.y, s = (keys.KeyD ? 1 : 0) - (keys.KeyA ? 1 : 0) + touchStick.x;
          const m = Math.hypot(f, s), mf = m ? f / m : 0, ms = m ? s / m : 0;
          const ca = Math.cos(player.a), sa = Math.sin(player.a), wx = ca * mf - sa * ms, wy = sa * mf + ca * ms;
          moveMarine(dt, wx, wy, m);
          stage4Movement(dt);
          worldTriggerUpdate(dt);
          q3PostMove(dt);
          // ---- record ----
          const cam = q3ViewCamera(), wpn = q3WeaponMotion(), p = player, o = i * REC;
          out[o] = p.x; out[o + 1] = p.y; out[o + 2] = p.z; out[o + 3] = p.vx; out[o + 4] = p.vy; out[o + 5] = p.vz; out[o + 6] = p.ground ? 1 : 0; out[o + 7] = p.crouchOffset;
          out[o + 8] = p.a; out[o + 9] = p.pitch; out[o + 10] = slideTime; out[o + 11] = slideCooldown; out[o + 12] = slideBuffer; out[o + 13] = jumpBuffer; out[o + 14] = coyoteTime;
          out[o + 15] = movementSlopeAngle; out[o + 16] = moveFovScale; out[o + 17] = worldPadCooldown; out[o + 18] = cam.z; out[o + 19] = cam.forward[2]; out[o + 20] = cam.roll; out[o + 21] = cam.up[2];
          out[o + 22] = Q3.bobCycle; out[o + 23] = Q3.stepChange; out[o + 24] = Q3.landChange; out[o + 25] = wpn.pitch; out[o + 26] = wpn.yaw; out[o + 27] = wpn.roll; out[o + 28] = wpn.drop; out[o + 29] = stage4Height(p);
        }
        for (let b = 0; b < KEYS.length; b++) if (prev & (1 << b)) window.dispatchEvent(new KeyboardEvent('keyup', { code: KEYS[b], bubbles: true }));
        fovPanelOpen = true;
        return { records: out, contacts: { ...STAGE4_CONTACTS }, steps: Q3.steps, landings: Q3.landings, padLaunches: worldPadLaunches, finite: out.every(Number.isFinite) };
      }, [sc, Array.from(sc.tape.data), KEYS, RECORD.length]);
      if (!res.finite) throw new Error('non-finite record in ' + sc.name);
      results.push({ sc, res });
      const last = res.records.slice(-RECORD.length), zs = []; for (let i = 2; i < res.records.length; i += RECORD.length) zs.push(res.records[i]);
      process.stdout.write(`${sc.name}: ${sc.tape.ticks} ticks, end (${last[0].toFixed(2)}, ${last[1].toFixed(2)}, ${last[2].toFixed(2)}), z ${Math.min(...zs).toFixed(2)}..${Math.max(...zs).toFixed(2)}, contacts ${JSON.stringify(res.contacts)}\n`);
    }
    // ---- write the binary fixture ----
    const chunks = []; const u32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); chunks.push(b); }; const f64s = a => chunks.push(Buffer.from(new Float64Array(a).buffer));
    chunks.push(Buffer.from('CATTRAJ1')); u32(2); u32(results.length); u32(3); u32(RECORD.length);
    for (const { sc, res } of results) {
      const name = Buffer.from(sc.name); u32(name.length); chunks.push(name); u32(sc.tape.ticks);
      f64s([sc.start.x, sc.start.y, sc.start.z, sc.start.a, sc.start.pitch, sc.start.now, sc.start.vz, sc.start.ground]);
      const c = res.contacts; f64s([c.horizontal, c.ceiling, c.landings, c.steps, c.iterations, c.recoveries]);
      f64s(sc.tape.data); f64s(res.records);
    }
    fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
    const blob = Buffer.concat(chunks); fs.writeFileSync(out, blob);
    const report = { tool: 'tools/n3/legacy_trajectories.cjs', source: 'legacy/index.html', sha256: sha, map: world.map, tickSeconds: '1/120', keys: KEYS, record: RECORD,
      fixture: { file: path.basename(out), bytes: blob.length, sha256: crypto.createHash('sha256').update(blob).digest('hex') }, sprintMode: world.sprintMode,
      runtimeStateChanges: ['#start click handler + ENTER THE ARENA button (as tools/n2)', 'fovPanelOpen=true between scenarios (legacy simulation hold), false while a scenario runs synchronously',
        'bots/enemyShots/rockets/grenades emptied', 'player pose/velocity, movement timers, moveFovScale, worldPadCooldown, q3Reset(), now=100 and STAGE4_CONTACTS reset per scenario',
        'KeyboardEvents dispatched to window for key transitions; aim(dx,dy) called for mouse motion', 'player part of update(dt) executed by the tool in legacy order; the remainder of update() not executed'],
      world: { spawns: world.spawns.length, pads: world.pads, ramps: world.ramps.length, lowCeilingSpots: world.ceilings.length, lowestClearance: world.ceilings[0] ? world.ceilings[0].clearance : null },
      scenarios: results.map(({ sc, res }) => ({ name: sc.name, ticks: sc.tape.ticks, start: sc.start, contacts: res.contacts, footsteps: res.steps, viewLandings: res.landings, padLaunchesTotal: res.padLaunches })),
      pageErrors: log };
    if (reportFile) fs.writeFileSync(reportFile, JSON.stringify(report, null, 1) + '\n');
    process.stdout.write(`wrote ${out}: ${results.length} scenarios, ${blob.length} bytes\n`);
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
