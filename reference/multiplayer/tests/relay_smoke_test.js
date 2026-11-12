'use strict';
// Protocol-level smoke test for the Cat Arena relay without external npm deps.
const fs=require('fs'), os=require('os'), path=require('path'), cp=require('child_process');
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'cat-arena-relay-'));
fs.mkdirSync(path.join(tmp,'node_modules','ws'),{recursive:true});
fs.writeFileSync(path.join(tmp,'node_modules','ws','index.js'),`
const {EventEmitter}=require('events');
class WebSocket extends EventEmitter {static OPEN=1;constructor(){super();this.readyState=1;this.output=[];}send(o){this.output.push(JSON.parse(o));}ping(){}close(){this.readyState=3;this.emit('close')}terminate(){this.close()}}
class WebSocketServer extends EventEmitter{constructor(){super();module.exports.latest=this;}handleUpgrade(){}}
module.exports={WebSocketServer,WebSocket};
`);
const relayDir=path.join(tmp,'app');fs.mkdirSync(relayDir);
fs.copyFileSync(path.join(__dirname,'../relay/server.js'),path.join(relayDir,'server.js'));
fs.symlinkSync(path.join(tmp,'node_modules'),path.join(relayDir,'node_modules'));
fs.writeFileSync(path.join(relayDir,'game.html'),'<html>test</html>');
const p=cp.spawn(process.execPath,['-e',`
 const assert=require('assert');
 const {WebSocketServer,WebSocket}=require('ws');
 process.env.PORT='19284';require('./server.js');
 setTimeout(async()=>{
  const mod=require('ws'), wss=mod.latest;
  const make=()=>{let ws=new WebSocket();wss.emit('connection',ws);return ws};
  let host=make(),guest=make(),bad=make();
  host.emit('message',Buffer.from(JSON.stringify({t:'create',name:'Host',title:'Test Lobby',build:'VA-MODULAR-RB-011',mapId:'helios-crucible-v1'})));
  assert.equal(host.output[0].t,'ready');const code=host.output[0].code;
  const res=await fetch('http://localhost:19284/rooms');let data=await res.json();
  assert.equal(data.version,1);assert.equal(data.build,'VA-MODULAR-RB-011');assert.equal(data.rooms.length,1);assert.equal(data.rooms[0].title,'Test Lobby');
  guest.emit('message',Buffer.from(JSON.stringify({t:'join',code,name:'Guest',build:'VA-MODULAR-RB-011',mapId:'helios-crucible-v1'})));
  assert.equal(guest.output[0].t,'ready');assert.equal(guest.output[0].role,'guest');
  bad.emit('message',Buffer.from(JSON.stringify({t:'join',code,name:'Wrong map',build:'VA-MODULAR-RB-011',mapId:'reactor-bastion-v1'})));
  assert.equal(bad.output[0].t,'error');
  guest.emit('message',Buffer.from(JSON.stringify({t:'relay',data:{type:'pose',x:12,y:3}})));
  assert.ok(host.output.find(x=>x.t==='relay'&&x.data.x===12));
  const health=await(await fetch('http://localhost:19284/health')).json();assert.equal(health.status,'ok');
  console.log('PASS: create, room discovery, join, incompatible-map rejection, relay, health');process.exit(0);
 },100);
`],{cwd:relayDir,env:{...process.env,PORT:'19284'},stdio:['ignore','pipe','pipe']});
let output='';p.stdout.on('data',d=>output+=d);p.stderr.on('data',d=>output+=d);
p.on('exit',code=>{console.log(output);fs.rmSync(tmp,{recursive:true,force:true});process.exit(code||0)});
setTimeout(()=>{p.kill('SIGTERM')},6000).unref();
