#!/usr/bin/env node
// N6 enemy presentation assets, taken from the UNMODIFIED legacy/index.html (hash-checked) running in
// headless Chromium. Nothing is simulated; the tool reads data the page builds at load:
//   * makeEnemyTemplate(type) for the 20 ENEMY_TYPES: the instanced creature meshes exactly as the
//     legacy uploads them (12 floats per vertex; the fraction of the material id is the limb tag
//     written by meshEnemyData) and ENEMY_RIGS[type], the skeleton enemyRigFromMesh measured from them
//       -> assets/n6/enemies/NN_name.emesh
//     The three textures of a type (base colour, metal/roughness, normal) are the JPEG files of the
//     N1 bake (assets/n1/models/enemies/name-texture-{0,1,2}.jpg); the tool checks that they are
//     byte-identical to the data: URLs of MESHY_ENEMIES and copies nothing.
//   * buildMarineCat() + MARINE_RIG: the Marine Cat as the legacy draws it -> assets/n6/marine.emesh.
//     The mesh is compared with assets/n1/models/character/cat_marine_rigged.glb (the GLB the legacy
//     comment names as its source): every runtime vertex is matched to a GLB vertex, and the GLB skin
//     (JOINTS_0 / WEIGHTS_0) gives each vertex its dominant joint and hit part. The seven GLB clips
//     are listed in the report only: the legacy runtime never samples them (see docs/N6_ENEMIES.md).
//   * the 624 x 234 light-volume atlas bakeStaticLightmap() uploads for each map (captured from the
//     texImage2D call, rows in the order the texture stores them) -> assets/n6/light-volume/<map>.png
//   * tables: ENEMY_CLASS, ENEMY_ATTACK_ANIM, GAIT_STRIDE, ENEMY_DEATH, EFX_TYPE, MARINE_RIG,
//     CAT_MARINE_REFERENCE, cat_marine_hitboxes.json -> native/include/cat/generated/enemy_tables.inc
//   node tools/n6/legacy_enemy_assets.cjs
// .emesh layout: "CATENM01", u32 vertexCount, u32 flags (1 = per-vertex joint/part bytes follow),
//   f32 rig[16] {hip, knee, shoulder, elbow, neck, spine, wingRoot, height, shoulderX, wingTip, depth,
//   width, 0, 0, 0, 0}, f32[vertexCount*12], then (flags & 1) u8 joint[vertexCount], u8 part[vertexCount].
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto'), zlib = require('zlib');
const { chromium } = require('playwright');
const sha256 = b => crypto.createHash('sha256').update(b).digest('hex');
function png(width, height, rgba) {
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0); return Buffer.concat([len, body, crc]); };
  const head = Buffer.alloc(13); head.writeUInt32BE(width, 0); head.writeUInt32BE(height, 4); head[8] = 8; head[9] = 6;
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) rgba.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', head), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
const num = v => { const s = String(v); return /[.e]/.test(s) || !Number.isFinite(v) ? s : s + '.0'; };
const MAPS = ['helios-crucible', 'bastion-keep', 'reactor-bastion', 'q3-gallery'];
const RIG_KEYS = ['hip', 'knee', 'shoulder', 'elbow', 'neck', 'spine', 'wingRoot', 'height', 'shoulderX', 'wingTip', 'depth', 'width'];

// ---- minimal GLB reader (positions, joints, weights, node hierarchy) --------------------------------
function readGlb(file) {
  const f = fs.readFileSync(file);
  if (f.readUInt32LE(0) !== 0x46546c67) throw new Error('not a GLB');
  const jl = f.readUInt32LE(12), json = JSON.parse(f.slice(20, 20 + jl).toString());
  const bin = f.slice(20 + jl + 8);
  const comp = { 5120: [1, (b, o) => b.readInt8(o)], 5121: [1, (b, o) => b.readUInt8(o)], 5122: [2, (b, o) => b.readInt16LE(o)], 5123: [2, (b, o) => b.readUInt16LE(o)], 5125: [4, (b, o) => b.readUInt32LE(o)], 5126: [4, (b, o) => b.readFloatLE(o)] };
  const width = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
  const accessor = i => {
    const a = json.accessors[i], v = json.bufferViews[a.bufferView], [size, read] = comp[a.componentType], n = width[a.type];
    const stride = v.byteStride || size * n, base = (v.byteOffset || 0) + (a.byteOffset || 0), out = [];
    for (let k = 0; k < a.count; k++) { const row = []; for (let c = 0; c < n; c++) row.push(read(bin, base + k * stride + c * size)); out.push(n === 1 ? row[0] : row); }
    return out;
  };
  return { json, accessor };
}

