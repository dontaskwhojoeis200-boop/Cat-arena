#!/usr/bin/env node
// N5 weapon presentation assets, taken from the UNMODIFIED legacy/index.html (hash-checked) running in
// headless Chromium. Nothing is drawn or simulated; the tool only reads data the page builds at load:
//   * MESHY_ASSETS[0..15]: the 16 first-person weapon models (12 floats per vertex: position, normal,
//     colour, uv, material id), their muzzle point and three JPEG textures (base colour,
//     metal/roughness, normal) -> assets/n5/weapons/NN_name.gun + NN_name_{base,mr,normal}.jpg
//     (the JPEG bytes are copied verbatim out of the data: URLs)
//   * FX_ATLAS.canvas: the 8 x 8 tile effect atlas the legacy paints procedurally at start-up, read
//     back with getImageData (the same un-premultiplied bytes WebGL uploads) -> assets/n5/fx-atlas.png
//   * FX_TILE, WFX_DEF, VIEWMODEL_MUZZLE_HEIGHT, CLASSIC_VIEWMODEL, QUAKE_REF: effect and viewmodel
//     tables -> native/include/cat/generated/weapon_fx_tables.inc (generated C++, no hand transcription)
//   node tools/n5/legacy_weapon_assets.cjs
// .gun layout: "CATGUN01", u32 vertexCount, f32 muzzle[3], f32[vertexCount*12].
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
const num = v => { const s = String(v); return /[.e]/.test(s) ? s : s + '.0'; };
(async () => {
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = sha256(bytes);
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => { const url = new URL(req.url, 'http://localhost'); if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  try {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=helios-crucible`, { waitUntil: 'load', timeout: 900000 });
    const data = await page.evaluate(() => {
      const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); };
      const c = FX_ATLAS.canvas, img = c.getContext('2d').getImageData(0, 0, c.width, c.height);
      return {
        guns: MESHY_ASSETS.map((a, i) => ({ name: a.name, triangles: a.triangles, muzzle: a.muzzle, vertices: a.vertices, textures: a.textures, built: buildGun(i).length, weapon: WEAPONS[i].name })),
        atlas: { width: c.width, height: c.height, tiles: FX_ATLAS.tiles, size: FX_ATLAS.size, rgba: b64(new Uint8Array(img.data.buffer)) },
        tiles: { ...FX_TILE }, def: WFX_DEF, muzzleHeight: [...VIEWMODEL_MUZZLE_HEIGHT], classic: { ...CLASSIC_VIEWMODEL }, quake: { ...QUAKE_REF },
      };
    });
    const outDir = path.join(root, 'assets/n5'), gunDir = path.join(outDir, 'weapons');
    fs.mkdirSync(gunDir, { recursive: true });
    const files = [];
    const put = (file, buf) => { fs.writeFileSync(path.join(outDir, file), buf); files.push({ file, bytes: buf.length, sha256: sha256(buf) }); };
    if (data.guns.length !== 16) throw new Error('expected 16 weapon models');
    const gunRows = [];
    data.guns.forEach((g, i) => {
      const v = Buffer.from(g.vertices, 'base64');
      if (v.length % 48 || v.length / 48 !== g.triangles * 3 || g.built !== v.length / 4) throw new Error('unexpected vertex data for ' + g.name);
      const head = Buffer.alloc(24); head.write('CATGUN01'); head.writeUInt32LE(v.length / 48, 8); g.muzzle.forEach((m, k) => head.writeFloatLE(m, 12 + k * 4));
      const stem = String(i).padStart(2, '0') + '_' + g.name;
      put('weapons/' + stem + '.gun', Buffer.concat([head, v]));
      ['base', 'mr', 'normal'].forEach((kind, k) => {
        const m = /^data:image\/jpeg;base64,(.*)$/.exec(g.textures[k]); if (!m) throw new Error('texture is not a JPEG data URL: ' + g.name + ' ' + kind);
        put('weapons/' + stem + '_' + kind + '.jpg', Buffer.from(m[1], 'base64'));
      });
      gunRows.push({ index: i, weapon: g.weapon, model: g.name, stem, triangles: g.triangles, muzzle: g.muzzle });
    });
    const rgba = Buffer.from(data.atlas.rgba, 'base64');
    if (rgba.length !== data.atlas.width * data.atlas.height * 4 || data.atlas.width !== 1024 || data.atlas.tiles !== 8) throw new Error('unexpected FX atlas');
    put('fx-atlas.png', png(data.atlas.width, data.atlas.height, rgba));
    // ---- generated C++ tables ----
    const tileNames = Object.keys(data.tiles);
    const kinds = [...new Set(data.def.map(d => d.kind))], impacts = [...new Set(data.def.map(d => d.impact))], marks = [...new Set(data.def.map(d => d.mark || 'none'))], brass = [...new Set(data.def.map(d => d.brass || 'none'))];
    const id = s => s.replace(/[^a-z0-9]/gi, '_');
    let inc = `// GENERATED by tools/n5/legacy_weapon_assets.cjs from legacy/index.html (sha256 ${sha}).\n// Do not edit: re-run the tool. Tables: FX_TILE, WFX_DEF, VIEWMODEL_MUZZLE_HEIGHT, CLASSIC_VIEWMODEL, QUAKE_REF, MESHY_ASSETS[].name.\n`;
    inc += `enum Tile : int { ${tileNames.map(n => `tile_${n.toLowerCase()} = ${data.tiles[n]}`).join(', ')} };\n`;
    inc += `enum class ShotKind { ${kinds.map(id).join(', ')} };\nenum class ImpactKind { ${impacts.map(id).join(', ')} };\nenum class MarkKind { ${marks.map(id).join(', ')} };\nenum class BrassKind { ${brass.map(id).join(', ')} };\n`;
    inc += `struct FxDef {\n    ShotKind kind; std::array<double, 3> color; double speed; bool hot;\n    double flash_size, flash_side, flash_life; std::array<double, 3> flash_color; std::array<double, 4> light;\n    BrassKind brass; double brass_delay; int brass_count; double brass_scale, smoke;\n    double recoil_back, recoil_climb, recoil_roll, recoil_jitter, recoil_k;\n    ImpactKind impact; MarkKind mark; double mark_size;\n};\n`;
    inc += `inline constexpr std::array<FxDef, 16> kFxDef{{\n` + data.def.map((d, i) =>
      `    /* ${i} ${data.guns[i].weapon} */ {ShotKind::${id(d.kind)}, {${d.color.map(num).join(', ')}}, ${num(d.speed || 0)}, ${d.hot ? 'true' : 'false'}, ${num(d.flash.size)}, ${num(d.flash.side)}, ${num(d.flash.life)}, {${d.flash.col.map(num).join(', ')}}, {${d.dl.map(num).join(', ')}}, ` +
      `BrassKind::${id(d.brass || 'none')}, ${num(d.brassDelay || 0)}, ${d.brassCount || 0}, ${num(d.brassScale || 1)}, ${num(d.smoke || 0)}, ${num(d.recoil.back)}, ${num(d.recoil.climb)}, ${num(d.recoil.roll)}, ${num(d.recoil.jit)}, ${num(d.recoil.k)}, ` +
      `ImpactKind::${id(d.impact)}, MarkKind::${id(d.mark || 'none')}, ${num(d.markSize || 0)}},`).join('\n') + `\n}};\n`;
    inc += `inline constexpr std::array<double, 16> kMuzzleHeight{${data.muzzleHeight.map(num).join(', ')}};   // VIEWMODEL_MUZZLE_HEIGHT\n`;
    inc += `inline constexpr double kPoseX = ${num(data.classic.x)}, kPoseY = ${num(data.classic.y)}, kPoseZ = ${num(data.classic.z)}, kPoseToeIn = ${num(data.classic.toeIn)}, kPoseMuzzleX = ${num(data.classic.muzzleX)}, kPoseMuzzleY = ${num(data.classic.muzzleY)}, kPoseMuzzleZ = ${num(data.classic.muzzleZ)};   // CLASSIC_VIEWMODEL\n`;
    inc += `inline constexpr double kRefMuzzleX = ${num(data.quake.muzzleX)}, kRefFov = ${num(data.quake.fov)};   // QUAKE_REF\n`;
    inc += `inline constexpr std::array<std::array<double, 3>, 16> kGunMuzzle{{${gunRows.map(g => `{${g.muzzle.map(num).join(', ')}}`).join(', ')}}};   // MESHY_ASSETS[i].muzzle\n`;
    inc += `inline constexpr std::array<const char*, 16> kGunFile{${gunRows.map(g => `"${g.stem}"`).join(', ')}};\n`;
    fs.mkdirSync(path.join(root, 'native/include/cat/generated'), { recursive: true });
    fs.writeFileSync(path.join(root, 'native/include/cat/generated/weapon_fx_tables.inc'), inc);
    const report = { tool: 'tools/n5/legacy_weapon_assets.cjs', source: 'legacy/index.html', sha256: sha, weapons: gunRows, fxAtlas: { width: 1024, height: 1024, tiles: 8, tilePixels: 128, tileNames: data.tiles }, generated: { file: 'native/include/cat/generated/weapon_fx_tables.inc', sha256: sha256(Buffer.from(inc)) }, files };
    fs.writeFileSync(path.join(outDir, 'weapon-assets.json'), JSON.stringify(report, null, 1) + '\n');
    process.stdout.write(`wrote ${files.length} files to assets/n5 (${files.reduce((a, f) => a + f.bytes, 0)} bytes) and native/include/cat/generated/weapon_fx_tables.inc\n`);
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
