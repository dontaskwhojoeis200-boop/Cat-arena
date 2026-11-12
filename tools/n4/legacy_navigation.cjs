#!/usr/bin/env node
// N4 navigation reference: dumps the layered navigation graph (NAV3) and routes from the UNMODIFIED
// legacy game for one map.
//
//   node tools/n4/legacy_navigation.cjs --map helios-crucible --out native/tests/fixtures/n4/helios-crucible_navigation.bin [--report f.json]
//
// From the running page (after a solo game has started, simulation held with `fovPanelOpen`):
//   * NAV3.nodes as built by the legacy nav3Build();
//   * for each walker profile (light, standard, heavy) the adjacency of every node from the legacy
//     nav3FillAdj(): edge count, per-node neighbour counts and a 32-bit FNV-1a hash over
//     (from, to, cost low word, cost high word);
//   * route queries: nav3Nearest() for start and goal, nav3TargetFloorUncached(), then the A* loop of
//     processNavJobs() transcribed below with the legacy heap, heuristic, adjacency tables and
//     float32 g-scores (the legacy loop itself is tied to enemy objects and a time budget), the node
//     chain as nav3Finish() builds it, and the smoothed route from the legacy nav3ApplyRoute().
// Output (little endian): "CATNAV01", u32 version=1, u32 nodes, u32 queries; f64[nodes*6]
// {x,y,z,gx,gy,layer*10+kind}; 3 x {u32 edges, u32 hash, f64[nodes] neighbour counts}; per query
// f64[12] {profile,fx,fy,fz,tx,ty,tz,targetFloor,startId,goalId,found,expanded}, u32 n, f64[n] chain
// ids, u32 m, f64[m*3] route points.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');
const { chromium } = require('playwright');
(async () => {
  const v = process.argv.slice(2); let out = null, reportFile = null, map = 'helios-crucible';
  for (let i = 0; i < v.length; i++) { if (v[i] === '--out') out = v[++i]; else if (v[i] === '--report') reportFile = v[++i]; else if (v[i] === '--map') map = v[++i]; else throw new Error('unknown option ' + v[i]); }
  if (!out) throw new Error('usage: --map <id> --out <file.bin> [--report file.json]');
  const root = path.resolve(__dirname, '../..');
  const bytes = fs.readFileSync(path.join(root, 'legacy/index.html'));
  const sha = crypto.createHash('sha256').update(bytes).digest('hex');
  if (sha !== 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393') throw new Error('legacy/index.html hash mismatch: ' + sha);
  const server = http.createServer((req, res) => { const url = new URL(req.url, 'http://localhost'); if (url.pathname === '/' || url.pathname === '/index.html') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); res.end(bytes); } else { res.writeHead(404); res.end(); } });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const browser = await chromium.launch({ env: { ...process.env } });
  try {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    const errors = []; page.on('pageerror', e => errors.push(String(e).slice(0, 300)));
    page.setDefaultTimeout(900000);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html?map=${map}`, { waitUntil: 'load', timeout: 900000 });
    await page.evaluate(() => document.getElementById('start').click());
    await page.waitForFunction(() => { const b = document.getElementById('loadReady'); return b && b.offsetParent !== null && !b.disabled; }, null, { timeout: 900000, polling: 1000 });
    await page.click('#loadReady');
    await page.waitForFunction(() => typeof active !== 'undefined' && active === true && NAV3.ready, null, { timeout: 900000, polling: 500 });
    const result = await page.evaluate(() => {
      fovPanelOpen = true; bots.length = 0;
      const profiles = [NAV3_PROFILES.light, NAV3_PROFILES.standard, NAV3_PROFILES.heavy];
      const kinds = { floor: 0, bridge: 1, ramp: 2 }, layers = { L1: 0, L2: 1, L3: 2 };
      const nodes = []; for (const n of NAV3.nodes) nodes.push(n.x, n.y, n.z, n.gx, n.gy, layers[n.layer] * 10 + kinds[n.kind]);
      const adjacency = [], f64 = new Float64Array(1), u32 = new Uint32Array(f64.buffer);
      for (const p of profiles) {
        const T = nav3AdjTable(p); let h = 2166136261 >>> 0, edges = 0; const counts = [];
        const mix = w => { h = Math.imul(h ^ (w >>> 0), 16777619) >>> 0; };
        for (let id = 0; id < NAV3.nodes.length; id++) {
          if (T.cnt[id] < 0) nav3FillAdj(T, id, p);
          counts.push(T.cnt[id]);
          for (let k = 0; k < T.cnt[id]; k++) { f64[0] = T.cost[id * T.M + k]; mix(id); mix(T.to[id * T.M + k]); mix(u32[0]); mix(u32[1]); edges++; }
        }
        adjacency.push({ edges, hash: h, counts });
      }
      // ---- queries ----
      const S = WORLD.spawns, N = NAV3.nodes, queries = [];
      const add = (pi, f, t) => queries.push({ pi, fx: f[0], fy: f[1], fz: f[2], tx: t[0], ty: t[1], tz: t[2] });
      for (let q = 0; q < 36; q++) { const a = S[q % S.length], b = S[(q * 7 + 3) % S.length]; add(q % 3, [a.x + .37, a.y - .21, a.z], [b.x - .18, b.y + .29, b.z + (q % 5 === 0 ? 1.1 : 0)]); }
      for (let q = 0; q < 14; q++) { const a = N[(q * 977 + 131) % N.length], b = N[(q * 4513 + 2999) % N.length]; add((q + 1) % 3, [a.x + .2, a.y + .1, a.z], [b.x - .3, b.y + .2, b.z]); }
      const done = [];
      for (const q of queries) {
        const p = profiles[q.pi], r = { ...q, start: -1, goal: -1, found: 0, expanded: 0, chain: [], route: [] };
        const tf = nav3TargetFloorUncached({ x: q.tx, y: q.ty, z: q.tz }); r.tf = tf;
        const start = nav3Nearest(q.fx, q.fy, q.fz, p), goal = nav3Nearest(q.tx, q.ty, tf, p, true);
        if (start) r.start = start.id; if (goal) r.goal = goal.id;
        if (start && goal) {
          // --- processNavJobs() search loop, transcribed; legacy heap / heuristic / adjacency / float32 g ---
          const n = NAV3.nodes.length, search = nav3SearchBuffers(n);
          const job = { profile: p, start, goal, heap: nav3HeapReset(), g: search.g, parent: search.parent, closed: search.closed, expanded: 0 };
          job.g[start.id] = 0; nav3HeapPush(job, start.id, nav3Heuristic(start, goal));
          let success = false;
          for (;;) {
            if (!job.heap.size || job.expanded > NAV3_MAX_SEARCH) break;
            const id = nav3HeapPop(job); if (job.closed[id]) continue;
            job.closed[id] = 1; job.expanded++;
            if (id === job.goal.id) { success = true; break; }
            const T = nav3AdjTable(job.profile); if (T.cnt[id] < 0) nav3FillAdj(T, id, job.profile);
            const base = id * T.M, cnt = T.cnt[id], TO = T.to, CO = T.cost;
            for (let k = 0; k < cnt; k++) { const to = TO[base + k], cost = CO[base + k];
              if (job.closed[to]) continue;
              const alt = job.g[id] + cost; if (alt >= job.g[to]) continue;
              job.parent[to] = id; job.g[to] = alt; nav3HeapPush(job, to, alt + nav3Heuristic(NAV3.nodes[to], job.goal));
            }
          }
          r.expanded = job.expanded;
          if (success) {   // nav3Finish(): node chain from the goal back to the start
            const reverse = []; let id = job.goal.id;
            for (let i = 0; i < 4000 && id !== job.start.id && id >= 0; i++) { reverse.push(NAV3.nodes[id]); id = job.parent[id]; }
            if (id === job.start.id) {
              reverse.reverse(); r.found = 1; r.chain = reverse.map(n => n.id);
              const walker = { x: q.fx, y: q.fy, z: q.fz };
              nav3ApplyRoute(walker, reverse, q.tx, q.ty, tf, p);   // legacy smoothing
              for (const w of walker.path) r.route.push(w.x, w.y, w.z);
            }
          }
        }
        done.push(r);
      }
      return { map: window.VOID_ACTIVE_MAP, nodes, adjacency, queries: done, layerCounts: NAV3.layerCounts, gridWidth: NAV3.gridWidth, buildMs: NAV3.buildMilliseconds };
    });
    if (errors.length) throw new Error('page errors: ' + errors.join(' | '));
    const chunks = []; const u32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n >>> 0); chunks.push(b); }; const f64s = a => chunks.push(Buffer.from(new Float64Array(a).buffer));
    const count = result.nodes.length / 6;
    chunks.push(Buffer.from('CATNAV01')); u32(1); u32(count); u32(result.queries.length);
    f64s(result.nodes);
    for (const a of result.adjacency) { u32(a.edges); u32(a.hash); f64s(a.counts); }
    for (const q of result.queries) { f64s([q.pi, q.fx, q.fy, q.fz, q.tx, q.ty, q.tz, q.tf, q.start, q.goal, q.found, q.expanded]); u32(q.chain.length); f64s(q.chain); u32(q.route.length / 3); f64s(q.route); }
    const blob = Buffer.concat(chunks); fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true }); fs.writeFileSync(out, blob);
    const found = result.queries.filter(q => q.found).length, cross = result.queries.filter(q => q.found && Math.abs(q.fz - q.tf) > 1).length;
    const report = { tool: 'tools/n4/legacy_navigation.cjs', source: 'legacy/index.html', sha256: sha, map: result.map, nodes: count, layerCounts: result.layerCounts, gridWidth: result.gridWidth,
      edges: { light: result.adjacency[0].edges, standard: result.adjacency[1].edges, heavy: result.adjacency[2].edges },
      queries: result.queries.length, routesFound: found, routesBetweenFloors: cross, noStartOrGoal: result.queries.filter(q => q.start < 0 || q.goal < 0).length,
      longestChain: Math.max(...result.queries.map(q => q.chain.length)), mostExpanded: Math.max(...result.queries.map(q => q.expanded)),
      fixture: { file: path.basename(out), bytes: blob.length, sha256: crypto.createHash('sha256').update(blob).digest('hex') } };
    if (reportFile) fs.writeFileSync(reportFile, JSON.stringify(report, null, 1) + '\n');
    console.log(JSON.stringify(report));
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error('FAIL', e && e.stack || e); process.exit(1); });
