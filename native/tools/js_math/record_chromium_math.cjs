// Records Math.sin / cos / tan / atan2 / pow results of the browser the legacy game is verified in
// (Playwright's Chromium) for tests/js_math_tests.cpp.
//   NODE_PATH=$(npm root -g) node tools/js_math/record_chromium_math.cjs tests/fixtures/js_math/chromium_math.bin
// Output: little-endian float64 records {function, a, b, result}; function 0 sin, 1 cos, 2 tan,
// 3 atan2(a, b), 4 pow(a, b). Inputs come from a fixed LCG (the file is reproducible).
const fs = require('fs');
const { chromium } = require('playwright');

const PER_FUNCTION = 6000;
let seed = 20261111;
const rnd = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed / 4294967296; };
// Arguments like the game's (angles, phases, times) plus small, large and near-multiple-of-pi/2 values.
const angle = () => {
  const u = rnd() - 0.5, k = Math.floor(rnd() * 8);
  return [u * 1.6, u * 8, u * 200, u * 2e-6, u * 4e4, u * 1e7, u * 1e22 * rnd(),
    Math.round(u * 64) * Math.PI / 4 * (1 + (rnd() - 0.5) * 1e-12)][k];
};
const special = [0, -0, Infinity, -Infinity, NaN, 1, -1, 0.5, 2, -2, 3, 0.7, 4, 1.05, Math.PI, Math.PI / 2,
  -Math.PI / 2, Math.PI / 4, 1e-310, 5e-324, 1e308, -1e308, 1e-20];

const inputs = [];
for (const a of special) {
  for (let f = 0; f < 3; f++) inputs.push([f, a, 0]);
  for (const b of special) { inputs.push([3, a, b]); inputs.push([4, a, b]); }
}
for (let i = 0; i < PER_FUNCTION; i++) {
  for (let f = 0; f < 3; f++) inputs.push([f, angle(), 0]);
  inputs.push([3, angle() * (rnd() < 0.5 ? 1 : 1e-3), angle()]);
  const k = i % 6;
  const base = [rnd() * 3, rnd() * 1e3, -rnd() * 5, 1 + (rnd() - 0.5) * 1e-7, rnd(), 1.05][k];
  const exponent = [(rnd() - 0.5) * 12, (rnd() - 0.5) * 12, Math.round((rnd() - 0.5) * 20), (rnd() - 0.5) * 1e10,
    rnd() < 0.5 ? 0.7 : 4, Math.floor(rnd() * 60)][k];
  inputs.push([4, base, exponent]);
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const results = await page.evaluate((list) => list.map(([f, a, b]) =>
    [Math.sin, Math.cos, Math.tan, Math.atan2, Math.pow][f](a, b)), inputs);
  const out = new Float64Array(inputs.length * 4);
  inputs.forEach(([f, a, b], i) => out.set([f, a, b, results[i]], i * 4));
  fs.writeFileSync(process.argv[2], Buffer.from(out.buffer));
  console.log(`${inputs.length} records from Chromium ${browser.version()}`);
  await browser.close();
})();
