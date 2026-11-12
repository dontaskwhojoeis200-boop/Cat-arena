#!/usr/bin/env node
// N5 weapon / damage reference: runs the UNMODIFIED legacy/index.html in headless Chromium and
// records weapon, projectile, damage and player state for every 1/120 s tick of scripted input.
//
//   node tools/n5/legacy_combat.cjs --out native/tests/fixtures/n5/helios_combat.bin [--report file.json]
//                                   [--fx-out native/tests/fixtures/n5/helios_weapon_fx.bin]
//
// A tick (page state only; the file is hash-checked and never modified):
//   1. key changes as real KeyboardEvents (movement keys, KeyR reload, KeyQ swap); the fire button as
//      the legacy mousedown handler does it (fireHeld=true; fireWeapon()) and mouseup (fireHeld=false);
//      mouse motion through the legacy aim();
//   2. the relevant part of the legacy update(dt), transcribed in its original order with the
//      original functions: timers, player movement (as tools/n3), `if(fireHeld)fireWeapon()`, the
//      reload timer, the rocket / grenade / energy-bolt loops and the health-pickup loop.
// Not run: enemy AI, enemy shots, waves, burn, respawn (scenarios never kill the player).
// Targets are plain objects in the legacy `bots` array (position, type, hp): the legacy hit boxes,
// shotSweep, explode and damageBot run on them unchanged; nothing moves them.
// Determinism: Math.random is replaced by a seeded mulberry32 for the run, and cosmetic functions
// that could draw random numbers are replaced by no-ops (sounds, voices, weapon / death effects), so
// every random number comes from game rules (pellet spread, the hurt roll in damageBot, weapon
// reroll on a kill). WFX.fire / WFX.explosion are replaced by recorders of what they were given.
// --fx-out additionally runs the legacy weapon effects (weapon-fx.js) for the same tapes and writes a
// second fixture. The effect code (WFX.fire / explosion / update / build, renderGun3D) runs with its
// OWN seeded random stream, so the gameplay records and the --out fixture are byte-identical with or
// without it. Per tick it records the viewmodel camera that renderGun3D sends to the shader, the
// weapon glow, the number of quads and checksums of the three effect batches (main, mark, gun), the
// dynamic lights, the recoil spring and the world-space muzzle point:
// "CATWFX01", u32 version=2, u32 scenarios, u32 recordWidth=43, f64 horizontalFov, f64 aspect; per
// scenario u32 nameLength, name, u32 ticks, f64 cosmeticSeed, f64[ticks*43] (FX_RECORD below).
// Version 2 (N5.1) appends the dynamic lights Q3R.uploadLights selects for the frame; the first 40
// values per tick are unchanged from version 1.
// Reference pictures (need --fx-out, 1280x720 by default: --width / --height):
//   --frames "scenario:tick,tick;scenario:tick" --frames-dir <dir>   the legacy render() at those ticks of a tape
//   --views-dir <dir>                                                every weapon at rest in the firing lane
// render() runs with the HUD hidden and lens flares off (not part of N5) and with a throw-away
// Math.random, so taking pictures does not change any recorded number.
// Output: "CATCMBT1", u32 version=1, u32 scenarios, u32 tapeWidth=3, u32 recordWidth=48; per scenario
// u32 nameLength, name, u32 ticks, f64[12] start {x,y,z,yaw,pitch,now,hp,weaponA,weaponB,seed,targets,0},
// f64[targets*5] {x,y,z,class(0 standard,1 heavy,2 fast),hp}, f64[ticks*3] tape {keyMask,dx,dy},
// f64[ticks*48] records (RECORD below; native/tests/combat_tests.cpp reads the same layout).
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');
const { chromium } = require('playwright');
const KEYS = ['KeyW', 'KeyS', 'KeyA', 'KeyD', 'Space', 'ControlLeft', 'ShiftLeft', 'KeyC', null, 'KeyR', 'KeyQ'];   // bit 8 = fire (mouse)
const W = 1, S = 2, A = 4, D = 8, JUMP = 16, CROUCH = 32, SPRINT = 64, SLIDE = 128, FIRE = 256, RELOAD = 512, SWAP = 1024;
const RECORD = ['x', 'y', 'z', 'vx', 'vy', 'vz', 'ground', 'hp', 'slot', 'weapon0', 'weapon1', 'mag0', 'mag1', 'cooldown', 'reloading', 'gunKick', 'muzzleFlash', 'rngCalls',
  'rockets', 'grenades', 'bolts', 'projSumX', 'projSumY', 'projSumZ', 'shots', 'shotHits', 'enemyHits', 'shotEndSumX', 'shotEndSumY', 'shotEndSumZ',
  'explosions', 'explosionX', 'explosionY', 'explosionZ', 'explosionPower', 'targetHp0', 'targetHp1', 'targetHp2', 'targetHp3', 'targetHp4', 'targetHp5', 'targetHp6', 'targetHp7',
  'targets', 'frags', 'pickupCooldownSum', 'yaw', 'pitch'];
