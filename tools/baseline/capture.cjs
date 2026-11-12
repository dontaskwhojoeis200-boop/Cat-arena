'use strict';
// Reference capture only. Never edits the game or uses persistent user browser profiles.
// This driver has syntax validation only at checkpoint 0.2; see BASELINE_REPORT.md.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),crypto=require('node:crypto'),os=require('node:os');
const root=path.resolve(__dirname,'../..'),cfg=require('./reference_config.json');
const out=path.resolve(process.env.BASELINE_OUTPUT||path.join(root,'docs/baseline/runs',new Date().toISOString().replace(/[:.]/g,'-')));
fs.mkdirSync(out,{recursive:true});
const report={startedUTC:new Date().toISOString(),status:'IN_PROGRESS',configuration:cfg,host:{platform:os.platform(),release:os.release(),arch:os.arch(),cpu:os.cpus()[0]?.model,visibleCpuCount:os.cpus().length},maps:[],measureRequested:process.argv.includes('--measure'),limitations:['Automated smoke checks are not full feature parity.','No Android installation, human audio quality or live multiplayer test in this driver.']};
const save=()=>fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));
let browser,server;
async function wait(ms){return new Promise(r=>setTimeout(r,ms));}
async function state(page){return page.evaluate(()=>({active,map:window.VOID_ACTIVE_MAP,player:player?{x:player.x,y:player.y,z:player.z,a:player.a,pitch:player.pitch,hp:player.hp}:null,wave:waveNumber,bots:bots.length,loadout:[...loadout],slot,mag:[...mag],audio:audio?{state:audio.state,sampleRate:audio.sampleRate,musicPaused:themeElement?.paused,musicTime:themeElement?.currentTime,musicReady:themeElement?.readyState,decodedSamples:VOID_V318_DECODED.size}:null,glError:gl.getError(),contextLost:gl.isContextLost()}));}
async function settings(page){await page.evaluate(c=>{
 const set=(id,value,event='change')=>{const e=document.getElementById(id);if(!e)throw Error('Missing setting '+id);if(e.type==='checkbox')e.checked=value;else e.value=String(value);e.dispatchEvent(new Event(event,{bubbles:true}));};
 window.VOID_GRAPHICS.preset(c.preset);set('adaptiveResolution',false);set('sceneResolution',c.renderHeight,'input');set('msaaSamples',c.msaa);set('fxaaEnabled',c.fxaa);set('worldCullingMode',c.culling);set('fpsLimit',c.frameLimit,'input');set('vsyncEnabled',c.vsyncPreference);setFov(c.fov);
 },cfg.settings);}
