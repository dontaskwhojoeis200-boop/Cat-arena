'use strict';
// VOID ARENA v3.24 four-player co-op relay for Node.js 20+.
// One browser hosts the authoritative simulation; this server manages rooms
// and relays game packets. All clients in a room must use the same deployment.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {WebSocketServer, WebSocket} = require('ws');

const PORT = Number(process.env.PORT) || 8787;
const HOST = process.env.HOST || '0.0.0.0';
const GAME_FILE = path.join(__dirname, 'game.html');
const ROOM_SIZE = 4;
const BUILD_ID='VA-MODULAR-RB-011';
const MAP_IDS=['helios-crucible-v1','bastion-keep-v1','reactor-bastion-v1','q3-gallery-v1'];
function validMap(m) { return typeof m==='string' && MAP_IDS.includes(m); }
function validBuild(b) { return b===BUILD_ID; }
const MAX_ROOMS = 100;
const MAX_PEERS = 410;
const ROOM_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const rooms = new Map();
const peers = new Set();
function code6() {
  let code;
  do {code=Array.from({length:6},()=>ROOM_ALPHABET[crypto.randomInt(ROOM_ALPHABET.length)]).join('');}
  while (rooms.has(code));
  return code;
}
function send(ws, data) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  try {ws.send(JSON.stringify(data));} catch (_) { /* socket closing */ }
}
function nameOf(x) {return String(x || 'Marine Cat').replace(/[^\w .-]/g,'').trim().slice(0,20)||'Marine Cat';}
function titleOf(x,host) {return String(x || (host+'\'s Server')).replace(/[^\w .-]/g,'').trim().slice(0,32)||'Blackstone Keep';}
function member(ws) {return {id:ws.id,name:ws.name};}
function members(room) {return [room.host,...room.guests.values()];}
function roster(room) {
  const data={t:'roster',code:room.code,players:members(room).map(member),maxPlayers:ROOM_SIZE};
  for(const ws of members(room))send(ws,data);
}
function notify(room,event,peer,exclude=null) {
  for(const ws of members(room))if(ws!==exclude)send(ws,{t:'peer',event,peer});
}
const server=http.createServer((req,res)=>{
  if(req.method!=='GET'&&req.method!=='HEAD'){
    res.writeHead(405,{'Content-Type':'text/plain'});res.end('Method not allowed');return;
  }
  const route=(req.url||'/').split('?')[0];
  if(route==='/rooms'){
    // Read-only, discoverable lobby directory; the internal join ID is never
    // displayed in the client interface. No invented or offline rooms.
    const live=[...rooms.values()].filter(r=>r.host.readyState===WebSocket.OPEN)
      .map(room=>({id:room.code,name:room.title,title:room.title,
        host:room.host.name,players:room.guests.size+1,maxPlayers:ROOM_SIZE,
        open:room.guests.size<ROOM_SIZE-1,createdAt:room.createdAt,
        build:room.build,mapId:room.mapId}));
    const body=JSON.stringify({version:1,game:'CAT ARENA',build:BUILD_ID,
      mapIds:MAP_IDS,rooms:live,maxPlayers:ROOM_SIZE});
    res.writeHead(200,{'Content-Type':'application/json; charset=utf-8',
      'Cache-Control':'no-store','Access-Control-Allow-Origin':'*',
      'X-Content-Type-Options':'nosniff'});
    res.end(req.method==='HEAD'?undefined:body);return;
  }
  if(route==='/health'){
    const body=JSON.stringify({status:'ok',game:'CAT ARENA v0.17',maxPlayersPerRoom:ROOM_SIZE,rooms:rooms.size,players:peers.size});
    res.writeHead(200,{'Content-Type':'application/json','Cache-Control':'no-store'});
    res.end(req.method==='HEAD'?undefined:body);return;
  }
  if(route!=='/'&&route!=='/index.html'){
    res.writeHead(404,{'Content-Type':'text/plain'});res.end('Not found');return;
  }
  fs.stat(GAME_FILE,(err,stat)=>{
    if(err||!stat.isFile()){
      res.writeHead(500,{'Content-Type':'text/plain'});
      res.end('index.html missing next to server.js');return;
    }
    res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Length':stat.size,
      'Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});
    if(req.method==='HEAD'){res.end();return;}
    const stream=fs.createReadStream(GAME_FILE);
    stream.on('error',()=>res.destroy());stream.pipe(res);
  });
});
const wss=new WebSocketServer({noServer:true,maxPayload:128*1024,perMessageDeflate:false});
server.on('upgrade',(req,socket,head)=>{
  const route=(req.url||'/').split('?')[0];
  if(route!=='/ws'){socket.write('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');socket.destroy();return;}
  wss.handleUpgrade(req,socket,head,ws=>wss.emit('connection',ws,req));
});
function disconnect(ws){
  peers.delete(ws);
  const room=ws.room;ws.room=null;
  if(!room)return;
  if(room.host===ws){
    rooms.delete(room.code);
    for(const guest of room.guests.values()){
      guest.room=null;
      send(guest,{t:'peer',event:'host_left'});
    }
    room.guests.clear();
    console.log('Host closed room',room.code);
  }else if(room.guests.get(ws.id)===ws){
    room.guests.delete(ws.id);
    notify(room,'guest_left',member(ws));
    roster(room);
    console.log('Guest left',room.code,room.guests.size+1+'/'+ROOM_SIZE);
  }
}
function action(ws,msg){
  if(!msg||typeof msg!=='object'||Array.isArray(msg))return;
  if(msg.t==='create'){
    if(ws.room){send(ws,{t:'error',message:'Already in a room.'});return;}
    if(rooms.size>=MAX_ROOMS){send(ws,{t:'error',message:'Server is at capacity.'});return;}
    if(!validBuild(msg.build)||!validMap(msg.mapId)){
      send(ws,{t:'error',message:'Client build or map is incompatible with this Cat Arena server.'});return;
    }
    ws.name=nameOf(msg.name);ws.id=crypto.randomBytes(9).toString('hex');
    const room={code:code6(),host:ws,guests:new Map(),
      title:titleOf(msg.title,ws.name),createdAt:Date.now(),
      build:msg.build,mapId:msg.mapId};
    ws.room=room;
    rooms.set(room.code,room);
    send(ws,{t:'ready',role:'host',code:room.code,id:ws.id,peer:null,players:[member(ws)],slot:0,maxPlayers:ROOM_SIZE});
    roster(room);console.log('Room created',room.code,'1/'+ROOM_SIZE);return;
  }
  if(msg.t==='join'){
    if(ws.room){send(ws,{t:'error',message:'Already in a room.'});return;}
    const code=String(msg.code||'').toUpperCase().replace(/[^A-Z0-9]/g,'').slice(0,6);
    const room=rooms.get(code);
    if(!room){send(ws,{t:'error',message:'That server is no longer online. Refresh the server list and select another server.'});return;}
    if(room.host.readyState!==WebSocket.OPEN){send(ws,{t:'error',message:'Host disconnected; create a new room.'});return;}
    if(room.guests.size>=ROOM_SIZE-1){send(ws,{t:'error',message:'That server is full (4/4). Refresh the server list.'});return;}
    if(!validBuild(msg.build)||msg.build!==room.build||!validMap(msg.mapId)||msg.mapId!==room.mapId){
      send(ws,{t:'error',message:'Client version or selected map does not match the host.'});return;
    }
    ws.room=room;ws.name=nameOf(msg.name);ws.id=crypto.randomBytes(9).toString('hex');
    let slot=1;const taken=new Set([...room.guests.values()].map(p=>p.slot));
    while(taken.has(slot)&&slot<ROOM_SIZE)slot++;
    ws.slot=slot;
    room.guests.set(ws.id,ws);
    send(ws,{t:'ready',role:'guest',code:room.code,id:ws.id,peer:member(room.host),
      players:members(room).map(member),slot,maxPlayers:ROOM_SIZE});
    notify(room,'guest_joined',member(ws),ws);
    roster(room);
    console.log('Guest joined',room.code,(room.guests.size+1)+'/'+ROOM_SIZE);
    return;
  }
  if(msg.t==='relay'){
    const room=ws.room;
    if(!room||!msg.data||typeof msg.data!=='object'||Array.isArray(msg.data))return;
    if(room.host===ws){
      const to=typeof msg.to==='string'?msg.to:null;
      if(to){const peer=room.guests.get(to);if(peer)send(peer,{t:'relay',from:ws.id,data:msg.data});}
      else for(const peer of room.guests.values())send(peer,{t:'relay',from:ws.id,data:msg.data});
    }else if(room.guests.get(ws.id)===ws){
      send(room.host,{t:'relay',from:ws.id,data:msg.data});
    }
  }
}
wss.on('connection',ws=>{
  if(peers.size>=MAX_PEERS){send(ws,{t:'error',message:'Server is full.'});ws.close(1013,'Server full');return;}
  peers.add(ws);ws.room=null;ws.id='';ws.isAlive=true;
  ws.on('pong',()=>{ws.isAlive=true;});
  ws.on('message',data=>{
    try{if(data.length>128*1024)return;action(ws,JSON.parse(data.toString()));}
    catch(_){send(ws,{t:'error',message:'Invalid game message.'});}
  });
  ws.on('close',()=>disconnect(ws));ws.on('error',()=>{});
});
const heartbeat=setInterval(()=>{
  for(const ws of peers){
    if(!ws.isAlive){ws.terminate();continue;}
    ws.isAlive=false;
    try{ws.ping();}catch(_){ws.terminate();}
  }
},30000);
heartbeat.unref?.();
server.listen(PORT,HOST,()=>{
  console.log(`CAT ARENA v0.17 co-op (up to ${ROOM_SIZE} players) listening on ${HOST}:${PORT}`);
  console.log(`Health check: http://127.0.0.1:${PORT}/health`);
  console.log('Open the public HTTPS game URL on every player device.');
});
