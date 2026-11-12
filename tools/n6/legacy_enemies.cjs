#!/usr/bin/env node
// N6 reference recorder: enemies, waves, AI, navigation scheduling, enemy attacks, player damage,
// death and wave restart, recorded from the UNMODIFIED legacy/index.html (hash-checked) in headless
// Chromium (Playwright).
//
// Unlike the N3/N5 recorders, this one does not transcribe update(): for every 1/120 s tick it
// delivers the tape's input events and then calls the legacy update(dt) itself (the page's own loop is
// held with fovPanelOpen between scenarios; inside a scenario the whole run is one synchronous call). Three things are pinned so that the run is
// reproducible, none of which changes a rule of the game:
//   * Math.random is a seeded mulberry32 (the "gameplay stream").
//   * Presentation code that draws random numbers inside update() runs on a second seeded stream or is
//     silenced: every function of WFX and EFX, enemyDeathFx, spawnParticle,
//     spawnImpactSprite, spawnExplosionVisual (cosmetic stream); sounds and the frag celebration (silenced).
//     enemyEvalPose and enemyStartDeath (gait phase, idle seed, pain side, death variant and side) run on
//     a third stream (seed * 131 + 7), so that the animation can be replayed on its own (--pose-out).
//   * performance.now() is constant during update(), so the navigation slice of a tick always gets its
//     full work budget (NAV3_WORK_PER_SLICE = 110 expansions) instead of stopping after 1.8 ms of wall
//     time. The legacy result therefore no longer depends on the speed of the machine.
// Two fixture-only actions exist besides the game's inputs (tape key bits): KILL (damageBot(0, 9999)
// on the first enemy, to drive waves to completion) and HURT (takeDamage(35), to force a death).
//
//   node tools/n6/legacy_enemies.cjs --map helios-crucible --out native/tests/fixtures/n6/helios-crucible_enemies.bin [--report file.json] [--pose-out file.bin]
//   reference pictures:  --frames "scenario:tick,tick;scenario:tick" --frames-dir <dir> [--width 1280 --height 720]
//   staged pictures:     --lineup-dir <dir> [--width 1280 --height 720]
//     Five scenes built by hand at spawn 0 (no update() runs): all 20 types standing; all 20 in their attack wind-up
//     with projected shadows; the final five (strike recovery, hit flash, outlines); ten bodies at different moments
//     of the death animation; two Marine Cats as other players see them. Every random number is 0.5. Writes
//     <shot>.png and lineup.txt, which the native app replays (cat_arena --enemy-lineup lineup.txt --lineup-shot <shot>):
//       shot <name> <shadows> <waveTotal> <waveKills> <now>      player x y z yaw pitch
//       bot <type> x y z facing hitFlash attackTotal attackWind attackRecover attackRecoverTotal
//       corpse <type> x y z facing hitFlash t variant side flash floorZ zNow   (x y z facing hitFlash: the enemy when it died)
//       guest x y z yaw vx vy r g b                              end
//
// Output "CATENMY1", u32 version=1, u32 scenarios, u32 tapeWidth=3, u32 recordWidth=64; per scenario
// u32 nameLength, name, u32 ticks, f64[16] start {x,y,z,yaw,pitch,now,hp,weaponA,weaponB,seed,wave,
// previousWaveTotal,0,0,0,0}, f64[ticks*3] tape {keyMask,dx,dy}, f64[ticks*64] records (RECORD below).
// Scenario start, in this order: state reset, seeded stream installed, waveNumber = wave - 1,
// waveTotal = previousWaveTotal, beginWave(wave).
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');
const { chromium } = require('playwright');
const KEYS = ['KeyW', 'KeyS', 'KeyA', 'KeyD', 'Space', 'ControlLeft', 'ShiftLeft', 'KeyC', null, 'KeyR', 'KeyQ'];   // bit 8 = fire (mouse)
const W = 1, S = 2, A = 4, D = 8, JUMP = 16, CROUCH = 32, SPRINT = 64, SLIDE = 128, FIRE = 256, RELOAD = 512, SWAP = 1024, KILL = 2048, HURT = 4096;
const RECORD = ['x', 'y', 'z', 'vx', 'vy', 'vz', 'ground', 'hp', 'yaw', 'pitch', 'slot', 'weapon0', 'weapon1', 'mag0', 'mag1', 'cooldown', 'reloading', 'rngCalls', 'score', 'deaths', 'respawnGrace', 'burnRemaining', 'flash',
  'waveNumber', 'waveTotal', 'waveSpawned', 'waveKills', 'waveCooldown', 'waveSpawnTimer',
  'bots', 'botSumX', 'botSumY', 'botSumZ', 'botSumHp', 'botSumFacing', 'botSumCool', 'botSumAttackWind', 'botSumAttackRecover', 'botSumWalkAmount', 'botSumAnimPhase', 'botSumPathIndex', 'botSumPathLength',
  'botSumNavStuck', 'botSumPathCooldown', 'botsQueued', 'botSumSpeed', 'botSumUid', 'botSumType',
  'enemyShots', 'shotSumX', 'shotSumY', 'shotSumZ', 'shotSumDamage', 'shotSumLife',
  'navQueue', 'navQueries', 'navSuccess', 'navFailed', 'navShared', 'navActiveExpanded', 'playerProjectiles', 'playerProjectileSum', 'dying', 'aiDecisionAcc'];
