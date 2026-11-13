// N7: Web Audio reference for tests/audio_tests.cpp, recorded in the browser the legacy game runs in
// (Playwright's Chromium).
//   NODE_PATH=$(npm root -g) node native/tools/n7/record_webaudio.cjs assets/n7 native/tests/fixtures/n7
// Writes:
//   decode.json   decodeAudioData of every bank sample and the soundtrack at the file's own rate (no
//                 resampling): frames, channels, rate, RMS per channel and 256 probe values per channel
//   graph.f32     float32 stereo renders (48 kHz, 14,400 frames each) of the node chains the legacy uses,
//                 in this order: rate, filters_pan, stereo_pan, sine_beep, triangle_beep, saw_beep, compressor
//   graph.json    scenario names and parameters (the input signals are generated identically in C++)
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const [assetDir, outDir] = process.argv.slice(2);
const bank = fs.readFileSync(path.join(assetDir, 'audio/sfx/bank.txt'), 'utf8').trim().split('\n').map(l => l.split('\t'));
// Ogg Vorbis identification header: 0x01 "vorbis", version (4), channels (1), rate (4, little endian)
const oggRate = (b) => { const i = b.indexOf(Buffer.from([1, 0x76, 0x6f, 0x72, 0x62, 0x69, 0x73])); if (i < 0) throw new Error('no vorbis header'); return b.readUInt32LE(i + 12); };
const files = bank.map(([key, file]) => { const b = fs.readFileSync(path.join(assetDir, 'audio/sfx', file)); return { key, rate: oggRate(b), b64: b.toString('base64') }; });
files.push({ key: 'main-theme', rate: 32000, b64: fs.readFileSync(path.join(assetDir, 'audio/main-theme.mp3')).toString('base64') });   // MPEG-1 layer III, 32 kHz (ffprobe)

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  const page = await browser.newPage();
  const decode = await page.evaluate(async (list) => {
    const out = [];
    for (const f of list) {
      const bytes = Uint8Array.from(atob(f.b64), c => c.charCodeAt(0));
      const srcRate = f.rate;   // decode at the file's own rate: no resampling
      const buf = await new OfflineAudioContext(1, 1, srcRate).decodeAudioData(bytes.slice().buffer);
      const ch = [];
      for (let c = 0; c < buf.numberOfChannels; c++) {
        const d = buf.getChannelData(c);
        let s = 0; for (let i = 0; i < d.length; i++) s += d[i] * d[i];
        const probes = [];
        for (let k = 0; k < 256; k++) probes.push(d[Math.floor((d.length - 1) * k / 255)]);
        ch.push({ rms: Math.sqrt(s / d.length), probes });
      }
      out.push({ key: f.key, rate: buf.sampleRate, frames: buf.length, channels: buf.numberOfChannels, ch });
    }
    return out;
  }, files);

  const graph = await page.evaluate(async () => {
    const R = 48000, N = 14400;
    // input signal: same LCG and formula as tests/audio_tests.cpp make_signal()
    function signal(frames, channels, seed) {
      let s = seed >>> 0;
      const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
      const data = [];
      for (let c = 0; c < channels; c++) data.push(new Float32Array(frames));
      for (let i = 0; i < frames; i++) for (let c = 0; c < channels; c++) {
        const t = i / R;
        data[c][i] = Math.fround(.5 * Math.sin(2 * Math.PI * (220 + 40 * c) * t) + .3 * Math.sin(2 * Math.PI * 3150 * t) + .2 * (rnd() * 2 - 1)) * Math.exp(-t * 4);
      }
      return data;
    }
    function bufferOf(ctx, data) {
      const b = ctx.createBuffer(data.length, data[0].length, R);
      data.forEach((d, c) => b.copyToChannel(d, c));
      return b;
    }
    const scenarios = [];
    async function run(name, params, build) {
      const ctx = new OfflineAudioContext(2, N, R);
      build(ctx);
      const out = await ctx.startRendering();
      const l = out.getChannelData(0), r = out.getChannelData(1), inter = new Float32Array(N * 2);
      for (let i = 0; i < N; i++) { inter[2 * i] = l[i]; inter[2 * i + 1] = r[i]; }
      scenarios.push({ name, params, data: Array.from(inter) });
    }
    await run('rate', { seed: 1, channels: 1, rate: .99, gain: .42 }, ctx => {
      const s = ctx.createBufferSource(), g = ctx.createGain(); s.buffer = bufferOf(ctx, signal(12000, 1, 1)); s.playbackRate.value = .99; g.gain.value = .42;
      s.connect(g).connect(ctx.destination); s.start();
    });
    await run('filters_pan', { seed: 2, channels: 1, rate: 1.0125, lowpass: 4100, highpass: 155, gain: .5, pan: .3 }, ctx => {
      const s = ctx.createBufferSource(), lo = ctx.createBiquadFilter(), hi = ctx.createBiquadFilter(), g = ctx.createGain(), p = ctx.createStereoPanner();
      s.buffer = bufferOf(ctx, signal(12000, 1, 2)); s.playbackRate.value = 1.0125; lo.type = 'lowpass'; lo.frequency.value = 4100; hi.type = 'highpass'; hi.frequency.value = 155;
      g.gain.value = .5; p.pan.value = .3; s.connect(lo).connect(hi).connect(g); g.connect(p).connect(ctx.destination); s.start();
    });
    await run('stereo_pan', { seed: 3, channels: 2, rate: 1, gain: .6, pan: -.4 }, ctx => {
      const s = ctx.createBufferSource(), g = ctx.createGain(), p = ctx.createStereoPanner(); s.buffer = bufferOf(ctx, signal(12000, 2, 3)); g.gain.value = .6; p.pan.value = -.4;
      s.connect(g).connect(p).connect(ctx.destination); s.start();
    });
    const beep = (wave, f, len, vol) => ctx => {   // beep() of the legacy, started at t = 0
      const o = ctx.createOscillator(), g = ctx.createGain(); o.type = wave; o.frequency.value = f;
      o.frequency.exponentialRampToValueAtTime(Math.max(45, f * .49), len); g.gain.setValueAtTime(vol, 0); g.gain.exponentialRampToValueAtTime(.001, len);
      o.connect(g).connect(ctx.destination); o.start(0); o.stop(len);
    };
    await run('sine_beep', { wave: 'sine', f: 830, len: .14, vol: .022 }, beep('sine', 830, .14, .022));
    await run('triangle_beep', { wave: 'triangle', f: 350, len: .08, vol: .019 }, beep('triangle', 350, .08, .019));
    await run('saw_beep', { wave: 'sawtooth', f: 410, len: .14, vol: .08 }, beep('sawtooth', 410, .14, .08));
    await run('compressor', { seed: 4, channels: 1, gain: 2.2 }, ctx => {
      const s = ctx.createBufferSource(), g = ctx.createGain(), c = ctx.createDynamicsCompressor();
      c.threshold.value = -10; c.knee.value = 12; c.ratio.value = 3.1; c.attack.value = .008; c.release.value = .16;
      s.buffer = bufferOf(ctx, signal(12000, 1, 4)); g.gain.value = 2.2; s.connect(g).connect(c).connect(ctx.destination); s.start();
    });
    return scenarios;
  });
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, 'decode.json'), JSON.stringify({ browser: browser.version(), decode }) + '\n');
  const all = new Float32Array(graph.reduce((n, s) => n + s.data.length, 0));
  let o = 0; for (const s of graph) { all.set(s.data, o); o += s.data.length; }
  fs.writeFileSync(path.join(outDir, 'graph.f32'), Buffer.from(all.buffer));
  fs.writeFileSync(path.join(outDir, 'graph.json'), JSON.stringify({ browser: browser.version(), rate: 48000, frames: 14400, scenarios: graph.map(s => ({ name: s.name, params: s.params })) }, null, 1) + '\n');
  console.log(`decode ${decode.length} files, graph ${graph.length} scenarios, Chromium ${browser.version()}`);
  await browser.close();
})();