const CLASS_TYPE = ['grunt', 'ogre', 'fiend'];
const FX_RECORD = ['gunCamX', 'gunCamY', 'gunCamZ', 'gunFwdX', 'gunFwdY', 'gunFwdZ', 'gunRightX', 'gunRightY', 'gunRightZ', 'gunUpX', 'gunUpY', 'gunUpZ', 'gunTanHalf', 'gunAspect', 'glowR', 'glowG', 'glowB',
  'cosmeticDraws', 'mainQuads', 'markQuads', 'gunQuads', 'mainPosSum', 'mainAttrSum', 'mainColourSum', 'markPosSum', 'markColourSum', 'gunPosSum', 'gunColourSum', 'lights', 'lightSum',
  'recoilBack', 'recoilClimb', 'recoilRoll', 'recoilYaw', 'muzzleX', 'muzzleY', 'muzzleZ', 'mainKeySum', 'gunVisible', 'gunAttrSum',
  'uploadedLights', 'uploadedPosSum', 'uploadedColourSum'];   // N5.1: what Q3R.uploadLights sends to the shaders (the 16 best lights)
function tape(ticks) { return { ticks, data: new Float64Array(ticks * 3) }; }
function fill(t, from, to, mask, dx = 0, dy = 0) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) { t.data[i * 3] = mask; t.data[i * 3 + 1] = dx; t.data[i * 3 + 2] = dy; } }
function or(t, from, to, mask) { for (let i = Math.max(0, from); i < Math.min(t.ticks, to); i++) t.data[i * 3] |= mask; }
function look(t, at, dx, dy) { t.data[at * 3 + 1] += dx; t.data[at * 3 + 2] += dy; }

