#!/usr/bin/env node
'use strict';
// Offline converter only. Source execution is confined to version-pinned CPU modules.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),crypto=require('node:crypto');
const acorn=require('acorn');const {createCanvas,loadImage}=require('@napi-rs/canvas');
const root=path.resolve(__dirname,'../..');
const out=path.resolve(process.argv[2]||path.join(root,'assets/n1'));
const html=fs.readFileSync(path.join(root,'legacy/index.html'),'utf8');
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const expected=JSON.parse(fs.readFileSync(path.join(root,'docs/ORIGINAL_HANDOFF_HASHES.json'))).find(x=>x.path==='legacy/index.html').sha256;
if(sha(html)!==expected)throw Error('Primary source changed: review source selectors before baking');
const blocks=[...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script\s*>/gi)].map(m=>m[1]);
if(blocks.length!==55)throw Error('Unexpected script count');
const lines=html.split('\n');const range=(a,b)=>lines.slice(a-1,b).join('\n');
const sourceTrees=new Map();
function functionSource(block,name){
 if(!sourceTrees.has(block))sourceTrees.set(block,acorn.parse(blocks[block-1],{ecmaVersion:'latest'}));
 const f=sourceTrees.get(block).body.find(n=>n.type==='FunctionDeclaration'&&n.id.name===name);
 if(!f)throw Error('Missing source function '+name);return blocks[block-1].slice(f.start,f.end);
}
function declarationSource(block,name){
 if(!sourceTrees.has(block))sourceTrees.set(block,acorn.parse(blocks[block-1],{ecmaVersion:'latest'}));
 for(const n of sourceTrees.get(block).body)if(n.type==='VariableDeclaration')for(const d of n.declarations)if(d.id.name===name)return `const ${name}=`+blocks[block-1].slice(d.init.start,d.init.end)+';';
 throw Error('Missing data declaration '+name);
}
function nestedFunction(block,name){
 const tree=acorn.parse(blocks[block-1],{ecmaVersion:'latest'});let found;
 function visit(n){if(!n||typeof n!=='object')return;if(n.type==='FunctionDeclaration'&&n.id.name===name){if(found)throw Error('Ambiguous function '+name);found=blocks[block-1].slice(n.start,n.end);}for(const v of Object.values(n))if(Array.isArray(v))v.forEach(visit);else if(v&&typeof v==='object')visit(v);}
 visit(tree);if(!found)throw Error('Missing function '+name);return found;
}
function image64(stem,data){const b=Buffer.from(data.replace(/^data:[^;]+;base64,/,''),'base64');let ext;if(b.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])))ext='.png';else if(b[0]===255&&b[1]===216)ext='.jpg';else if(b.toString('ascii',0,4)==='RIFF')ext='.webp';else throw Error('Unknown image payload '+stem);return write(stem+ext,b);}
function canonical(value,where='$'){
 if(value===null||typeof value==='string'||typeof value==='boolean')return value;
 if(typeof value==='number'){if(!Number.isFinite(value))throw Error('Nonfinite '+where);return Object.is(value,-0)?0:value;}
 if(ArrayBuffer.isView(value))return Array.from(value,(v,i)=>canonical(v,where+'['+i+']'));
 if(Array.isArray(value))return value.map((v,i)=>canonical(v,where+'['+i+']'));
 if(typeof value==='object'){const r={};for(const k of Object.keys(value).sort())r[k]=canonical(value[k],where+'.'+k);return r;}
 throw Error('Non-data '+typeof value+' at '+where);
}
const outputs=[];const written=new Set();
function write(name,data){
 if(written.has(name))throw Error('Duplicate output '+name);written.add(name);
 const dest=path.join(out,name);if(path.relative(out,dest).startsWith('..')||path.isAbsolute(name))throw Error('Unsafe output path');
 fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,data);
 outputs.push({path:name,bytes:Buffer.byteLength(data),sha256:sha(data)});return name;
}
function json(name,value){return write(name,JSON.stringify(canonical(value),null,2)+'\n');}
function binary64(name,value){return write(name,Buffer.from(value,'base64'));}
function crc32(data){let c=0xffffffff;for(const v of data){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
function u32(v){const b=Buffer.alloc(4);b.writeUInt32LE(v);return b;}
function encodeValue(v){
 if(v===null)return Buffer.from([0]);if(v===false)return Buffer.from([1]);if(v===true)return Buffer.from([2]);
 if(typeof v==='number'){const b=Buffer.alloc(9);b[0]=3;b.writeDoubleLE(v,1);return b;}
 if(typeof v==='string'){const b=Buffer.from(v);return Buffer.concat([Buffer.from([4]),u32(b.length),b]);}
 if(Array.isArray(v))return Buffer.concat([Buffer.from([5]),u32(v.length),...v.map(encodeValue)]);
 const keys=Object.keys(v).sort();return Buffer.concat([Buffer.from([6]),u32(keys.length),...keys.flatMap(k=>[encodeValue(k),encodeValue(v[k])])]);
}
function datafile(name,value){const payload=encodeValue(canonical(value));return write(name,Buffer.concat([Buffer.from('CATDATA1'),u32(1),u32(payload.length),u32(crc32(payload)),payload]));}
function mesh(name,input,stride){
 if(input.length%stride||input.length%(stride*3))throw Error('Invalid triangle stream '+name);
 const f=Float32Array.from(input);for(const v of f)if(!Number.isFinite(v))throw Error('Nonfinite mesh '+name);
 const unique=[],indices=[],seen=new Map();const bytes=Buffer.from(f.buffer);
 for(let i=0;i<f.length;i+=stride){const key=bytes.subarray(i*4,(i+stride)*4).toString('base64');let id=seen.get(key);
  if(id===undefined){id=unique.length/stride;seen.set(key,id);for(let k=0;k<stride;k++)unique.push(f[i+k]);}indices.push(id);}
 const verts=Buffer.alloc(unique.length*4),idx=Buffer.alloc(indices.length*4);
 unique.forEach((v,i)=>verts.writeFloatLE(v,i*4));indices.forEach((v,i)=>idx.writeUInt32LE(v,i*4));
 const body=Buffer.concat([verts,idx]);write(name,Buffer.concat([Buffer.from('CATMESH1'),u32(1),u32(stride),u32(unique.length/stride),u32(indices.length),u32(crc32(body)),body]));
 const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];for(let i=0;i<unique.length;i+=stride)for(let j=0;j<3;j++){min[j]=Math.min(min[j],unique[i+j]);max[j]=Math.max(max[j],unique[i+j]);}
 return {file:name,stride,vertices:unique.length/stride,indices:indices.length,triangles:indices.length/3,bounds:indices.length?[min,max]:null};
}
function context(){
 const auditConsole={...console,warn(...args){throw Error('Source warning: '+args.join(' '));}};
 const c={console:auditConsole,performance:{now:()=>0},atob:s=>Buffer.from(s,'base64').toString('binary'),Float32Array,Float64Array,Uint8Array,Uint8ClampedArray,Uint16Array,Uint32Array,Int32Array,ArrayBuffer,
 document:{createElement(type){if(type!=='canvas')throw Error('Unsupported DOM '+type);return createCanvas(1,1);}},
 Image:require('@napi-rs/canvas').Image};c.window=c;return vm.createContext(c);
}
function run(c,s,label){return vm.runInContext(s,c,{filename:label,timeout:120000});}
function block(c,n){return run(c,blocks[n-1],'legacy-block-'+n);}
function get(c,s){return run(c,s,'export');}
function normalizeWorld(W){
 const result={},consumed=[];
 const methods=new Set(['slab','ground','deck','roof','ramp','rampZ','solid','box','bandWall','segWall','wall','railing','cavity','footprint','region','light','spawn','pickup','pad','decor','note','patch','surface','mirror','portal','model','kit']);
 for(const [k,v] of Object.entries(W)){
  if(typeof v==='function'){if(!methods.has(k))throw Error('Unresolved world callback '+k);consumed.push(k);}
  else result[k]=canonical(v,'world.'+k);
 }
 return {data:result,consumedBuilderMethods:consumed.sort()};
}
function normalizeCompiled(W,source){
 const refs=new Map([[source,'authored']]);for(const [key,list] of Object.entries(source))if(Array.isArray(list))list.forEach((v,i)=>{if(v&&typeof v==='object')refs.set(v,`authored.${key}[${i}]`);});
 function copy(v,key=''){
  if(v===undefined)return null;
  if(key==='src'||key==='solidRef'||key==='source'){if(!refs.has(v))throw Error('Unresolved source link');return {$ref:refs.get(v)};}
  if(v===null||typeof v!=='object')return canonical(v);
  if(Array.isArray(v))return v.map(x=>copy(x));
  const o={};for(const k of Object.keys(v).sort())o[k]=copy(v[k],k);
  if(v.poly&&v.poly._b)o.bounds=canonical(v.poly._b);return o;
 }
 return copy(W);
}
async function main(){
 fs.mkdirSync(out,{recursive:true});if(fs.readdirSync(out).length)throw Error('Output directory must be empty (avoid stale assets)');
 const c=context();for(const n of [1,2,3,4,7,8,12,15,17,18,19,21,22,23,24,25,26,27,28,37,38,39,41])block(c,n);
 const embedded=get(c,'VOID_EMBEDDED_ASSETS');for(const [name,a] of Object.entries(embedded))binary64('embedded/'+name,a.data);
 const catalog={schema:1,coordinateSystem:'world: X-right Y-forward Z-up; model metadata retained',models:[],textures:[],audio:[],lightmaps:{},visibility:{},md3:{},materialScripts:[],warnings:[]};
 for(const [name,data] of Object.entries(get(c,'VOID_V318_SAMPLE_BANK'))){const file=binary64('audio/'+name.replace(/:/g,'_')+'.ogg',data);catalog.audio.push({id:name,file});}
 for(const [group,assets] of [['enemies',Object.values(get(c,'MESHY_ENEMIES'))],['weapons',get(c,'MESHY_ASSETS')]])for(const [i,a] of assets.entries()){
  const id=a.name||String(i),buf=Buffer.from(a.vertices,'base64');if(buf.length%48)throw Error('Mesh alignment '+id);
  const vertices=[];for(let j=0;j<buf.length;j+=4)vertices.push(buf.readFloatLE(j));
  const m=mesh(`models/${group}/${id}.cmesh`,vertices,12);const metadata={...a};delete metadata.vertices;delete metadata.textures;
  const textures=[];for(const [j,t] of (a.textures||[]).entries())textures.push(image64(`models/${group}/${id}-texture-${j}`,t));
  if(m.triangles!==a.triangles)throw Error('Triangle count mismatch '+id);catalog.models.push({group,id,...m,metadata,textures});
 }
 const kit=get(c,'VOID_KIT');
 for(const [id,a] of Object.entries(kit.assets)){
  const meta={...a};for(const key of ['pos','nrm','uv','idx']){const file=binary64(`models/kit/${id}.${key}.bin`,a[key]);meta[key]={file,componentType:key==='idx'?'uint16-le':'float32-le'};}
  json(`models/kit/${id}.json`,meta);catalog.models.push({group:'kit',id,metadata:`models/kit/${id}.json`});
 }
 catalog.kitGlowAssets=[];
 run(c,nestedFunction(16,'glowMask'),'source-glow-mask');
 for(const [name,t] of Object.entries(kit.textures)){const file=binary64(`textures/kit/${name.replace(/[^a-zA-Z0-9_.-]/g,'_')}.jpg`,t.data);catalog.textures.push({id:name,file,metadata:{...t,data:null}});
  if(['BA_24','BA_27','BA_28','BA_08','BA_30'].includes(name)){
   const im=await loadImage(Buffer.from(t.data,'base64')),cv=createCanvas(t.size,t.size),cx=cv.getContext('2d');cx.drawImage(im,0,0);c.glowPixels=cx.getImageData(0,0,t.size,t.size).data;
   const glow=get(c,"glowMask(glowPixels,'orange')");if(glow){catalog.kitGlowAssets.push(name);const glowFile=write(`textures/kit/${name}_glow.rgba`,Buffer.from(glow));catalog.textures.push({id:t.name+'_glow',file:glowFile,width:t.size,height:t.size,format:'RGBA8'});}
  }
 }

 for(const name of get(c,'Q3Tex.names()')){const pixels=get(c,`Q3Tex.generators[${JSON.stringify(name)}]()`);write('textures/procedural/'+name+'.rgba',Buffer.from(pixels));catalog.textures.push({id:name,file:'textures/procedural/'+name+'.rgba',width:256,height:256,format:'RGBA8',origin:'top-left'});}
 for(const [id,lm] of Object.entries(get(c,'VOID_LIGHTMAPS'))){const meta={...lm};meta.png=binary64(`lightmaps/${id}.png`,lm.png);catalog.lightmaps[id]=meta;}
 for(const [id,vis] of Object.entries(get(c,'VOID_VIS'))){const meta={...vis};meta.data=binary64(`visibility/${id}.bin`,vis.data);catalog.visibility[id]=meta;}
 for(const [id,a] of Object.entries(get(c,'VOID_MD3_ASSETS'))){catalog.md3[id]={...a,data:binary64(`models/md3/${id}.md3`,a.data)};}
 const rigs=get(c,'typeof VOID_MD3_RIGS!=="undefined"?VOID_MD3_RIGS:null');catalog.md3Rigs=rigs;
 // Execute original deterministic Canvas painters with upload calls intercepted, never WebGL rendering.
 c.gl={};for(const name of ['activeTexture','bindTexture','pixelStorei','texImage2D','texParameteri','useProgram','uniform1i','generateMipmap'])c.gl[name]=()=>{};
 c.gl.createTexture=()=>({});c.gl.getUniformLocation=()=>null;c.prog={};
 run(c,range(1196,1448),'legacy-atlas-painters');run(c,'const atlasTexture={};','offline-texture-handle');block(c,33);
 write('textures/world-atlas.png',get(c,'texCanvas').toBuffer('image/png'));
 run(c,range(1508,1838),'legacy-weapon-projectile-painters');
 for(const canvasName of ['nailTexCanvas','shotgunTexCanvas','grenadeTexCanvas','railTexCanvas','rocketTexCanvas','projectileAtlasCanvas']){
  const file=write('textures/'+canvasName+'.png',get(c,canvasName).toBuffer('image/png'));catalog.textures.push({id:canvasName,file,width:512,height:512});
 }

 catalog.textures.push({id:'$world',file:'textures/world-atlas.png',width:512,height:512,tile:128,origin:'top-left',uvConvention:'legacy UNPACK_FLIP_Y_WEBGL; native loader must flip V when sampling top-left pixels'});
 block(c,9);write('textures/fx-atlas.png',get(c,'FX_ATLAS.canvas').toBuffer('image/png'));json('textures/fx-tiles.json',get(c,'FX_TILE'));
 for(const [name,tile,k] of [['sky/rb_clouds_a',51,1],['sky/rb_clouds_b',52,.38/.55]]){
  const cv=createCanvas(256,256),cx=cv.getContext('2d');cx.drawImage(get(c,'FX_ATLAS.canvas'),tile%8*128,Math.floor(tile/8)*128,128,128,0,0,256,256);
  const px=cx.getImageData(0,0,256,256).data;for(let i=3;i<px.length;i+=4)px[i]=Math.round(px[i]*k);
  const file=write('textures/'+name+'.rgba',Buffer.from(px));catalog.textures.push({id:name,file,width:256,height:256,format:'RGBA8'});
 }
 // Raw material scripts and parsed stage data are both retained.
 for(const script of get(c,'VOID_SHADER_SCRIPTS')){write('materials/'+script.file,script.text);catalog.materialScripts.push('materials/'+script.file);}
 const gameplayNames={20:['WEAPONS','BASE_WALK_SPEED','CROUCH_MULT','SPRINT_MULT','SLIDE_DURATION','SLIDE_COOLDOWN','SLIDE_BOOST','MOVEMENT_PHYSICS','PLAYER_RADIUS','PLAYER_HEIGHT','PLAYER_CROUCH_HEIGHT','GRAVITY','ENEMY_SLOW_SPEED','ENEMY_STANDARD_SPEED','ENEMY_FAST_SPEED'],47:['FLYING_STATS','ENEMY_STATS','ENEMY_TYPES']};
 const gc=context(),gameplay={schema:1,source:'primary declarations; not executed native gameplay'};
 for(const [b,names] of Object.entries(gameplayNames))for(const name of names){run(gc,declarationSource(Number(b),name),'gameplay-data-'+name);gameplay[name]=get(gc,name);}
 json('gameplay/constants.json',gameplay);datafile('gameplay/constants.cadata',gameplay);catalog.gameplay='gameplay/constants.cadata';
 json('catalog.json',catalog);
 for(const id of ['helios-crucible','bastion-keep','reactor-bastion','q3-gallery'])await bakeMap(id,catalog);
 json('source-modules.json',blocks.map((b,i)=>({block:i+1,sha256:sha(b),bytes:Buffer.byteLength(b)})));
 json('conversion-manifest.json',{schema:1,tool:'cat-arena-n1',sourceSha256:expected,profile:{curveDetail:1,worldUV:true,canvas:'@napi-rs/canvas 0.1.100',notes:'Offline CPU execution; no gameplay or render validation'},files:outputs.slice().sort((a,b)=>a.path.localeCompare(b.path,'en'))});
 console.log('BAKE PASS',outputs.length,'files');
}
async function bakeMap(id,catalog){
 const c=context();for(const n of [7,8,12,15,16,21,22,23,24,25,26,28])block(c,n);
 run(c,`const SOURCE=VOID_MAP_REGISTRY[${JSON.stringify(id)}](WorldKit);const COMPILED=compileWorld(SOURCE);const WORLD=COMPILED.WORLD;const LEVEL_V2=COMPILED.LEVEL;`,'build-map');
 // Native-friendly resolved data; builders have already consumed closure-based cuts.
 const authored=normalizeWorld(get(c,'SOURCE'));const compiled=normalizeCompiled(get(c,'WORLD'),get(c,'SOURCE'));
 const map={schema:1,id,wireId:id+'-v1',build:'VA-MODULAR-RB-011',authored:authored.data,compiled,level:canonical(get(c,'LEVEL_V2')),builderMethodsConsumed:authored.consumedBuilderMethods};
 block(c,13);for(const s of get(c,'VOID_SHADER_SCRIPTS')){c.script=s;run(c,'Q3MAT.loadScript(script.text,script.file)','material-load');}
 // Prepare real kit material declarations from original script() without image/GPU initialization.
 const scriptFunction=nestedFunction(16,'script');
 c.kit=get(c,'VOID_KIT');c.glowIds=catalog.kitGlowAssets||[];
 run(c,`{const B=kit;const info={_glow:new Set(glowIds)};${scriptFunction};Q3MAT.loadScript(script(),'scripts/bastion-kit.shader');}`,'kit-materials');
 // Register actual pixel arrays to preserve source material texture namespace/type.
 for(const [asset,t] of Object.entries(c.kit.textures)){
  c.texName=t.name;c.texSize=t.size;
  run(c,'Q3MAT.registerKitImage(texName,new Uint8Array(0),texSize)','register-texture-metadata');
  if(c.glowIds.includes(asset)){c.texName=t.name+'_glow';run(c,'Q3MAT.registerKitImage(texName,new Uint8Array(0),texSize)','register-glow-metadata');}
 }
 run(c,'let bakingWorldUV=true;const dynamicScratch=[];const WORLD_EMPTY=[];const SLAB_Q=[];const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));const mult=(c,n)=>c.map(x=>clamp(x*n,0,1));const TAU=Math.PI*2;','cpu-geometry-context');
 run(c,range(1196,1196),'atlas-ids');
 for(const name of ['pushTri','quad','cube','fixWinding'])run(c,functionSource(20,name),name);
 for(const name of ['levelInside','levelInsideOrEdge','worldCell','worldSlabsAt','slabQuery','worldInFootprint','worldInMassive','levelWalkable','levelFeatureSolidAt','levelFloorSurfacesAt','worldSlabZ','worldSlabThickness'])run(c,functionSource(30,name),name);
 block(c,32);block(c,34);
 run(c,'const G=[],SKY=[];levelBuildVisual(G,SKY);fixWinding(G);fixWinding(SKY);','bake-visible-geometry');
 // Chart builder is CPU-only; this preserves secondary UV association instead of orphaning atlas PNGs.
 block(c,6);block(c,14);
 run(c,`const SK=WORLD.meta.sky||{};const LM=Q3LM.prepare(G,Q3MAT.pendingSurfaces(),{lights:LEVEL_V2.fixtures.map(l=>({x:l.x,y:l.y,z:l.z,range:l.range,strength:l.power,warm:l.warm,kind:l.kind,color:l.color})),sun:{az:SK.sunAzimuth??-.62,el:SK.sunElevation??.10}});`,'chart-lightmaps');
 map.render={world:mesh(`maps/${id}/world.cmesh`,get(c,'LM.g14'),14),sky:mesh(`maps/${id}/sky.cmesh`,get(c,'SKY'),12),materials:[],materialMesh:null};
 const pending=get(c,'Q3MAT.pendingSurfaces()');const combined=[];
 for(const p of pending){const first=combined.length/18;for(const v of p.v)combined.push(v);map.render.materials.push({material:p.m.name,firstIndex:first,indexCount:p.v.length/18,origin:p.origin?canonical(p.origin):null,lightmapParam:p.lmParam?canonical(p.lmParam):null});}
 map.render.materialMesh=mesh(`maps/${id}/materials.cmesh`,combined,18);
 map.lightmap={...catalog.lightmaps[id],computedHash:get(c,'LM.hash'),computedWidth:get(c,'LM.W'),computedHeight:get(c,'LM.H')};
 map.lightmap.compatible=map.lightmap.hash===map.lightmap.computedHash;
 if(!map.lightmap.compatible)console.warn('LIGHTMAP MISMATCH',id,map.lightmap.hash,map.lightmap.computedHash);
 map.materialDefinitions=canonical([...get(c,'Q3MAT.materials').values()]);
 map.navigation={kind:'authored-and-compiled-metadata',connections:map.level.connections,regions:map.level.sectors,walkableSlabIndices:compiled.slabs.map((s,i)=>s.nav?i:-1).filter(i=>i>=0),runtimeAStarGraphBaked:false};
 json(`maps/${id}/map.json`,map);datafile(`maps/${id}/map.cadata`,map);
 console.log('MAP',id,map.render.world.triangles,map.render.materialMesh.triangles,compiled.walls.length,'walls');
}
if(require.main===module)main().catch(e=>{console.error(e.stack);process.exitCode=1;});
module.exports={bakeMap,canonical,encodeValue,crc32};