// Animation fixture (--pose-out): "CATPOSE1", u32 version=2, u32 scenarios, u32 width=81; per scenario u32 nameLength, name, u32 ticks, f64[ticks*81].
const POSE_WIDTH = 81, POSE = '0-15 sum of the 16 pose values over enemies | 16-31 the same weighted by (uid % 7) + 1 | 32 enemies | 33 animation random draws | 34-36 left hand sum | 37-39 right hand sum | 40 sum of attack progress | 41 dying | 42-44 death fall, side, sink sums | 45-60 death pose sums | 61 flash sum | 62 sum of variant + 10 * side | 63 sum of z | 64-79 Marine Cat pose of the local player | 80 cosmetic random draws so far (WFX / EFX / death effects: their branches depend on the values drawn, so the count follows the effect simulation)';
function tape(ticks) { return { ticks, data: new Float64Array(ticks * 3) }; }
function fill(t, from, to, mask, dx = 0, dy = 0) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) { t.data[i * 3] = mask; t.data[i * 3 + 1] = dx; t.data[i * 3 + 2] = dy; } }
function or(t, from, to, mask) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) t.data[i * 3] |= mask; }
function look(t, at, dx, dy) { if (at < t.ticks) { t.data[at * 3 + 1] += dx; t.data[at * 3 + 2] += dy; } }