(async () => {
  const v = process.argv.slice(2); let out = null, reportFile = null, fxOut = null, framesArg = null, framesDir = null, viewsDir = null, width = 1280, height = 720;
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') out = v[++i]; else if (v[i] === '--report') reportFile = v[++i]; else if (v[i] === '--fx-out') fxOut = v[++i]; else if (v[i] === '--frames') framesArg = v[++i]; else if (v[i] === '--frames-dir') framesDir = v[++i]; else if (v[i] === '--views-dir') viewsDir = v[++i]; else if (v[i] === '--width') width = Number(v[++i]); else if (v[i] === '--height') height = Number(v[++i]); else throw new Error('unknown option ' + v[i]); }
  if (!out) throw new Error('usage: --out <file.bin> [--report file.json]');
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
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=helios-crucible`, { waitUntil: 'load', timeout: 900000 });
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: 900000, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true, null, { timeout: 900000, polling: 500 });
    // A firing lane: the spawn point and yaw with the longest unobstructed line at chest height.
    const world = await page.evaluate(() => {
      fovPanelOpen = true;
      let best = null;
      for (const s of WORLD.spawns) for (let k = 0; k < 48; k++) {
        const a = k * Math.PI / 24, cx = Math.cos(a), cy = Math.sin(a); let free = 0;
        for (let d = 1; d <= 34; d += .5) {
          const x = s.x + cx * d, y = s.y + cy * d, sup = stage4Support(x, y, s.z, .1);
          if (!sup || Math.abs(sup.z - s.z) > .02 || !stage4CapsuleClear(x, y, s.z, 2.4, .7) || worldShotCollisionT(s.x, s.y, s.z + 1.3, x, y, s.z + 1.3, .3) !== Infinity) break;
          free = d;
        }
        if (!best || free > best.free) best = { x: s.x, y: s.y, z: s.z, a, free, tag: s.tag };
      }
      const pk = worldPickupSpots().map(p => ({ x: p.x, y: p.y, z: p.z }));
      let pick = null;
      for (const p of pk) for (let k = 0; k < 16 && !pick; k++) {
        const a = k * Math.PI / 8, x = p.x - Math.cos(a) * 3, y = p.y - Math.sin(a) * 3, sup = stage4Support(x, y, p.z, .1);
        if (!sup || Math.abs(sup.z - p.z) > .02) continue;
        let ok = true; for (let d = 0; d <= 6 && ok; d += .25) ok = stage4CapsuleClear(x + Math.cos(a) * d, y + Math.sin(a) * d, p.z, 1.8, .3);
        if (ok) pick = { x, y, z: p.z, a, px: p.x, py: p.y };
      }
      return { map: window.VOID_ACTIVE_MAP, lane: best, pickups: pk.length, pickupLane: pick, weapons: WEAPONS.map(w => w.name) };
    });
    const L = world.lane; if (!L || L.free < 15) throw new Error('no firing lane found: ' + JSON.stringify(L));
    const at = (d, side = 0) => ({ x: L.x + Math.cos(L.a) * d - Math.sin(L.a) * side, y: L.y + Math.sin(L.a) * d + Math.cos(L.a) * side, z: L.z });
    const target = (d, side, cls, hp) => ({ ...at(d, side), cls, hp });
    const scenarios = [];
    const add = (name, t, opt) => scenarios.push({ name, tape: t, start: { x: L.x, y: L.y, z: L.z, a: L.a, pitch: 0, now: 100, hp: 100, wa: 0, wb: 1, seed: 1000 + scenarios.length * 7919, ...opt.start }, targets: opt.targets || [] });
    // 1. every weapon: sustained fire at three target classes, clip exhaustion, reload key, swap, second weapon
    for (let i = 0; i < 16; i++) {
      const t = tape(560); fill(t, 20, 320, FIRE); for (let k = 40; k < 320; k += 23) look(t, k, ((k / 23) % 2 ? 1 : -1) * 6, ((k / 23) % 3 - 1) * 3);
      or(t, 350, 353, RELOAD); or(t, 430, 433, SWAP); or(t, 470, 540, FIRE);
      add('weapon_' + String(i).padStart(2, '0') + '_' + world.weapons[i].toLowerCase().replace(/ /g, '_'), t,
        { start: { wa: i, wb: (i + 5) % 16 }, targets: [target(5, 0, 0, 6000), target(9, .9, 1, 6000), target(14, -.8, 2, 6000)] });
    }
    // 2. kills: weak targets, every kill rerolls the weapon in hand
    { const t = tape(900); fill(t, 10, 900, FIRE); for (let k = 30; k < 900; k += 17) look(t, k, ((k / 17) % 2 ? 1 : -1) * 9, 0);
      add('kills_and_reroll', t, { start: { wa: 0, wb: 11 }, targets: [target(4, 0, 0, 30), target(6, .7, 2, 30), target(7, -.9, 0, 45), target(9, .3, 1, 60), target(11, -.4, 0, 30), target(13, .8, 2, 45), target(15, 0, 1, 80)] }); }
    // 3. rocket jump and grenade jump: look straight down, jump and fire
    { const t = tape(700); for (let k = 5; k < 9; k++) look(t, k, 0, 90); or(t, 40, 43, JUMP | FIRE); or(t, 330, 333, SWAP); or(t, 400, 403, JUMP | FIRE);
      add('rocket_and_grenade_jump', t, { start: { wa: 4, wb: 2 } }); }
    // 4. rocket at a wall behind the player (horizontal knockback), moving
    { const t = tape(420); fill(t, 0, 420, W); for (let k = 2; k < 4; k++) look(t, k, 0, 60); or(t, 60, 63, FIRE); or(t, 200, 203, FIRE);
      add('rocket_at_feet_moving', t, { start: { wa: 4, wb: 10 } }); }
    // 5. splash falloff: a row of targets at increasing distance from the impact point
    for (const [name, w] of [['rocket', 4], ['grenade', 2], ['rift', 10]]) {
      const t = tape(520); or(t, 20, 23, FIRE); or(t, 180, 183, FIRE); or(t, 340, 343, FIRE);
      add('splash_falloff_' + name, t, { start: { wa: w, wb: 3 }, targets: [target(10, 0, 0, 4000), target(10.3, 1.3, 0, 4000), target(10.6, 2.4, 1, 4000), target(10.9, 3.6, 2, 4000), target(11.2, 4.9, 0, 4000), target(12.2, -1.1, 0, 4000), target(13.6, -2.2, 0, 4000)] });
    }
    // 6. grenade arcs at different pitches
    { const t = tape(900); for (let n = 0; n < 5; n++) { look(t, 10 + n * 170, 0, n === 0 ? -100 : 28); or(t, 20 + n * 170, 23 + n * 170, FIRE); }
      add('grenade_arcs', t, { start: { wa: 2, wb: 4 }, targets: [target(8, 0, 0, 4000), target(13, .5, 1, 4000)] }); }
    // 7. shotgun recoil: on the ground and in the air (double jump), all three shotguns
    for (const [name, w] of [['shotgun', 1], ['super_shotgun', 7], ['quad_shotgun', 15]]) {
      const t = tape(520); or(t, 20, 23, FIRE); or(t, 200, 203, JUMP); or(t, 230, 233, FIRE); fill(t, 360, 520, W); or(t, 400, 403, JUMP); or(t, 440, 443, FIRE);
      add('recoil_' + name, t, { start: { wa: w, wb: 0 }, targets: [target(6, 0, 0, 4000)] });
    }
    // 8. headshots: aim up at the head box, rail and hand cannon
    { const t = tape(420); look(t, 5, 0, -40); for (let n = 0; n < 4; n++) or(t, 30 + n * 130, 33 + n * 130, FIRE); or(t, 200, 203, SWAP);
      add('headshots_rail_handcannon', t, { start: { wa: 3, wb: 12 }, targets: [target(6, 0, 0, 2000), target(12, 0, 1, 2000)] }); }
    // 9. health pickup: damaged player walks over a medkit, twice (13 s cooldown)
    if (world.pickupLane) { const P = world.pickupLane, t = tape(2100); fill(t, 10, 80, W); fill(t, 200, 330, S); fill(t, 1700, 1830, W);
      add('health_pickup', t, { start: { x: P.x, y: P.y, z: P.z, a: P.a, hp: 37, wa: 0, wb: 1 } }); }
    // 10. firing while moving, crouching and sliding (shot origin follows the eye)
    { const t = tape(700); fill(t, 10, 700, W | FIRE); or(t, 150, 300, CROUCH); or(t, 320, 520, SPRINT); or(t, 420, 423, SLIDE); for (let k = 30; k < 700; k += 31) look(t, k, ((k / 31) % 2 ? 1 : -1) * 14, ((k / 31) % 3 - 1) * 4);
      add('fire_while_moving', t, { start: { wa: 5, wb: 13 }, targets: [target(9, 0, 0, 9000), target(14, 1, 1, 9000), target(L.free - 1, -.5, 2, 9000)] }); }

    const wanted = new Map();   // scenario -> ticks to photograph
    if (framesArg) { if (!fxOut || !framesDir) throw new Error('--frames needs --fx-out and --frames-dir'); for (const part of framesArg.split(';')) { const [name, list] = part.split(':'); wanted.set(name, list.split(',').map(Number)); } fs.mkdirSync(framesDir, { recursive: true }); }
    if (framesArg || viewsDir) await page.evaluate(() => { setHudVisibility(false); Q3R.S.flares = false; window.__SHOWCASE_HIDE_FP = false;
      window.__n5Grab = () => { const game = Math.random; Math.random = () => .5; try { render(); } finally { Math.random = game; }
        const w = canvas.width, h = canvas.height, px = new Uint8Array(w * h * 4); gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
        const c = document.createElement('canvas'); c.width = w; c.height = h; const cx = c.getContext('2d'), img = cx.createImageData(w, h);
        for (let y = 0; y < h; y++) img.data.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
        for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255; cx.putImageData(img, 0, 0); return c.toDataURL('image/png'); }; });
    const results = [];
    for (const sc of scenarios) {
      const res = await page.evaluate(([sc, tapeData, KEYS, REC, CLASS_TYPE, FX, FXREC, shotTicks]) => {
        const pictures = [];
        fovPanelOpen = false;
        // --- determinism: seeded Math.random, cosmetic functions silenced, effect entry points recorded ---
        const realRandom = Math.random; let seed = sc.start.seed | 0, rngCalls = 0;
        Math.random = () => { rngCalls++; seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
        const saved = { playGunSound, playExplosionSound, playEnemyVoice, enemyDeathFx, vaCelebrateFrag, playBodySound, beep, fire: WFX.fire, explosion: WFX.explosion, blood: WFX.blood, impact: WFX.impact, addLight: Q3R.addLight };
        // cosmetic stream: every random number drawn inside the effect code comes from here
        let cseed = sc.cosmeticSeed | 0, cosCalls = 0;
        const cosRandom = () => { cosCalls++; cseed |= 0; cseed = cseed + 0x6D2B79F5 | 0; let t = Math.imul(cseed ^ cseed >>> 15, 1 | cseed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
        const cosmetic = f => { const game = Math.random; Math.random = cosRandom; try { return f(); } finally { Math.random = game; } };
        let lights = [];
        const fxOutRec = FX ? new Array((tapeData.length / 3) * FXREC).fill(0) : null;
        playGunSound = playExplosionSound = playEnemyVoice = enemyDeathFx = vaCelebrateFrag = playBodySound = beep = () => {};
        let shots = [], blasts = [];
        WFX.fire = (index, list, eye) => { for (const s of list) shots.push(s); if (FX) cosmetic(() => saved.fire(index, list, eye)); };
        WFX.explosion = (x, y, z, power, kind) => { blasts.push([x, y, z, power]); if (FX) cosmetic(() => saved.explosion(x, y, z, power, kind)); };
        if (FX) { Q3R.addLight = (x, y, z, radius, r, g, b) => { if (lights.length < 64) lights.push(x + y + z + radius + r + g + b); saved.addLight(x, y, z, radius, r, g, b); };
          WFX.clear(); Object.assign(WFX.recoil, { back: 0, vb: 0, climb: 0, vc: 0, roll: 0, vr: 0, yaw: 0, vy: 0, k: 200 }); Object.assign(WFX.gun, { flash: null, switchT: 9, lastIndex: -1, railCharge: 0, bolt: null, boltFx: 0, glow: [0, 0, 0] }); }
        WFX.blood = WFX.impact = () => {};
        try {
          for (const code of Object.keys(keys)) if (keys[code]) window.dispatchEvent(new KeyboardEvent('keyup', { code, bubbles: true }));
          keys = {}; jumpHeld = false; fireHeld = false; touchStick = { x: 0, y: 0 };
          bots.length = 0; enemyShots.length = 0; rockets.length = 0; grenades.length = 0; energyBolts.length = 0; bullets.length = 0;
          for (const t of sc.targets) bots.push({ x: t.x, y: t.y, z: t.z, vx: 0, vy: 0, hp: t.hp, maxHp: t.hp, type: CLASS_TYPE[t.cls], flying: false, hitFlash: 0, uid: 9000 + bots.length });
          pickups = worldPickupSpots();
          Object.assign(player, { x: sc.start.x, y: sc.start.y, z: sc.start.z, a: sc.start.a, pitch: sc.start.pitch, vx: 0, vy: 0, vz: 0, ground: true, crouchOffset: 0, downed: false, hp: sc.start.hp });
          slideTime = 0; slideCooldown = 0; slideSpeed = 0; slideDirX = 0; slideDirY = 0; slideBuffer = 0; jumpBuffer = 0; coyoteTime = 0; movementSlopeAngle = 0; airStrafeGained = 0;
          moveFovScale = 1; worldPadCooldown = 0; q3Reset(); now = sc.start.now; respawnGrace = 0; burnRemaining = 0; flash = 0; score = 0;
          loadout = [sc.start.wa, sc.start.wb]; slot = 0; mag = [WEAPONS[sc.start.wa].clip, WEAPONS[sc.start.wb].clip]; reloading = 0; cooldown = 0; gunKick = 0; muzzleFlashT = 0;
          const n = tapeData.length / 3, out = new Array(n * REC).fill(0), dt = 1 / 120;
          let prev = 0;
          for (let i = 0; i < n; i++) {
            shots = []; blasts = [];
            const mask = tapeData[i * 3], dx = tapeData[i * 3 + 1], dy = tapeData[i * 3 + 2];
            for (let b = 0; b < KEYS.length; b++) { const bit = 1 << b; if (KEYS[b] && ((mask ^ prev) & bit)) window.dispatchEvent(new KeyboardEvent(mask & bit ? 'keydown' : 'keyup', { code: KEYS[b], bubbles: true })); }
            if ((mask ^ prev) & 256) { if (mask & 256) { fireHeld = true; fireWeapon(); } else fireHeld = false; }   // mousedown / mouseup handlers
            prev = mask;
            if (dx || dy) aim(dx, dy);
            // ---- legacy update(dt), original order ----
            now += dt; respawnGrace = Math.max(0, respawnGrace - dt); flash = Math.max(0, flash - dt * 2); gunKick = Math.max(0, gunKick - dt * 6); muzzleFlashT = Math.max(0, muzzleFlashT - dt);
            const f = (keys.KeyW ? 1 : 0) - (keys.KeyS ? 1 : 0) - touchStick.y, s = (keys.KeyD ? 1 : 0) - (keys.KeyA ? 1 : 0) + touchStick.x;
            const m = Math.hypot(f, s), mf = m ? f / m : 0, ms = m ? s / m : 0;
            const ca = Math.cos(player.a), sa = Math.sin(player.a), wx = ca * mf - sa * ms, wy = sa * mf + ca * ms;
            moveMarine(dt, wx, wy, m); stage4Movement(dt); worldTriggerUpdate(dt); q3PostMove(dt);
            if (fireHeld) fireWeapon(); if (reloading > 0) { reloading -= dt; if (reloading <= 0) { reloading = 0; mag[slot] = WEAPONS[loadout[slot]].clip; } }
            for (let i = rockets.length - 1; i >= 0; i--) { const p = rockets[i]; if (!p) continue; p.life -= dt;
              const hit = shotSweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, .14);
              if (hit.hit || p.life <= 0) { explode(hit.x, hit.y, 1.15, hit.z, 'fire', p.ownerId || null); rockets.splice(i, 1); } else { p.x = hit.x; p.y = hit.y; p.z = hit.z; } }
            for (let i = grenades.length - 1; i >= 0; i--) { const p = grenades[i]; if (!p) continue; p.life -= dt; p.vz -= 15 * dt;
              const hit = shotSweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, .15);
              if (hit.hit || p.life <= 0) { explode(hit.x, hit.y, 1.08, hit.z, 'fire', p.ownerId || null); grenades.splice(i, 1); } else { p.x = hit.x; p.y = hit.y; p.z = hit.z; } }
            for (let i = energyBolts.length - 1; i >= 0; i--) { const p = energyBolts[i]; if (!p) continue; p.life -= dt;
              const hit = shotSweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, p.kind === 'rift' ? .23 : .14);
              if (hit.hit || p.life <= 0) { if (p.kind === 'rift') explode(hit.x, hit.y, 1.42, hit.z, 'rift', p.ownerId || null); energyBolts.splice(i, 1); } else { p.x = hit.x; p.y = hit.y; p.z = hit.z; } }
            for (let p of pickups) { if (p.cool > 0) p.cool -= dt; else if (Math.hypot(player.x - p.x, player.y - p.y) < .85 && netPickupsOnSameDeck(player, p) && !player.downed && player.hp < 100) { player.hp = Math.min(100, player.hp + 40); p.cool = 13; } }
            if (FX) cosmetic(() => WFX.update(dt));   // update(): after the pickup and particle loops
            // ---- record ----
            const p = player, o = i * REC;
            out[o] = p.x; out[o + 1] = p.y; out[o + 2] = p.z; out[o + 3] = p.vx; out[o + 4] = p.vy; out[o + 5] = p.vz; out[o + 6] = p.ground ? 1 : 0; out[o + 7] = p.hp;
            out[o + 8] = slot; out[o + 9] = loadout[0]; out[o + 10] = loadout[1]; out[o + 11] = mag[0]; out[o + 12] = mag[1]; out[o + 13] = cooldown; out[o + 14] = reloading; out[o + 15] = gunKick; out[o + 16] = muzzleFlashT; out[o + 17] = rngCalls;
            out[o + 18] = rockets.length; out[o + 19] = grenades.length; out[o + 20] = energyBolts.length;
            for (const q of [...rockets, ...grenades, ...energyBolts]) { out[o + 21] += q.x; out[o + 22] += q.y; out[o + 23] += q.z; }
            out[o + 24] = shots.length; for (const s of shots) { if (s.hit) out[o + 25]++; if (s.enemy) out[o + 26]++; out[o + 27] += s.x; out[o + 28] += s.y; out[o + 29] += s.z; }
            out[o + 30] = blasts.length; if (blasts.length) { const b = blasts[blasts.length - 1]; out[o + 31] = b[0]; out[o + 32] = b[1]; out[o + 33] = b[2]; out[o + 34] = b[3]; }
            for (let k = 0; k < 8; k++) out[o + 35 + k] = bots[k] ? bots[k].hp : 0;
            out[o + 43] = bots.length; out[o + 44] = score; for (const q of pickups) out[o + 45] += Math.max(0, q.cool); out[o + 46] = p.a; out[o + 47] = p.pitch;
            if (p.hp <= 0) throw new Error('player died in ' + sc.name + ' at tick ' + i);
            if (FX) cosmetic(() => {   // one rendered frame's effect build per tick, as render() does it
              const f = fxOutRec, q = i * FXREC, camera = viewCamera(), aspect = canvas.width / canvas.height;
              const fxCam = { x: camera.x, y: camera.y, z: camera.z, forward: camera.forward, right: camera.right, up: camera.up, tanHalf: Math.tan((horizontalFov * moveFovScale) * Math.PI / 360) / aspect, aspect };
              lights = []; FXR.resetStats(); FXR.begin(fxCam, ['mark', 'sky', 'main', 'flare']); Q3R.clearLights();
              WFX.build(fxCam);
              for (const k of pickups) if (k.cool <= 0) FXR.sprite(FXR.B.main, k.x, k.y, netPickupHeight(k) + .5, 1.3, 1.3, 0, FX_TILE.GLOW, .45, 1, .55, .22 + .08 * Math.sin(now * 3 + k.x), 1);
              const sums = (b, o0, o1, o2) => { const d = b.data; let a = 0, c = 0, e = 0; for (let j = 0; j < b.n * 48; j += 12) { a += d[j] + d[j + 1] + d[j + 2]; c += d[j + 3] + d[j + 4] + d[j + 5] + d[j + 6]; e += d[j + 7] + d[j + 8] + d[j + 9] + d[j + 10] + d[j + 11]; } f[q + o0] = a; if (o1 >= 0) f[q + o1] = c; f[q + o2] = e; };
              const main = FXR.B.main, mark = FXR.B.mark;
              f[q + 18] = main.n; f[q + 19] = mark.n; sums(main, 21, 22, 23); sums(mark, 24, -1, 25);
              for (let j = 0; j < main.n; j++) f[q + 37] += main.keys[j];
              f[q + 28] = lights.length; for (const l of lights) f[q + 29] += l;
              const m = WFX.muzzleWorld(fxCam); f[q + 34] = m.x; f[q + 35] = m.y; f[q + 36] = m.z;
              // render(): Q3R.uploadLights(camera) keeps the 16 lights that matter most to this view
              Q3R.uploadLights(camera.x, camera.y, camera.z); { const v = Q3R.lightData(); f[q + 40] = v.count; for (let j = 0; j < v.count * 4; j += 4) { f[q + 41] += v.pos[j] + v.pos[j + 1] + v.pos[j + 2] + v.pos[j + 3]; f[q + 42] += v.col[j] + v.col[j + 1] + v.col[j + 2]; } }
              // renderGun3D: capture the camera and glow uniforms it sets
              const u3 = gl.uniform3f, u1 = gl.uniform1f; let glowSeen = false;
              gl.uniform3f = function (loc, a, b, c) { if (loc === uni.camPos) { f[q] = a; f[q + 1] = b; f[q + 2] = c; f[q + 38] = 1; } else if (loc === uni.camForward) { f[q + 3] = a; f[q + 4] = b; f[q + 5] = c; }
                else if (loc === uni.camRight) { f[q + 6] = a; f[q + 7] = b; f[q + 8] = c; } else if (loc === uni.camUp) { f[q + 9] = a; f[q + 10] = b; f[q + 11] = c; }
                else if (loc === Q3R.loc.gunGlow && !glowSeen) { glowSeen = true; f[q + 14] = a; f[q + 15] = b; f[q + 16] = c; } return u3.call(gl, loc, a, b, c); };
              gl.uniform1f = function (loc, a) { if (loc === uni.tanHalf) f[q + 12] = a; else if (loc === uni.aspect) f[q + 13] = a; return u1.call(gl, loc, a); };
              // nothing needs to reach the GPU: the draw / clear / upload calls of renderGun3D are skipped (state setters still run)
              gl.drawArrays = gl.drawElements = gl.clear = gl.bufferSubData = () => {};
              try { renderGun3D(); } finally { delete gl.uniform3f; delete gl.uniform1f; delete gl.drawArrays; delete gl.drawElements; delete gl.clear; delete gl.bufferSubData; }
              const gun = FXR.B.gun; f[q + 20] = gun.n; sums(gun, 26, 39, 27);
              f[q + 30] = WFX.recoil.back; f[q + 31] = WFX.recoil.climb; f[q + 32] = WFX.recoil.roll; f[q + 33] = WFX.recoil.yaw;
            });
            if (FX) fxOutRec[i * FXREC + 17] = cosCalls;
            if (shotTicks.includes(i)) pictures.push([i, window.__n5Grab()]);
          }
          for (let b = 0; b < KEYS.length; b++) if (KEYS[b] && (prev & (1 << b))) window.dispatchEvent(new KeyboardEvent('keyup', { code: KEYS[b], bubbles: true }));
          fireHeld = false;
          return { pictures, records: out, fx: fxOutRec, fxFinite: !fxOutRec || fxOutRec.every(Number.isFinite), view: { fov: horizontalFov, aspect: canvas.width / canvas.height }, cosCalls, rngCalls, finite: out.every(Number.isFinite), frags: score, hpEnd: player.hp };
        } finally {
          Math.random = realRandom; ({ playGunSound, playExplosionSound, playEnemyVoice, enemyDeathFx, vaCelebrateFrag, playBodySound, beep } = saved);
          WFX.fire = saved.fire; WFX.explosion = saved.explosion; WFX.blood = saved.blood; WFX.impact = saved.impact; Q3R.addLight = saved.addLight; if (FX) WFX.clear();
          bots.length = 0; rockets.length = 0; grenades.length = 0; energyBolts.length = 0; fovPanelOpen = true;
        }
      }, [{ ...sc, cosmeticSeed: (sc.start.seed * 31 + 17) | 0 }, Array.from(sc.tape.data), KEYS, RECORD.length, CLASS_TYPE, !!fxOut, FX_RECORD.length, wanted.get(sc.name) || []]);
      for (const [tick, url] of res.pictures) { const file = path.join(framesDir, `${sc.name}_${String(tick).padStart(5, '0')}.png`); fs.writeFileSync(file, Buffer.from(url.split(',')[1], 'base64')); process.stdout.write(`picture ${file}\n`); }
      if (!res.finite || !res.fxFinite) throw new Error('non-finite record in ' + sc.name);
      const col = k => { const a = []; for (let i = k; i < res.records.length; i += RECORD.length) a.push(res.records[i]); return a; };
      const sum = k => col(k).reduce((a, b) => a + b, 0);
      res.summary = { shots: sum(24), shotHits: sum(25), enemyHits: sum(26), explosions: sum(30), frags: res.frags, rngCalls: res.rngCalls, hpMin: Math.min(...col(7)), hpEnd: res.hpEnd, zMax: Math.max(...col(2)), maxProjectiles: Math.max(...col(18).map((v, i) => v + col(19)[i] + col(20)[i])) };
      results.push({ sc, res });
      process.stdout.write(`${sc.name}: ${sc.tape.ticks} ticks ${JSON.stringify(res.summary)}\n`);
    }
    if (viewsDir) {   // every weapon at rest in the firing lane
      fs.mkdirSync(viewsDir, { recursive: true });
      for (let i = 0; i < 16; i++) {
        const url = await page.evaluate(([L, i]) => {
          fovPanelOpen = true; bots.length = 0; enemyShots.length = 0; rockets.length = 0; grenades.length = 0; energyBolts.length = 0; particles.length = 0; pickups = worldPickupSpots();
          Object.assign(player, { x: L.x, y: L.y, z: L.z, a: L.a, pitch: 0, vx: 0, vy: 0, vz: 0, ground: true, crouchOffset: 0, downed: false, hp: 100 });
          moveFovScale = 1; q3Reset(); now = 100; flash = 0; loadout = [i, (i + 5) % 16]; slot = 0; mag = [WEAPONS[i].clip, WEAPONS[(i + 5) % 16].clip]; reloading = 0; cooldown = 0; gunKick = 0; muzzleFlashT = 0;
          WFX.clear(); Object.assign(WFX.recoil, { back: 0, vb: 0, climb: 0, vc: 0, roll: 0, vr: 0, yaw: 0, vy: 0, k: 200 }); Object.assign(WFX.gun, { flash: null, switchT: 9, lastIndex: -1, railCharge: 0, bolt: null, boltFx: 0, glow: [0, 0, 0] });
          return window.__n5Grab();
        }, [L, i]);
        const file = path.join(viewsDir, `view_${String(i).padStart(2, '0')}.png`); fs.writeFileSync(file, Buffer.from(url.split(',')[1], 'base64'));
      }
      fs.writeFileSync(path.join(viewsDir, 'views.json'), JSON.stringify({ tool: 'tools/n5/legacy_combat.cjs --views-dir', pose: [L.x, L.y, L.z, L.a, 0], time: 100, size: [width, height], note: 'railCharge 0 as at game start (the railgun shows its recharge glow), HUD hidden, flares off' }, null, 1) + '\n');
      process.stdout.write(`wrote 16 weapon views to ${viewsDir}; lane pose ${L.x},${L.y},${L.z},${L.a},0\n`);
    }
    if (errors.length) throw new Error('page errors: ' + errors.join(' | '));
    const chunks = []; const u32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); chunks.push(b); }; const f64s = a => chunks.push(Buffer.from(new Float64Array(a).buffer));
    chunks.push(Buffer.from('CATCMBT1')); u32(1); u32(results.length); u32(3); u32(RECORD.length);
    for (const { sc, res } of results) {
      const name = Buffer.from(sc.name); u32(name.length); chunks.push(name); u32(sc.tape.ticks);
      const s = sc.start; f64s([s.x, s.y, s.z, s.a, s.pitch, s.now, s.hp, s.wa, s.wb, s.seed, sc.targets.length, 0]);
      f64s(sc.targets.flatMap(t => [t.x, t.y, t.z, t.cls, t.hp])); f64s(sc.tape.data); f64s(res.records);
    }
    const blob = Buffer.concat(chunks); fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true }); fs.writeFileSync(out, blob);
    if (fxOut) {
      const c2 = []; const w32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); c2.push(b); }; const w64 = a => c2.push(Buffer.from(new Float64Array(a).buffer));
      c2.push(Buffer.from('CATWFX01')); w32(2); w32(results.length); w32(FX_RECORD.length); w64([results[0].res.view.fov, results[0].res.view.aspect]);
      for (const { sc, res } of results) { const name = Buffer.from(sc.name); w32(name.length); c2.push(name); w32(sc.tape.ticks); w64([(sc.start.seed * 31 + 17) | 0]); w64(res.fx); }
      const fxBlob = Buffer.concat(c2); fs.writeFileSync(fxOut, fxBlob);
      process.stdout.write(`wrote ${fxOut}: ${fxBlob.length} bytes, sha256 ${crypto.createHash('sha256').update(fxBlob).digest('hex')}; cosmetic draws ${results.reduce((a, r) => a + r.res.cosCalls, 0)}\n`);
    }
    const report = { tool: 'tools/n5/legacy_combat.cjs', source: 'legacy/index.html', sha256: sha, map: world.map, tickSeconds: '1/120', keyBits: KEYS, record: RECORD, lane: world.lane, pickups: world.pickups,
      fixture: { file: path.basename(out), bytes: blob.length, sha256: crypto.createHash('sha256').update(blob).digest('hex') },
      runtimeStateChanges: ['solo game started, simulation held with fovPanelOpen between scenarios', 'Math.random replaced by seeded mulberry32 during a scenario',
        'no-op: playGunSound, playExplosionSound, playEnemyVoice, enemyDeathFx, vaCelebrateFrag, playBodySound, beep, WFX.blood, WFX.impact', 'recorders: WFX.fire (shot end points), WFX.explosion (detonations)',
        'bots replaced by static target objects; pickups reset with worldPickupSpots(); player, loadout, magazines and timers set per scenario', 'update(dt) parts executed by the tool in legacy order; AI, enemy shots, waves, burn and respawn not executed'],
      scenarios: results.map(({ sc, res }) => ({ name: sc.name, ticks: sc.tape.ticks, start: sc.start, targets: sc.targets.length, ...res.summary })) };
    if (reportFile) fs.writeFileSync(reportFile, JSON.stringify(report, null, 1) + '\n');
    process.stdout.write(`wrote ${out}: ${results.length} scenarios, ${blob.length} bytes\n`);
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