(async () => {
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = sha256(bytes);
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => { const url = new URL(req.url, 'http://localhost'); if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  const outDir = path.join(root, 'assets/n6');
  fs.mkdirSync(path.join(outDir, 'enemies'), { recursive: true }); fs.mkdirSync(path.join(outDir, 'light-volume'), { recursive: true });
  const files = [];
  const put = (file, buf) => { fs.writeFileSync(path.join(outDir, file), buf); files.push({ file, bytes: buf.length, sha256: sha256(buf) }); };
  try {
    let data = null; const volumes = [];
    for (const map of MAPS) {
      const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
      page.setDefaultTimeout(900000);
      // Capture the light-volume upload: the only 624 x 234 RGBA texImage2D call of the page.
      await page.addInitScript(() => {
        const original = WebGL2RenderingContext.prototype.texImage2D;
        WebGL2RenderingContext.prototype.texImage2D = function (...a) {
          if (a.length === 9 && a[3] === 624 && a[4] === 234 && a[8] && a[8].length === 624 * 234 * 4) { window.__lightVolume = new Uint8Array(a[8]); window.__lightVolumeUploads = (window.__lightVolumeUploads || 0) + 1; window.__lightVolumeFlipY = !!this.getParameter(this.UNPACK_FLIP_Y_WEBGL); }
          return original.apply(this, a);
        };
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${map}`, { waitUntil: 'load', timeout: 900000 });
      await page.waitForFunction(() => typeof WORLD !== 'undefined' && typeof enemyMeshes !== 'undefined', null, { timeout: 900000 });
      const have = await page.evaluate(() => !!window.__lightVolume);
      if (!have) { await page.evaluate(() => document.getElementById('start').click()); await page.waitForFunction(() => !!window.__lightVolume, null, { timeout: 900000 }); }
      const got = await page.evaluate(first => {
        const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); };
        const out = { volume: b64(window.__lightVolume), uploads: window.__lightVolumeUploads, flipY: window.__lightVolumeFlipY, info: staticLightmapInfo, mapId: window.VOID_MAP_ID, sunAzimuth: (WORLD.meta.sky || {}).sunAzimuth ?? null, fog: typeof fogColor !== 'undefined' ? fogColor : null };
        if (!first) return out;
        const f32 = a => b64(new Uint8Array(a.buffer, a.byteOffset, a.byteLength));
        out.types = ENEMY_TYPES.map(type => {
          const mesh = makeEnemyTemplate(type), rig = ENEMY_RIGS[type];
          return { type, mesh: f32(mesh), uploaded: enemyMeshes[type].count, rig: { ...rig, u0: [...rig.u0], u1: [...rig.u1], u2: [...rig.u2] }, textures: MESHY_ENEMIES[type].textures, bounds: MESHY_ENEMIES[type].bounds, triangles: MESHY_ENEMIES[type].triangles,
            stats: window.VOID_RIG_STATS[type], efx: EFX_TYPE[type] || null, walkStyle: ENEMY_WALK_STYLE[type] };
        });
        const marine = buildMarineCat();
        out.marine = { mesh: f32(marine), uploaded: marineCatMesh.count, rig: { ...MARINE_RIG, u0: [...MARINE_RIG.u0], u1: [...MARINE_RIG.u1], u2: [...MARINE_RIG.u2] }, reference: CAT_MARINE_REFERENCE };
        out.gait = { ...GAIT_STRIDE }; out.death = { ...ENEMY_DEATH }; out.kindDefault = { ...EFX_KIND_DEFAULT };
        out.settings = { entityGrid: Q3R.S.entityGrid, shadows: Q3R.S.shadows, gibs: Q3R.S.gibs, blood: Q3R.S.blood, overbright: Q3R.S.overbright, dynamicLights: Q3R.S.dynamicLights, effects: Q3R.S.effects };
        out.material = { ...MATERIAL };
        return out;
      }, data === null);
      if (data === null) data = got;
      const rgba = Buffer.from(got.volume, 'base64');
      if (rgba.length !== 624 * 234 * 4) throw new Error('unexpected light volume for ' + map);
      // The PNG holds the texture as WebGL stores it, first row = t 0: UNPACK_FLIP_Y_WEBGL also applies to
      // typed-array uploads, so when the flag is set at the moment of the upload the rows are reversed here.
      const stored = Buffer.alloc(rgba.length);
      for (let y = 0; y < 234; y++) rgba.copy(stored, y * 624 * 4, (got.flipY ? 233 - y : y) * 624 * 4, (got.flipY ? 234 - y : y + 1) * 624 * 4);
      put(`light-volume/${map}.png`, png(624, 234, stored));
      volumes.push({ map, uploads: got.uploads, unpackFlipY: got.flipY, info: got.info, sunAzimuth: got.sunAzimuth, fog: got.fog });
      await page.close();
    }
    // ---- creature meshes ----
    const typeRows = [];
    data.types.forEach((t, i) => {
      const v = Buffer.from(t.mesh, 'base64');
      if (v.length % 48 || v.length / 48 !== t.uploaded || t.uploaded !== t.triangles * 3) throw new Error('unexpected mesh for ' + t.type);
      const head = Buffer.alloc(16 + 64); head.write('CATENM01'); head.writeUInt32LE(v.length / 48, 8); head.writeUInt32LE(0, 12);
      RIG_KEYS.forEach((k, j) => head.writeFloatLE(t.rig[k], 16 + j * 4));
      const stem = String(i).padStart(2, '0') + '_' + t.type;
      put('enemies/' + stem + '.emesh', Buffer.concat([head, v]));
      const textures = t.textures.map((url, k) => {
        const m = /^data:image\/jpeg;base64,(.*)$/.exec(url); if (!m) throw new Error('texture is not a JPEG data URL: ' + t.type);
        const rel = `assets/n1/models/enemies/${t.type}-texture-${k}.jpg`, want = Buffer.from(m[1], 'base64'), have = fs.readFileSync(path.join(root, rel));
        if (!want.equals(have)) throw new Error(rel + ' differs from the legacy data URL');
        return { file: rel, sha256: sha256(have), bytes: have.length };
      });
      const f = new Float32Array(v.buffer, v.byteOffset, v.length / 4); const mats = new Set(); for (let k = 11; k < f.length; k += 12) mats.add(Math.round(f[k] * 100) / 100);
      typeRows.push({ index: i, type: t.type, stem, triangles: t.triangles, bounds: t.bounds, limbTriangles: t.stats, class: t.rig.cls, attack: t.rig.attack, rig: Object.fromEntries(RIG_KEYS.map(k => [k, t.rig[k]])), materials: [...mats].sort((a, b) => a - b), textures });
    });
    // ---- Marine Cat: runtime mesh vs GLB ----
    const mv = Buffer.from(data.marine.mesh, 'base64'), mcount = mv.length / 48;
    if (mv.length % 48 || mcount !== data.marine.uploaded) throw new Error('unexpected marine mesh');
    const mf = new Float32Array(mv.buffer, mv.byteOffset, mv.length / 4);
    const glbFile = 'assets/n1/models/character/cat_marine_rigged.glb', glb = readGlb(path.join(root, glbFile)), G = glb.json;
    const hit = JSON.parse(fs.readFileSync(path.join(root, 'assets/n1/models/character/cat_marine_hitboxes.json'), 'utf8'));
    // GLB (+Y up, +Z forward, +X left of the viewer) -> game (+Z up, +Y forward): (x, y, z) -> (x, z, y).
    const toGame = p => [p[0], p[2], p[1]];
    const gverts = [];
    let glbTriangles = 0;
    for (const prim of G.meshes[0].primitives) {
      const P = glb.accessor(prim.attributes.POSITION), J = glb.accessor(prim.attributes.JOINTS_0), W = glb.accessor(prim.attributes.WEIGHTS_0), I = glb.accessor(prim.indices);
      glbTriangles += I.length / 3;
      for (const i of I) { let best = 0; for (let c = 1; c < 4; c++) if (W[i][c] > W[i][best]) best = c; gverts.push({ p: P[i], joint: J[i][best], material: prim.material, weights: W[i].filter(w => w > 0).length }); }
    }
    // joint rest positions (accumulated node translations; the rig has no rest rotations or scales to honour beyond what is checked here)
    const parent = new Map(); G.nodes.forEach((n, i) => (n.children || []).forEach(c => parent.set(c, i)));
    const rest = i => { let p = [0, 0, 0]; for (let k = i; k !== undefined; k = parent.get(k)) { const n = G.nodes[k]; if (n.rotation && n.rotation.some((r, c) => Math.abs(r - (c === 3 ? 1 : 0)) > 1e-6)) throw new Error('GLB rest rotation not supported'); const t = n.translation || [0, 0, 0]; p = [p[0] + t[0], p[1] + t[1], p[2] + t[2]]; } return p; };
    const joints = G.skins[0].joints.map((node, j) => ({ index: j, name: G.nodes[node].name, rest: rest(node) }));
    const partOfBone = {}; const parts = [];
    for (const b of hit.boxes) { parts.push({ name: b.name, shape: 'box', bones: [b.bone, b.bone], center: b.centerLocal, size: b.size, radius: 0, multiplier: b.damageMultiplier }); }
    for (const c of hit.capsules) { parts.push({ name: c.name, shape: 'capsule', bones: [c.fromBone, c.toBone], center: [0, 0, 0], size: [0, c.height, 0], radius: c.radius, multiplier: c.damageMultiplier }); }
    const partIndex = name => parts.findIndex(p => p.name === name);
    const boneNames = joints.map(j => j.name);
    for (const n of boneNames) {   // skin joint -> hit part
      const side = /_L$/.test(n) ? '_L' : /_R$/.test(n) ? '_R' : '';
      partOfBone[n] = /^(Head|Neck)$/.test(n) ? partIndex('head') : /^(Shoulder|Elbow|Wrist|Grip)_/.test(n) ? partIndex('arm' + side) : /^(Hip|Knee|Ankle)_/.test(n) ? partIndex('leg' + side) : partIndex('torso');
    }
    // Match: the runtime mesh may differ from the GLB by a uniform scale and a translation; find them from the bounds.
    const bounds = pts => { const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; for (const p of pts) for (let c = 0; c < 3; c++) { lo[c] = Math.min(lo[c], p[c]); hi[c] = Math.max(hi[c], p[c]); } return { lo, hi }; };
    const rpts = []; for (let i = 0; i < mcount; i++) rpts.push([mf[i * 12], mf[i * 12 + 1], mf[i * 12 + 2]]);
    const gb = bounds(gverts.map(v => toGame(v.p))), rb = bounds(rpts);
    let worst = 0; const jointBytes = Buffer.alloc(mcount), partBytes = Buffer.alloc(mcount);
    let sameOrder = gverts.length === mcount, orderWorst = 0;
    for (let i = 0; i < mcount; i++) {
      let best = -1, bd = 1e9;
      for (let k = 0; k < gverts.length; k++) { const q = toGame(gverts[k].p), d = Math.hypot(q[0] - rpts[i][0], q[1] - rpts[i][1], q[2] - rpts[i][2]); if (d < bd) { bd = d; best = k; } }
      worst = Math.max(worst, bd); jointBytes[i] = gverts[best].joint; partBytes[i] = partOfBone[boneNames[gverts[best].joint]];
      if (sameOrder) { const q = toGame(gverts[i].p); orderWorst = Math.max(orderWorst, Math.hypot(q[0] - rpts[i][0], q[1] - rpts[i][1], q[2] - rpts[i][2])); }
    }
    const mhead = Buffer.alloc(16 + 64); mhead.write('CATENM01'); mhead.writeUInt32LE(mcount, 8); mhead.writeUInt32LE(1, 12);
    RIG_KEYS.forEach((k, j) => mhead.writeFloatLE(data.marine.rig[k], 16 + j * 4));
    put('marine.emesh', Buffer.concat([mhead, mv, jointBytes, partBytes]));
    const mmats = new Set(); for (let k = 11; k < mf.length; k += 12) mmats.add(mf[k]);
    const matName = id => Object.keys(data.material).find(k => data.material[k] === id) || String(id);
    const partCounts = parts.map((p, k) => ({ part: p.name, vertices: [...partBytes].filter(b => b === k).length }));
    const marineReport = {
      runtime: { vertices: mcount, triangles: mcount / 3, bounds: rb, materials: [...mmats].sort((a, b) => a - b).map(m => ({ id: m, name: matName(m) })), rig: Object.fromEntries(RIG_KEYS.map(k => [k, data.marine.rig[k]])), reference: data.marine.reference },
      glb: { file: glbFile, sha256: sha256(fs.readFileSync(path.join(root, glbFile))), triangles: glbTriangles, flattenedVertices: gverts.length, bounds: gb, joints: joints.map(j => ({ ...j, restGame: toGame(j.rest) })), primitives: G.meshes[0].primitives.length, materials: G.materials.map(m => m.name),
        clips: G.animations.map(a => ({ name: a.name, channels: a.channels.length, seconds: Math.max(...a.samplers.map(s => G.accessors[s.input].max[0])) })), maxInfluencesUsed: Math.max(...gverts.map(v => v.weights)) },
      comparison: { nearestVertexWorstDistance: worst, sameTriangleOrder: sameOrder && orderWorst < 1e-5, sameOrderWorstDistance: sameOrder ? orderWorst : null, partVertices: partCounts },
      runtimeUseOfClips: 'none: the legacy poses the Marine Cat with enemyEvalPose(MARINE_RIG) (soldier gait) plus shoot / melee / hurt offsets in drawMarineCats; CAT_MARINE_REFERENCE.animationStates is metadata only',
    };
    // ---- generated C++ tables ----
    const classes = [...new Set(data.types.map(t => t.rig.cls))], attacks = [...new Set(data.types.map(t => t.rig.attack))];
    const projs = [...new Set(data.types.map(t => t.efx && t.efx.proj).filter(Boolean))], melees = [...new Set(data.types.map(t => t.efx && t.efx.melee).filter(Boolean))];
    const arr = a => '{' + a.map(num).join(', ') + '}';
    const cpp = n => ['throw', 'class', 'default', 'new', 'delete'].includes(n) ? n + '_' : n;   // C++ keywords get a trailing underscore
    const rigRow = (r, cls, attack) => `{RigClass::${cls}, AttackAnim::${cpp(attack)}, ${RIG_KEYS.map(k => num(r[k])).join(', ')}}`;
    let inc = `// GENERATED by tools/n6/legacy_enemy_assets.cjs from legacy/index.html (sha256 ${sha}).\n// Do not edit: re-run the tool. Tables: ENEMY_RIGS (enemyRigFromMesh), ENEMY_CLASS, ENEMY_ATTACK_ANIM, GAIT_STRIDE, ENEMY_DEATH, EFX_TYPE,\n// EFX_KIND_DEFAULT, MARINE_RIG, CAT_MARINE_REFERENCE; marine joints from cat_marine_rigged.glb, hit parts from cat_marine_hitboxes.json.\n`;
    inc += `enum class RigClass { ${classes.join(', ')} };\nenum class AttackAnim { ${attacks.map(cpp).join(', ')} };\n`;
    inc += `enum class EnemyProj { none, ${projs.join(', ')} };\nenum class EnemyMelee { none, ${melees.join(', ')} };\n`;
    inc += `struct Rig { RigClass cls; AttackAnim attack; double hip, knee, shoulder, elbow, neck, spine, wing_root, height, shoulder_x, wing_tip, depth, width; };\n`;
    inc += `inline constexpr std::array<Rig, 20> kEnemyRig{{\n${data.types.map((t, i) => `    /* ${i} ${t.type} */ ${rigRow(t.rig, t.rig.cls, t.rig.attack)}`).join(',\n')}\n}};\n`;
    inc += `inline constexpr Rig kMarineRig${rigRow(data.marine.rig, data.marine.rig.cls, data.marine.rig.attack)};\n`;
    inc += `inline constexpr std::array<const char*, 20> kEnemyFile{{${data.types.map((t, i) => `"${String(i).padStart(2, '0')}_${t.type}"`).join(', ')}}};\n`;
    inc += `inline constexpr std::array<const char*, 20> kEnemyName{{${data.types.map(t => `"${t.type}"`).join(', ')}}};\n`;
    inc += `inline constexpr std::array<double, ${classes.length}> kGaitStride{{${classes.map(c => num(data.gait[c])).join(', ')}}};   // metres per gait cycle, by RigClass\n`;
    inc += `inline constexpr double kDeathFall = ${num(data.death.fall)}, kDeathStay = ${num(data.death.stay)}, kDeathSink = ${num(data.death.sink)};\n`;
    inc += `struct EnemyLook { EnemyProj proj; EnemyMelee melee; std::array<double, 3> color; bool flash, charge, rune; };\n`;
    inc += `inline constexpr std::array<EnemyLook, 20> kEnemyLook{{\n${data.types.map((t, i) => { const e = t.efx || {}; return `    /* ${i} ${t.type} */ {EnemyProj::${e.proj || 'none'}, EnemyMelee::${e.melee || 'none'}, ${arr(e.col || [1, 1, 1])}, ${!!e.flash}, ${!!e.charge}, ${!!e.rune}}`; }).join(',\n')}\n}};\n`;
    for (const k of ['fireball', 'voidorb', 'spike', 'shot']) if (!projs.includes(data.kindDefault[k])) throw new Error('EFX_KIND_DEFAULT names an unknown look');
    inc += `// EFX_KIND_DEFAULT, in combat::ShotKind order (shot, spike, fireball, voidorb)\ninline constexpr std::array<EnemyProj, 4> kKindDefaultProj{{EnemyProj::${data.kindDefault.shot}, EnemyProj::${data.kindDefault.spike}, EnemyProj::${data.kindDefault.fireball}, EnemyProj::${data.kindDefault.voidorb}}};\n`;
    inc += `// Marine Cat: skin joints of cat_marine_rigged.glb (rest position in game axes: +Y forward, +Z up) and the hit parts of cat_marine_hitboxes.json.\n`;
    inc += `struct MarineJoint { const char* name; std::array<double, 3> rest; };\n`;
    inc += `inline constexpr std::array<MarineJoint, ${joints.length}> kMarineJoint{{\n${joints.map(j => `    {"${j.name}", ${arr(toGame(j.rest))}}`).join(',\n')}\n}};\n`;
    inc += `struct MarinePart { const char* name; bool capsule; int bone_a, bone_b; std::array<double, 3> center, size; double radius, damage_multiplier; };   // box: centre offset and size in game axes at bone_a; capsule: bone_a -> bone_b\n`;
    inc += `inline constexpr std::array<MarinePart, ${parts.length}> kMarinePart{{\n${parts.map(p => `    {"${p.name}", ${p.shape === 'capsule'}, ${boneNames.indexOf(p.bones[0])}, ${boneNames.indexOf(p.bones[1])}, ${arr(toGame(p.center))}, ${arr(toGame(p.size))}, ${num(p.radius)}, ${num(p.multiplier)}}`).join(',\n')}\n}};\n`;
    inc += `inline constexpr std::array<const char*, ${G.animations.length}> kMarineGlbClip{{${G.animations.map(a => `"${a.name}"`).join(', ')}}};   // present in the GLB; NOT sampled by the legacy runtime\n`;
    if (parts.some(p => boneNames.indexOf(p.bones[0]) < 0 || boneNames.indexOf(p.bones[1]) < 0)) throw new Error('hit part names an unknown bone');
    const incFile = path.join(root, 'native/include/cat/generated/enemy_tables.inc');
    fs.writeFileSync(incFile, inc);
    const report = { tool: 'tools/n6/legacy_enemy_assets.cjs', legacySha256: sha, settings: data.settings, types: typeRows, marine: marineReport, lightVolume: { width: 624, height: 234, slices: 24, columns: 8, rows: 3, tile: 78, zMin: -6, zMax: 24, maps: volumes }, generated: { file: 'native/include/cat/generated/enemy_tables.inc', sha256: sha256(Buffer.from(inc)) }, files };
    fs.writeFileSync(path.join(outDir, 'enemy-assets.json'), JSON.stringify(report, null, 1) + '\n');
    console.log(`wrote ${files.length} files to assets/n6; marine vs GLB worst vertex distance ${worst}; same order ${marineReport.comparison.sameTriangleOrder}`);
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error(e); process.exit(1); });