(async () => {
  const v = process.argv.slice(2); let out = null, reportFile = null, map = 'helios-crucible', only = null, scale = 1;
  let poseOut = null, framesArg = null, framesDir = null, width = 640, height = 360, lineupDir = null;
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') out = v[++i]; else if (v[i] === '--pose-out') poseOut = v[++i]; else if (v[i] === '--frames') framesArg = v[++i]; else if (v[i] === '--frames-dir') framesDir = v[++i]; else if (v[i] === '--lineup-dir') lineupDir = v[++i]; else if (v[i] === '--width') width = Number(v[++i]); else if (v[i] === '--height') height = Number(v[++i]); else if (v[i] === '--report') reportFile = v[++i]; else if (v[i] === '--map') map = v[++i]; else if (v[i] === '--only') only = v[++i]; else if (v[i] === '--scale') scale = Number(v[++i]); }
  if (lineupDir && !out) out = path.join(lineupDir, 'unused.bin');
  if (!out) throw new Error('usage: --map <id> --out <file.bin> [--report file.json] [--only name] [--scale 0.5]');
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => { const url = new URL(req.url, 'http://localhost'); if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  const errors = [];
  try {
    const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: 1 });
    page.on('pageerror', e => errors.push(String(e).slice(0, 300)));
    page.setDefaultTimeout(1800000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${map}`, { waitUntil: 'load', timeout: 900000 });
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: 900000, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true, null, { timeout: 900000, polling: 500 });
    const world = await page.evaluate(() => { fovPanelOpen = true; return { map: window.VOID_ACTIVE_MAP, spawns: WORLD.spawns.map(s => ({ x: s.x, y: s.y, z: s.z, a: s.a || 0 })), navNodes: NAV3.nodes.length, navReady: NAV3.ready, types: ENEMY_TYPES.slice() }; });
    if (!world.navReady) throw new Error('navigation graph not ready');
    const sp = k => world.spawns[k % world.spawns.length];
    const T = n => Math.max(240, Math.round(n * scale));
    const scenarios = [];
    const add = (name, t, start) => { if (!only || only === name) scenarios.push({ name, tape: t, start: { pitch: 0, now: 100, hp: 100, wave: 1, prevTotal: 10, ...start } }); };
    // 1. no input: the wave closes in and attacks; the player dies if it takes long enough
    { const t = tape(T(3000)); add('stand_still', t, { ...sp(0), wa: 0, wb: 1, seed: 6101 }); }
    // 2. fight: held fire in bursts, slow spin, strafing, jumps, swaps
    { const n = T(3600), t = tape(n); for (let i = 0; i < n; i++) { let m = 0; const k = i % 360; if (k < 250) m |= FIRE; if (i % 240 < 120) m |= A; else m |= D; if (i % 600 > 450) m |= W; if (i % 420 === 60 || i % 420 === 61) m |= JUMP; if (i % 900 === 500 || i % 900 === 501) m |= SWAP; t.data[i * 3] = m; t.data[i * 3 + 1] = 5; t.data[i * 3 + 2] = (i % 400 < 200 ? .4 : -.4); }
      add('fight_spin', t, { ...sp(1), wa: 11, wb: 4, seed: 6102 }); }
    // 3. run: sprint with turns, jumps and a slide, no firing (chasing, re-routing, far strides)
    { const n = T(3600), t = tape(n); for (let i = 0; i < n; i++) { let m = W | SPRINT; if (i % 300 === 100 || i % 300 === 101) m |= JUMP; if (i % 700 === 400 || i % 700 === 401) m |= SLIDE; if (i % 500 > 440) m = (m & ~W) | A; t.data[i * 3] = m; t.data[i * 3 + 1] = (Math.floor(i / 150) % 3 === 0 ? 7 : Math.floor(i / 150) % 3 === 1 ? -3 : 0); }
      add('sprint_circuit', t, { ...sp(2), wa: 5, wb: 8, seed: 6103 }); }
    // 4. waves: the first enemy is removed every 40 ticks (fixture action) -> waves 1..4, scaling, counts
    { const n = T(4200), t = tape(n); for (let i = 120; i < n; i += 40) t.data[i * 3] |= KILL; for (let i = 0; i < n; i++) { if (i % 200 < 60) t.data[i * 3] |= FIRE; t.data[i * 3 + 1] = 2; }
      add('wave_progression', t, { ...sp(3), wa: 3, wb: 15, seed: 6104 }); }
    // 5. flyers: wave 5 starts with a probe, a raider and a juggernaut; the player fights back and jumps
    { const n = T(2400), t = tape(n); for (let i = 0; i < n; i++) { let m = 0; if (i % 300 < 200) m |= FIRE; if (i % 360 === 30 || i % 360 === 31) m |= JUMP; if (i % 480 < 160) m |= S; t.data[i * 3] = m; t.data[i * 3 + 1] = -4; t.data[i * 3 + 2] = (i % 240 < 40 ? -2 : i % 240 < 80 ? 2 : 0); }
      add('flyers_wave5', t, { ...sp(4), wa: 8, wb: 13, seed: 6105, wave: 5, prevTotal: 22 }); }
    // 6. death and wave restart: forced damage kills the player twice; soft restart of wave 1 each time
    { const n = T(2400), t = tape(n); for (const at of [300, 330, 360, 1300, 1330, 1360]) if (at < n) t.data[at * 3] |= HURT; for (let i = 0; i < n; i++) { if (i % 500 < 100) t.data[i * 3] |= W; t.data[i * 3 + 1] = 3; }
      add('death_and_restart', t, { ...sp(5), wa: 12, wb: 7, seed: 6106, wave: 3, prevTotal: 13 }); }

    // Fill every adjacency list once, outside the recording (the game does this in idle ticks by wall
    // time; the lists are a cache, their content does not depend on when they are filled).
    process.stdout.write(`map ${world.map}: ${world.navNodes} nav nodes, ${world.spawns.length} spawns; filling adjacency ...\n`);
    for (let guard = 0; guard < 4000; guard++) { const done = await page.evaluate(() => { fovPanelOpen = true; nav3PrecomputeAdjacency(400); return nav3PrecomputeCursor >= NAV3.nodes.length * 3; }); if (done) break; }
    process.stdout.write('adjacency ready\n');
    // Reference pictures: the legacy render() after the given ticks (HUD hidden, lens flares off). While a picture
    // is taken every random number is 0.5, so pictures change no recorded value and no stream.
    const wanted = new Map();
    if (framesArg) { if (!framesDir) throw new Error('--frames needs --frames-dir'); for (const part of framesArg.split(';')) { const [name, list] = part.split(':'); wanted.set(name, list.split(',').map(Number)); } fs.mkdirSync(framesDir, { recursive: true });
      await page.evaluate(() => { setHudVisibility(false); Q3R.S.flares = false; window.__SHOWCASE_HIDE_FP = false;
        window.__n6Grab = () => { const game = Math.random; window.__grabbing = true; Math.random = () => .5; try { render(); } finally { Math.random = game; window.__grabbing = false; }
          const w = canvas.width, h = canvas.height, px = new Uint8Array(w * h * 4); gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const c = document.createElement('canvas'); c.width = w; c.height = h; const cx = c.getContext('2d'), img = cx.createImageData(w, h);
          for (let y = 0; y < h; y++) img.data.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255; cx.putImageData(img, 0, 0); return c.toDataURL('image/png'); }; });
    }
    if (lineupDir) {   // staged pictures only (see the header): no tape is recorded
      fs.mkdirSync(lineupDir, { recursive: true });
      const staged = await page.evaluate(() => {
        const realRandom = Math.random; Math.random = () => .5;   // every draw is 0.5: gait phase 3.14, idle seed 50, pain side +1
        setHudVisibility(false); Q3R.S.flares = false; window.__SHOWCASE_HIDE_FP = false;
        const grab = () => { render(); const w = canvas.width, h = canvas.height, px = new Uint8Array(w * h * 4); gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const c = document.createElement('canvas'); c.width = w; c.height = h; const cx = c.getContext('2d'), img = cx.createImageData(w, h);
          for (let y = 0; y < h; y++) img.data.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255; cx.putImageData(img, 0, 0); return c.toDataURL('image/png'); };
        const savedShadows = Q3R.S.shadows, lines = [], pictures = [];
        try {
          fovPanelOpen = true;
          const sp = WORLD.spawns[0], P = { x: sp.x, y: sp.y, z: sp.z, a: sp.a || 0 };
          const reset = () => {
            bots.length = 0; enemyShots.length = 0; rockets.length = 0; grenades.length = 0; energyBolts.length = 0; bullets.length = 0; particles.length = 0; explosions.length = 0; impactFx.length = 0; dyingEnemies.length = 0; netPlayers.clear();
            WFX.clear(); pickups = worldPickupSpots();
            Object.assign(player, { x: P.x, y: P.y, z: P.z, a: P.a, pitch: 0, vx: 0, vy: 0, vz: 0, ground: true, crouchOffset: 0, downed: false, hp: 100 });
            moveFovScale = 1; q3Reset(); now = 100; respawnGrace = 0; burnRemaining = 0; flash = 0; walkCycle = 0; enemySerial = 0;
            loadout = [0, 1]; slot = 0; mag = [WEAPONS[0].clip, WEAPONS[1].clip]; reloading = 0; cooldown = 0; gunKick = 0; muzzleFlashT = 0;
            WFX.viewmodelOffsets(0); WFX.update(1);   // the weapon is up and at rest
            waveNumber = 1; waveTotal = 30; waveKills = 0; waveSpawned = 30; waveCooldown = -1;
          };
          const ca = Math.cos(P.a), sa = Math.sin(P.a);
          const spot = (d, side) => ({ x: P.x + ca * d - sa * side, y: P.y + sa * d + ca * side });
          const put = (type, d, side, o = {}) => { spawnBot(false, type); const b = bots[bots.length - 1], q = spot(d, side);
            b.x = q.x; b.y = q.y; b.z = P.z + (b.flying ? 1.7 : 0); b.a = enemyFacingFromVec(P.x - b.x, P.y - b.y); b.hitFlash = 0; b.path = null; Object.assign(b, o); return b; };
          const row = k => [k < 10 ? 5.5 : 8.5, ((k % 10) - 4.5) * 1.45];
          const f = n => String(n);
          const shot = (name, shadows) => { Q3R.S.shadows = shadows;
            lines.push(`shot ${name} ${shadows} ${waveTotal} ${waveKills} ${f(now)}`, `player ${f(player.x)} ${f(player.y)} ${f(player.z)} ${f(player.a)} ${f(player.pitch)}`);
            for (const b of bots) lines.push(`bot ${ENEMY_TYPES.indexOf(b.type)} ${f(b.x)} ${f(b.y)} ${f(b.z)} ${f(b.a)} ${f(b.hitFlash)} ${f(b.attackTotal || 0)} ${f(b.attackWind || 0)} ${f(b.attackRecover || 0)} ${f(b.attackRecoverTotal || 0)}`);
            for (const d of dyingEnemies) lines.push(`corpse ${ENEMY_TYPES.indexOf(d.type)} ${f(d.sx)} ${f(d.sy)} ${f(d.sz)} ${f(d.a)} ${f(d.sHit)} ${f(d.t)} ${d.variant} ${d.side} ${f(d.flash)} ${f(d.floorZ)} ${f(d.z)}`);
            for (const g of netPlayers.values()) lines.push(`guest ${f(g.x)} ${f(g.y)} ${f(g.z)} ${f(g.a)} ${f(g.vx)} ${f(g.vy)} ${g.color.map(f).join(' ')}`);
            lines.push('end'); pictures.push([name, grab(), { bots: bots.length, dying: dyingEnemies.length, guests: netPlayers.size }]); };
          // 1. all 20 types standing, blob shadows
          reset(); ENEMY_TYPES.forEach((t, k) => put(t, ...row(k))); shot('idle', 1);
          // 2. all 20 types in the wind-up of their attack (progress 0.36), projected shadows (cg_shadows 3)
          reset(); ENEMY_TYPES.forEach((t, k) => put(t, ...row(k), { attackTotal: 1, attackWind: .5 })); shot('windup', 3);
          // 3. the final five: recovering from the strike (progress 0.86), freshly hit, outlines on
          reset(); waveTotal = 10; waveKills = 5; waveSpawned = 10;
          [['grunt', 5, -3], ['shambler', 6, -1.2], ['pyromancer', 7, .8], ['skyprobe', 6, 2.6], ['fiend', 9, 4.4]].forEach(([t, d, s]) => put(t, d, s, { attackTotal: 1, attackWind: 0, attackRecover: .5, attackRecoverTotal: 1, hitFlash: .15 })); shot('strike_final5', 1);
          // 4. bodies: every death variant at several moments of the fall, the rest and the sinking
          reset();
          [['grunt', 0, .25], ['ogre', 1, .5], ['fiend', 2, .78], ['skyraider', 3, .6], ['warden', 0, 2], ['skitter', 1, 3], ['banshee', 2, 5.6], ['crusher', 0, 6.3], ['cultist', 1, .1], ['harrier', 2, .4]].forEach(([t, variant, time], k) => {
            const b = put(t, k < 5 ? 5 : 7.5, ((k % 5) - 2) * 2.3, { hitFlash: .21 }), i = bots.indexOf(b);
            enemyStartDeath(b, { variant }); bots.splice(i, 1);
            const d = dyingEnemies[dyingEnemies.length - 1]; d.sx = b.x; d.sy = b.y; d.sz = b.z; d.sHit = b.hitFlash; d.side = k % 2 ? 1 : -1; d.t = time; d.flash = Math.max(0, 1 - time * 5); if (d.variant === 3) d.z = d.floorZ; });
          shot('bodies', 1);
          // 5. Marine Cats as other players see them (two guests), no enemies
          reset();
          { const a = spot(4.2, -1.1), b = spot(5.5, 1.4);
            netPlayers.set('guest-a', { x: a.x, y: a.y, z: P.z, a: P.a + Math.PI, vx: 0, vy: 0, step: 0, name: 'Guest A', color: [1, 1, 1] });
            netPlayers.set('guest-b', { x: b.x, y: b.y, z: P.z, a: P.a + Math.PI / 2, vx: 0, vy: 0, step: 0, name: 'Guest B', color: [.3, .6, 1] }); }
          shot('marines', 1);
          return { lines, pictures, player: P };
        } finally { Math.random = realRandom; Q3R.S.shadows = savedShadows; bots.length = 0; dyingEnemies.length = 0; netPlayers.clear(); fovPanelOpen = true; }
      });
      if (errors.length) throw new Error('page errors: ' + errors.join(' | '));
      fs.writeFileSync(path.join(lineupDir, 'lineup.txt'), `# tools/n6/legacy_enemies.cjs --lineup-dir: staged scenes of ${world.map} (see the tool header for the line formats)\n` + staged.lines.join('\n') + '\n');
      for (const [name, url, info] of staged.pictures) { const file = path.join(lineupDir, name + '.png'); fs.writeFileSync(file, Buffer.from(url.split(',')[1], 'base64')); process.stdout.write(`picture ${file} ${JSON.stringify(info)}\n`); }
      return;
    }
    const results = [];
    for (const sc of scenarios) {
      const res = await page.evaluate(([sc, tapeData, KEYS, REC, shotTicks]) => {
        const pictures = [];
        const realRandom = Math.random, realNow = performance.now;
        let seed = sc.start.seed | 0, rngCalls = 0;
        const game = () => { rngCalls++; seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
        let cseed = (sc.start.seed * 31 + 17) | 0, cosCalls = 0;
        const cosRandom = () => { cosCalls++; cseed |= 0; cseed = cseed + 0x6D2B79F5 | 0; let t = Math.imul(cseed ^ cseed >>> 15, 1 | cseed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
        // animation: enemyEvalPose (random gait phase, idle seed, pain side) and enemyStartDeath (death variant and side) on a third stream
        let pseed = (sc.start.seed * 131 + 7) | 0, poseCalls = 0;
        const poseRandom = () => { poseCalls++; pseed |= 0; pseed = pseed + 0x6D2B79F5 | 0; let t = Math.imul(pseed ^ pseed >>> 15, 1 | pseed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
        const posed = f => function (...a) { if (window.__grabbing) return f.apply(this, a); const g = Math.random; Math.random = poseRandom; try { return f.apply(this, a); } finally { Math.random = g; } };
        const cosmetic = f => function (...a) { if (window.__grabbing) return f.apply(this, a); const g = Math.random; Math.random = cosRandom; try { return f.apply(this, a); } finally { Math.random = g; } };
        // presentation on its own stream / silenced
        const savedWfx = {}, savedEfx = {};
        for (const k of Object.keys(WFX)) if (typeof WFX[k] === 'function') { savedWfx[k] = WFX[k]; WFX[k] = cosmetic(WFX[k]); }
        for (const k of Object.keys(EFX)) if (typeof EFX[k] === 'function') { savedEfx[k] = EFX[k]; EFX[k] = cosmetic(EFX[k]); }
        const saved = { playGunSound, playExplosionSound, playEnemyVoice, vaCelebrateFrag, playBodySound, beep, enemyDeathFx, enemyStartDeath, spawnParticle, spawnImpactSprite, spawnExplosionVisual, enemyEvalPose };
        playGunSound = playExplosionSound = playEnemyVoice = vaCelebrateFrag = playBodySound = beep = () => {};
        enemyDeathFx = cosmetic(saved.enemyDeathFx); enemyStartDeath = posed(saved.enemyStartDeath); enemyEvalPose = posed(saved.enemyEvalPose); spawnParticle = cosmetic(saved.spawnParticle);
        spawnImpactSprite = cosmetic(saved.spawnImpactSprite); spawnExplosionVisual = cosmetic(saved.spawnExplosionVisual);
        try {
          fovPanelOpen = true;   // (the page's own loop cannot run inside this call; the flag only matters between calls)
          for (const code of Object.keys(keys)) if (keys[code]) window.dispatchEvent(new KeyboardEvent('keyup', { code, bubbles: true }));
          keys = {}; jumpHeld = false; fireHeld = false; touchStick = { x: 0, y: 0 };
          bots.length = 0; enemyShots.length = 0; rockets.length = 0; grenades.length = 0; energyBolts.length = 0; bullets.length = 0; particles.length = 0; explosions.length = 0; impactFx.length = 0; dyingEnemies.length = 0;
          WFX.clear(); if (EFX.clear) EFX.clear();
          pickups = worldPickupSpots();
          const st = sc.start;
          Object.assign(player, { x: st.x, y: st.y, z: st.z, a: st.a, pitch: st.pitch, vx: 0, vy: 0, vz: 0, ground: true, crouchOffset: 0, downed: false, hp: st.hp });
          slideTime = 0; slideCooldown = 0; slideSpeed = 0; slideDirX = 0; slideDirY = 0; slideBuffer = 0; jumpBuffer = 0; coyoteTime = 0; movementSlopeAngle = 0; airStrafeGained = 0;
          moveFovScale = 1; worldPadCooldown = 0; q3Reset(); now = st.now; respawnGrace = 0; burnRemaining = 0; burnTick = 0; flash = 0; score = 0; deaths = 0;
          respawnQueued = false; pendingShotPurge = false; waveResetPending = false; aiDecisionAcc = 0; navSearchPool = 0; enemySerial = 0; walkCycle = 0;
          loadout = [st.wa, st.wb]; slot = 0; mag = [WEAPONS[st.wa].clip, WEAPONS[st.wb].clip]; reloading = 0; cooldown = 0; gunKick = 0; muzzleFlashT = 0;
          for (const k of ['queries', 'success', 'failed', 'sharedRoutes']) NAV3[k] = 0;
          NAV3_ROUTES.length = 0; nav3RouteSlot = 0;   // shared-route ring empty at the start of every scenario
          performance.now = () => 0;
          fovPanelOpen = false;   // input handlers ignore weapon / slide keys while a panel is open
          Math.random = game;
          waveNumber = Math.max(1, st.wave - 1); waveTotal = st.prevTotal;
          beginWave(st.wave);
          const ticks = tapeData.length / 3, out = new Array(ticks * REC).fill(0), dt = 1 / 120;
          const PW = 81, poses = new Array(ticks * PW).fill(0), deathPose = new Float32Array(16), marine = { color: [1, 1, 1], _isMarine: true }, marinePose = new Float32Array(16);
          let mask = 0, maxBots = 0, maxShots = 0, minHp = 1e9, maxWave = 0, kills = 0, typesSeen = {};
          for (let i = 0; i < ticks; i++) {
            const next = tapeData[i * 3], changed = next ^ mask;
            for (let b = 0; b < KEYS.length; b++) if (KEYS[b] && (changed & (1 << b))) window.dispatchEvent(new KeyboardEvent((next & (1 << b)) ? 'keydown' : 'keyup', { code: KEYS[b], bubbles: true }));
            if (changed & 256) { if (next & 256) { fireHeld = true; fireWeapon(); } else fireHeld = false; }
            if ((next & 2048) && bots.length) damageBot(0, 9999, bots[0].z + 1, null, 'body');   // fixture action KILL
            if (next & 4096) takeDamage(35);                                                    // fixture action HURT
            mask = next;
            const dx = tapeData[i * 3 + 1], dy = tapeData[i * 3 + 2];
            if (dx || dy) aim(dx, dy);
            update(dt);
            // ---- record ----
            const p = player, o = out, q = i * REC;
            o[q] = p.x; o[q + 1] = p.y; o[q + 2] = p.z; o[q + 3] = p.vx; o[q + 4] = p.vy; o[q + 5] = p.vz; o[q + 6] = p.ground ? 1 : 0; o[q + 7] = p.hp; o[q + 8] = p.a; o[q + 9] = p.pitch;
            o[q + 10] = slot; o[q + 11] = loadout[0]; o[q + 12] = loadout[1]; o[q + 13] = mag[0]; o[q + 14] = mag[1]; o[q + 15] = cooldown; o[q + 16] = reloading; o[q + 17] = rngCalls; o[q + 18] = score; o[q + 19] = deaths;
            o[q + 20] = respawnGrace; o[q + 21] = burnRemaining; o[q + 22] = flash;
            o[q + 23] = waveNumber; o[q + 24] = waveTotal; o[q + 25] = waveSpawned; o[q + 26] = waveKills; o[q + 27] = waveCooldown; o[q + 28] = waveSpawnTimer;
            o[q + 29] = bots.length;
            for (const b of bots) { o[q + 30] += b.x; o[q + 31] += b.y; o[q + 32] += b.z; o[q + 33] += b.hp; o[q + 34] += b.a; o[q + 35] += b.cool; o[q + 36] += b.attackWind; o[q + 37] += b.attackRecover; o[q + 38] += b.walkAmount; o[q + 39] += b.animPhase;
              o[q + 40] += b.pathIndex || 0; o[q + 41] += b.path ? b.path.length : 0; o[q + 42] += b.navStuck || 0; o[q + 43] += b.pathCooldown || 0; o[q + 44] += b.navQueued ? 1 : 0; o[q + 45] += b.speed; o[q + 46] += b.uid; o[q + 47] += ENEMY_TYPES.indexOf(b.type); typesSeen[b.type] = 1; }
            o[q + 48] = enemyShots.length;
            for (const s of enemyShots) { o[q + 49] += s.x; o[q + 50] += s.y; o[q + 51] += s.z; o[q + 52] += s.damage; o[q + 53] += s.life; }
            o[q + 54] = navQueue.length; o[q + 55] = NAV3.queries; o[q + 56] = NAV3.success; o[q + 57] = NAV3.failed; o[q + 58] = NAV3.sharedRoutes || 0; o[q + 59] = navActiveJob ? navActiveJob.expanded : -1;
            o[q + 60] = rockets.length + grenades.length + energyBolts.length;
            for (const list of [rockets, grenades, energyBolts]) for (const r of list) o[q + 61] += r.x + r.y + r.z;
            o[q + 62] = dyingEnemies.length; o[q + 63] = aiDecisionAcc;
            // ---- animation record (POSE below): every enemy is posed once per tick, as a 120 fps frame with everything in view ----
            { const w = i * PW;
              for (const b of bots) { const pose = b._pose || (b._pose = new Float32Array(16)); enemyEvalPose(b, ENEMY_RIGS[b.type], pose, now, 0);
                const k = (b.uid % 7) + 1; for (let j = 0; j < 16; j++) { poses[w + j] += pose[j]; poses[w + 16 + j] += pose[j] * k; }
                const hl = enemyHandWorld(b, -1, pose), hr = enemyHandWorld(b, 1, pose); poses[w + 34] += hl.x; poses[w + 35] += hl.y; poses[w + 36] += hl.z; poses[w + 37] += hr.x; poses[w + 38] += hr.y; poses[w + 39] += hr.z;
                poses[w + 40] += enemyAttackProgress(b); }
              poses[w + 32] = bots.length; poses[w + 41] = dyingEnemies.length;
              for (const d of dyingEnemies) { const st = enemyDeathState(d, deathPose); poses[w + 42] += st.fall; poses[w + 43] += st.side; poses[w + 44] += st.sink; for (let j = 0; j < 16; j++) poses[w + 45 + j] += deathPose[j];
                poses[w + 61] += d.flash; poses[w + 62] += d.variant + 10 * d.side; poses[w + 63] += d.z; }
              // the local player as a Marine Cat (what a mirror or another player sees): drawMarineCats(true) without its GL calls
              marine.x = player.x; marine.y = player.y; marine.z = player.z; marine.a = player.a; marine.vx = player.vx; marine.vy = player.vy;
              enemyEvalPose(marine, MARINE_RIG, marinePose, now, 0); for (let j = 0; j < 16; j++) poses[w + 64 + j] = marinePose[j];
              poses[w + 33] = poseCalls; poses[w + 80] = cosCalls; }
            // Picture runs follow the first-person weapon every tick, as a frame per tick would (WFX.viewmodelOffsets
            // notices weapon changes and starts the raise animation); it draws no random numbers.
            if (shotTicks.length) WFX.viewmodelOffsets(loadout[slot]);
            if (shotTicks.includes(i)) pictures.push([i, window.__n6Grab(), { bots: bots.length, shots: enemyShots.length, dying: dyingEnemies.length, hp: player.hp, wave: waveNumber, left: waveTotal - waveKills, entityGrid: Q3R.S.entityGrid, shadows: Q3R.S.shadows }]);
            maxBots = Math.max(maxBots, bots.length); maxShots = Math.max(maxShots, enemyShots.length); minHp = Math.min(minHp, p.hp); maxWave = Math.max(maxWave, waveNumber); kills = score;
          }
          return { pictures, records: out, poses, poseCalls, finite: out.every(Number.isFinite) && poses.every(Number.isFinite), rngCalls, cosCalls, maxBots, maxShots, minHp, maxWave, kills, deaths, types: Object.keys(typesSeen), nav: { queries: NAV3.queries, success: NAV3.success, failed: NAV3.failed, shared: NAV3.sharedRoutes || 0 } };
        } finally {
          Math.random = realRandom; performance.now = realNow;
          for (const k of Object.keys(savedWfx)) WFX[k] = savedWfx[k];
          for (const k of Object.keys(savedEfx)) EFX[k] = savedEfx[k];
          ({ playGunSound, playExplosionSound, playEnemyVoice, vaCelebrateFrag, playBodySound, beep, enemyDeathFx, enemyStartDeath, spawnParticle, spawnImpactSprite, spawnExplosionVisual, enemyEvalPose } = saved);
          for (const code of Object.keys(keys)) if (keys[code]) window.dispatchEvent(new KeyboardEvent('keyup', { code, bubbles: true }));
          fireHeld = false; fovPanelOpen = true; bots.length = 0; enemyShots.length = 0;
        }
      }, [sc, Array.from(sc.tape.data), KEYS, RECORD.length, wanted.get(sc.name) || []]);
      for (const [tick, url, info] of res.pictures) { const file = path.join(framesDir, `${sc.name}_${String(tick).padStart(5, '0')}.png`); fs.writeFileSync(file, Buffer.from(url.split(',')[1], 'base64')); process.stdout.write(`picture ${file} ${JSON.stringify(info)}\n`); }
      if (!res.finite) throw new Error('non-finite record in ' + sc.name);
      process.stdout.write(`${sc.name}: ${sc.tape.ticks} ticks, max enemies ${res.maxBots}, max enemy shots ${res.maxShots}, min hp ${res.minHp}, kills ${res.kills}, deaths ${res.deaths}, highest wave ${res.maxWave}, types ${res.types.length}, routes ${res.nav.success}/${res.nav.queries} (shared ${res.nav.shared}, failed ${res.nav.failed}), random draws ${res.rngCalls} gameplay / ${res.cosCalls} cosmetic / ${res.poseCalls} animation\n`);
      results.push({ sc, res });
    }
    if (errors.length) throw new Error('page errors: ' + errors.join(' | '));
    const chunks = []; const w32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); chunks.push(b); }; const w64 = a => chunks.push(Buffer.from(new Float64Array(a).buffer));
    chunks.push(Buffer.from('CATENMY1')); w32(1); w32(results.length); w32(3); w32(RECORD.length);
    for (const { sc, res } of results) { const name = Buffer.from(sc.name), s = sc.start; w32(name.length); chunks.push(name); w32(sc.tape.ticks);
      w64([s.x, s.y, s.z, s.a, s.pitch, s.now, s.hp, s.wa, s.wb, s.seed, s.wave, s.prevTotal, 0, 0, 0, 0]); w64(sc.tape.data); w64(res.records); }
    const blob = Buffer.concat(chunks); fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true }); fs.writeFileSync(out, blob);
    const digest = crypto.createHash('sha256').update(blob).digest('hex');
    process.stdout.write(`wrote ${out}: ${results.length} scenarios, ${results.reduce((a, r) => a + r.sc.tape.ticks, 0)} ticks, ${blob.length} bytes, sha256 ${digest}\n`);
    let poseInfo = null;
    if (poseOut) {
      const pc = []; const p32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); pc.push(b); };
      pc.push(Buffer.from('CATPOSE1')); p32(2); p32(results.length); p32(POSE_WIDTH);
      for (const { sc, res } of results) { const name = Buffer.from(sc.name); p32(name.length); pc.push(name); p32(sc.tape.ticks); if (res.poses.length !== sc.tape.ticks * POSE_WIDTH) throw new Error('pose record size'); pc.push(Buffer.from(new Float64Array(res.poses).buffer)); }
      const pblob = Buffer.concat(pc); fs.writeFileSync(poseOut, pblob);
      poseInfo = { file: poseOut, bytes: pblob.length, sha256: crypto.createHash('sha256').update(pblob).digest('hex'), width: POSE_WIDTH, layout: POSE };
      process.stdout.write(`wrote ${poseOut}: ${pblob.length} bytes, sha256 ${poseInfo.sha256}\n`);
    }
    if (reportFile) fs.writeFileSync(reportFile, JSON.stringify({ tool: 'tools/n6/legacy_enemies.cjs', source: 'legacy/index.html', sha256: sha, map: world.map, navNodes: world.navNodes, enemyTypes: world.types, record: RECORD, keyBits: KEYS.concat(['(fixture) KILL', '(fixture) HURT']),
      fixture: { file: out, bytes: blob.length, sha256: digest }, poseFixture: poseInfo, scenarios: results.map(({ sc, res }) => ({ name: sc.name, ticks: sc.tape.ticks, start: sc.start, maxEnemies: res.maxBots, maxEnemyShots: res.maxShots, minHp: res.minHp, kills: res.kills, deaths: res.deaths, highestWave: res.maxWave, types: res.types, nav: res.nav, gameplayDraws: res.rngCalls, cosmeticDraws: res.cosCalls, animationDraws: res.poseCalls })) }, null, 1) + '\n');
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
