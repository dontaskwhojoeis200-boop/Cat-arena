'use strict';
// N8 interop test driver: starts the ORIGINAL relay (reference/multiplayer/server.js, unmodified, with the
// real `ws` package) on a free port, runs a native test program against it, stops the relay.
// usage: node run_with_legacy_relay.cjs <server.js> <node_modules dir with ws> <program> [args...]
// The program receives `ws://127.0.0.1:<port>` after its own arguments.
const cp = require('node:child_process'), path = require('node:path'), fs = require('node:fs'), os = require('node:os'), net = require('node:net');
const [serverJs, modules, program, ...args] = process.argv.slice(2);
if(!serverJs || !modules || !program) { console.error('usage: run_with_legacy_relay.cjs <server.js> <node_modules> <program> [args]'); process.exit(2); }
const freePort = () => new Promise(res => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
(async () => {
  // server.js resolves `ws` next to itself: run an untouched copy in a temp folder that links the modules.
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cat-relay-'));
  fs.copyFileSync(serverJs, path.join(dir, 'server.js'));
  fs.symlinkSync(path.resolve(modules), path.join(dir, 'node_modules'), process.platform === 'win32' ? 'junction' : 'dir');
  const port = await freePort();
  const relay = cp.spawn(process.execPath, ['server.js'], { cwd: dir, env: { ...process.env, PORT: String(port), HOST: '127.0.0.1' }, stdio: ['ignore', 'pipe', 'pipe'] });
  const relayClosed = new Promise(resolve => relay.once('close', resolve));
  let log = '';
  relay.stdout.on('data', d => { log += d; }); relay.stderr.on('data', d => { log += d; });
  for(let i = 0; i < 100 && !log.includes('listening'); i++) await new Promise(r => setTimeout(r, 50));
  const hash = require('node:crypto').createHash('sha256').update(fs.readFileSync(serverJs)).digest('hex');
  console.log(`original relay ${serverJs} (sha256 ${hash}) on port ${port}; ws ${require(path.join(path.resolve(modules), 'ws', 'package.json')).version}`);
  const run = cp.spawnSync(program, [...args, `ws://127.0.0.1:${port}`], { stdio: 'inherit', timeout: 100000 });
  relay.kill();
  await relayClosed; // Windows holds the process working directory until the relay has exited.
  console.log('--- relay log ---\n' + log.trim());
  fs.rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  process.exit(run.status === null ? 1 : run.status);
})().catch(e => { console.error(e); process.exit(1); });