async function measure(page,camera){
 // Deliberately declared zero-enemy static rendering scene. Never used as combat FPS.
 await page.evaluate(p=>{
  resetTouchInput();fovPanelOpen=true;now=0;
  bots=[];dyingEnemies.length=0;enemyShots=[];rockets=[];grenades=[];energyBolts=[];bullets=[];particles=[];corpses=[];explosions=[];impactFx.length=0;
  loadout=[0,1];slot=0;mag=[WEAPONS[0].clip,WEAPONS[1].clip];
  Object.assign(player,{x:p[0],y:p[1],z:p[2],a:p[3],pitch:p[4],vx:0,vy:0,vz:0,crouchOffset:0,downed:false});q3Reset();
 },camera);
 await wait(cfg.warmupSeconds*1000);
 const result=await page.evaluate(async seconds=>{
  const raw=[],cpuReadings=[];let last=null,start=null;
  await new Promise(resolve=>{function sample(t){if(start===null)start=t;if(last!==null)raw.push(t-last);last=t;cpuReadings.push(lastCPU);if(t-start>=seconds*1000)resolve();else requestAnimationFrame(sample);}requestAnimationFrame(sample)});
  const sum=raw.reduce((a,b)=>a+b,0),sorted=[...raw].sort((a,b)=>b-a);
  const low=p=>{const n=Math.ceil(raw.length*p);return n?1000/(sorted.slice(0,n).reduce((a,b)=>a+b,0)/n):null;};
  return {scenario:'STATIC_RENDER_ONLY_ZERO_ENEMIES',sourceUnmodified:true,renderResolution:[canvas.width,canvas.height],camera:{x:player.x,y:player.y,z:player.z,a:player.a,pitch:player.pitch},entityCounts:{bots:bots.length,enemyShots:enemyShots.length,particles:particles.length},settings:window.VOID_GRAPHICS(),frames:raw.length,sampledMs:sum,averageRafFPS:raw.length?1000*raw.length/sum:null,low1FPS:raw.length>=100?low(.01):null,lowPoint1FPS:raw.length>=1000?low(.001):null,rawRafFrameMs:raw,cpuCallbackRollingReadingsMs:cpuReadings,gpuLastMs:lastGpuMS,gpuTimerSupported:!!perfExt,estimatedGpuAllocationBytes:getTrackedGpuBytes(),jsHeapBytes:getHeap(),culling:window.VOID_CULLING(),note:'RAF pacing includes browser scheduling. CPU entries are repeated rolling in-game readings, not independent per-frame CPU samples. GPU value can be null; no process RAM or physical VRAM measurement.'};
 },cfg.sampleSeconds);
 if(result.entityCounts.bots!==0||result.renderResolution[0]!==1280||result.renderResolution[1]!==720)throw Error('Controlled scene invariant failed');
 return result;
}
(async()=>{
 const source=fs.readFileSync(path.join(root,cfg.source));
 const hash=crypto.createHash('sha256').update(source).digest('hex');if(hash!==cfg.sha256)throw Error('Reference SHA-256 mismatch');report.sourceHashVerified=hash;
 let pw, pwPath='playwright';try{pw=require(pwPath)}catch(e){if(!process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES)throw e;pwPath=path.join(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES,'playwright');pw=require(pwPath)}
 report.playwrightVersion=require(path.join(path.dirname(require.resolve(pwPath)),'package.json')).version;
 server=http.createServer((req,res)=>{const u=new URL(req.url,'http://localhost');if(u.pathname==='/'||u.pathname==='/index.html'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'});res.end(source)}else{res.writeHead(404);res.end('Not found')}});
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve)});
 const options={headless:process.env.BASELINE_HEADLESS==='1',args:[]};
 if(process.env.CHROMIUM_PATH)options.executablePath=process.env.CHROMIUM_PATH;
 // Hardware/browser-default rendering. No software-GPU or sandbox-bypass flags.
 report.launchOptions=options;browser=await pw.chromium.launch(options);report.browserVersion=browser.version();
 const base='http://127.0.0.1:'+server.address().port;
 for(const [map,camera] of Object.entries(cfg.maps)){
  const context=await browser.newContext({viewport:cfg.viewport,deviceScaleFactor:cfg.deviceScaleFactor});
  const page=await context.newPage();page.setDefaultTimeout(30000);const m={map,status:'IN_PROGRESS',consoleErrors:[],pageErrors:[],measurements:[]};report.maps.push(m);save();
  page.on('pageerror',e=>m.pageErrors.push(String(e)));page.on('console',e=>{if(['error','warning'].includes(e.type()))m.consoleErrors.push({type:e.type(),text:e.text()})});
  try{
   await page.goto(base+'/index.html?map='+map,{waitUntil:'load',timeout:120000});
   await page.waitForFunction(()=>window.VOID_UI_READY===true);
   m.environment=await page.evaluate(()=>{const e=gl.getExtension('WEBGL_debug_renderer_info');return {userAgent:navigator.userAgent,viewport:[innerWidth,innerHeight],devicePixelRatio,renderer:e?gl.getParameter(e.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER),vendor:e?gl.getParameter(e.UNMASKED_VENDOR_WEBGL):gl.getParameter(gl.VENDOR),webgl:gl.getParameter(gl.VERSION),shadingLanguage:gl.getParameter(gl.SHADING_LANGUAGE_VERSION),extensions:gl.getSupportedExtensions()}});
   await settings(page);m.graphics=await page.evaluate(()=>({settings:window.VOID_GRAPHICS(),forms:[...document.querySelectorAll('input,select')].map(e=>({id:e.id,value:e.value,checked:e.type==='checkbox'?e.checked:undefined})),resolution:[canvas.width,canvas.height],fov:horizontalFov,renderScale}));
   await page.screenshot({path:path.join(out,map+'-menu.png')});
   await page.click('#catPlay');await page.waitForFunction(()=>!document.getElementById('start').disabled,null,{timeout:180000});
   m.previewStatus=await page.locator('#catPreviewStatus').textContent();await page.screenshot({path:path.join(out,map+'-selection.png')});
   await page.click('#start');await page.waitForSelector('#loadReady',{state:'visible',timeout:180000});
   m.loadInfo=await page.locator('#loadInfo').textContent();await page.click('#loadReady');await page.waitForFunction(()=>active===true);
   await wait(1500);m.initial=await state(page);await page.screenshot({path:path.join(out,map+'-gameplay.png')});
   await page.keyboard.down('w');await wait(500);await page.keyboard.up('w');m.afterForward=await state(page);
   await page.keyboard.press('Space');await wait(200);m.afterJump=await state(page);
   // Click at canvas center: input path rather than direct fireWeapon call.
   await page.mouse.click(640,360);await wait(250);m.afterFire=await state(page);
   m.smokeObservations={forwardDisplacement:Math.hypot(m.afterForward.player.x-m.initial.player.x,m.afterForward.player.y-m.initial.player.y),initialEnemies:m.initial.bots,ammoBefore:m.afterJump.mag,ammoAfter:m.afterFire.mag};
   // These observations do not assert collision/trajectory/combat correctness.
   if(report.measureRequested){for(let i=0;i<cfg.repetitions;i++)m.measurements.push(await measure(page,camera));await page.screenshot({path:path.join(out,map+'-static-reference.png')});await page.evaluate(()=>{fovPanelOpen=false;});}
   await page.evaluate(()=>document.exitPointerLock?.());await page.click('#voidPauseButton');
   await page.waitForSelector('#voidPauseOverlay',{state:'visible'});await page.screenshot({path:path.join(out,map+'-pause.png')});
   await page.click('#catReturnMenu');await page.waitForFunction(()=>!active&&window.CAT_UI.route==='home');m.returnToMenu=true;
   m.status=m.pageErrors.length?'ERRORS_RECORDED':'CAPTURED_REQUIRES_REVIEW';
  }catch(e){m.status='FAILED';m.failure=String(e);try{await page.screenshot({path:path.join(out,map+'-failure.png')})}catch{}}
  finally{await context.close();save();}
 }
 report.status=report.maps.every(m=>m.status==='CAPTURED_REQUIRES_REVIEW')?'CAPTURED_REQUIRES_MANUAL_REVIEW':'INCOMPLETE';
})().catch(e=>{report.status='BLOCKED';report.failure=String(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();if(server)await new Promise(r=>server.close(r));report.finishedUTC=new Date().toISOString();save();console.log('Capture status:',report.status,'Report:',path.join(out,'results.json'));});
