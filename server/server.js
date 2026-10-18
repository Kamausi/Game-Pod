#!/usr/bin/env node
// Mud Buddies game server - zero dependencies (Node >= 18).
//   * Serves the web build (../web)
//   * WebSocket (RFC 6455) lobby + 20 Hz snapshot relay at /ws
//   * Leaderboards REST API backed by a JSON file
// Usage: node server/server.js [--port 8787]
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const PORT = +(process.env.PORT || argValue('--port') || 8787);
const WEB_ROOT = path.resolve(__dirname, '..', 'web');
const DATA_DIR = path.resolve(process.env.MB_DATA_DIR || path.join(__dirname, 'data'));
const BOARD_FILE = path.join(DATA_DIR, 'leaderboards.json');
const MAX_ROOM = 8;
const SNAPSHOT_TYPE = 0x51, SNAPSHOT_SIZE = 96;

function argValue(flag) { const i = process.argv.indexOf(flag); return i > 0 ? process.argv[i + 1] : undefined; }
const now = () => Date.now() / 1000;

// ------------------------------------------------------------------------------------------
// Leaderboards. Plausibility limits derived from the sim (80 m pit, ~35 m/s top speed, etc.).
const BOARDS = {
  bog_time: { lower: true, min: 2.3, max: 180 },
  bog_distance: { lower: false, min: 0, max: 80.01 },
  circuit_race: { lower: true, min: 60, max: 3600 },
  circuit_lap: { lower: true, min: 20, max: 1200 },
  freestyle_score: { lower: false, min: 0, max: 2e6 },
  king_time: { lower: false, min: 0, max: 120.5 },
  bounty_time: { lower: true, min: 2.0, max: 600 },
  bounty_distance: { lower: false, min: 0, max: 32.01 },
};
let boards = {};
try { boards = JSON.parse(fs.readFileSync(BOARD_FILE, 'utf8')); } catch { boards = {}; }
let saveTimer = null;
function saveBoards() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    fs.mkdirSync(DATA_DIR, { recursive: true });
    const tmp = BOARD_FILE + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(boards));
    fs.renameSync(tmp, BOARD_FILE);
  }, 250);
}
const cleanName = (s, n) => String(s || '').replace(/[^\w .'\-#"]/g, '').slice(0, n).trim() || 'Buddy';
function submitScore(e, ip) {
  const spec = BOARDS[e.board];
  if (!spec) return { error: 'unknown board' };
  const v = Number(e.value);
  if (!Number.isFinite(v) || v < spec.min || v > spec.max) return { error: 'implausible result' };
  const entry = { name: cleanName(e.name, 16), rig: cleanName(e.rig, 48), value: Math.round(v * 1000) / 1000, at: Date.now(), ip: hashIp(ip) };
  const list = (boards[e.board] = boards[e.board] || []);
  list.push(entry);
  list.sort((a, b) => (spec.lower ? a.value - b.value : b.value - a.value));
  boards[e.board] = list.slice(0, 100);
  saveBoards();
  const rank = boards[e.board].indexOf(entry) + 1;
  return { ok: true, rank: rank || null };
}
function hashIp(ip) { return crypto.createHash('sha256').update(String(ip) + 'mud').digest('hex').slice(0, 10); }
const lastPost = new Map();

// ------------------------------------------------------------------------------------------
// HTTP
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
  '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.data': 'application/octet-stream' };

function sendJson(res, code, obj) {
  res.writeHead(code, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'Content-Type' });
  res.end(JSON.stringify(obj));
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (req.method === 'OPTIONS') return sendJson(res, 204, {});
  if (url.pathname === '/api/health') return sendJson(res, 200, { ok: true, rooms: rooms.size, clients: clients.size, time: now() });
  if (url.pathname === '/api/leaderboards') {
    const out = {};
    for (const k of Object.keys(BOARDS)) out[k] = (boards[k] || []).slice(0, 20).map(({ ip, ...r }) => r);
    return sendJson(res, 200, out);
  }
  const m = /^\/api\/leaderboards\/(\w+)$/.exec(url.pathname);
  if (m) {
    if (!BOARDS[m[1]]) return sendJson(res, 404, { error: 'unknown board' });
    return sendJson(res, 200, (boards[m[1]] || []).slice(0, 50).map(({ ip, ...r }) => r));
  }
  if (url.pathname === '/api/score' && req.method === 'POST') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 4096) req.destroy(); });
    req.on('end', () => {
      const ip = req.socket.remoteAddress;
      const t = Date.now();
      if (t - (lastPost.get(ip) || 0) < 1500) return sendJson(res, 429, { error: 'slow down' });
      lastPost.set(ip, t);
      let e; try { e = JSON.parse(body); } catch { return sendJson(res, 400, { error: 'bad json' }); }
      const r = submitScore(e, ip);
      sendJson(res, r.error ? 400 : 200, r);
    });
    return;
  }
  // Static files
  let p = decodeURIComponent(url.pathname);
  if (p.endsWith('/')) p += 'index.html';
  const file = path.resolve(WEB_ROOT, '.' + p);
  if (!file.startsWith(WEB_ROOT + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404, { 'Content-Type': 'text/plain' }); return res.end('not found'); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    res.end(data);
  });
});

// ------------------------------------------------------------------------------------------
// Minimal RFC 6455 WebSocket
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
class Socket {
  constructor(sock) {
    this.sock = sock; this.buf = Buffer.alloc(0); this.frag = []; this.fragOp = 0; this.open = true;
    this.onmessage = () => {}; this.onclose = () => {};
    sock.on('data', (d) => this.feed(d));
    sock.on('close', () => this.closed());
    sock.on('error', () => this.closed());
  }
  feed(d) {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 2) return;
      const b0 = this.buf[0], b1 = this.buf[1];
      const fin = (b0 & 0x80) !== 0, op = b0 & 0x0f, masked = (b1 & 0x80) !== 0;
      let len = b1 & 0x7f, off = 2;
      if (len === 126) { if (this.buf.length < 4) return; len = this.buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (this.buf.length < 10) return; len = Number(this.buf.readBigUInt64BE(2)); off = 10; }
      if (len > 65536) return this.close(1009);
      if (!masked) return this.close(1002);    // clients must mask
      if (this.buf.length < off + 4 + len) return;
      const mask = this.buf.subarray(off, off + 4);
      const payload = Buffer.from(this.buf.subarray(off + 4, off + 4 + len));
      for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      this.buf = this.buf.subarray(off + 4 + len);
      if (op === 0x8) return this.close(1000);
      if (op === 0x9) { this.sendFrame(0xA, payload); continue; }
      if (op === 0xA) continue;
      if (op === 0x0) { this.frag.push(payload); if (fin) { this.deliver(this.fragOp, Buffer.concat(this.frag)); this.frag = []; } continue; }
      if (!fin) { this.fragOp = op; this.frag = [payload]; continue; }
      this.deliver(op, payload);
    }
  }
  deliver(op, payload) { this.onmessage(op === 0x1 ? payload.toString('utf8') : payload); }
  sendFrame(op, data) {
    if (!this.open) return;
    const len = data.length;
    let head;
    if (len < 126) head = Buffer.from([0x80 | op, len]);
    else if (len < 65536) { head = Buffer.alloc(4); head[0] = 0x80 | op; head[1] = 126; head.writeUInt16BE(len, 2); }
    else { head = Buffer.alloc(10); head[0] = 0x80 | op; head[1] = 127; head.writeBigUInt64BE(BigInt(len), 2); }
    this.sock.write(Buffer.concat([head, data]));
  }
  send(data) { if (typeof data === 'string') this.sendFrame(0x1, Buffer.from(data)); else this.sendFrame(0x2, data); }
  close(code) {
    if (!this.open) return;
    const b = Buffer.alloc(2); b.writeUInt16BE(code || 1000);
    try { this.sendFrame(0x8, b); } catch { /* ignore */ }
    this.sock.end();
    this.closed();
  }
  closed() { if (!this.open) return; this.open = false; this.onclose(); }
}

server.on('upgrade', (req, sock) => {
  const url = new URL(req.url, 'http://x');
  const key = req.headers['sec-websocket-key'];
  if (url.pathname !== '/ws' || !key) { sock.destroy(); return; }
  const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
  sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  sock.setNoDelay(true);
  onClient(new Socket(sock), req.socket.remoteAddress);
});

// ------------------------------------------------------------------------------------------
// Lobby / rooms
const clients = new Map();
const rooms = new Map();
let nextId = 1;

function roomCode() {
  const A = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  for (;;) { let c = ''; for (let i = 0; i < 5; i++) c += A[crypto.randomInt(A.length)]; if (!rooms.has(c)) return c; }
}
function roomState(r) {
  return { t: 'room', code: r.code, host: r.host, mode: r.mode,
    players: [...r.players.values()].map((c) => ({ id: c.id, name: c.name, cfg: c.cfg, lane: c.lane, cfgChanged: c.cfgChanged })) };
}
function broadcastRoom(r) {
  const msg = JSON.stringify(roomState(r));
  for (const c of r.players.values()) { c.ws.send(msg); c.cfgChanged = false; }
}
function leaveRoom(c) {
  const r = c.room && rooms.get(c.room);
  c.room = null;
  if (!r) return;
  r.players.delete(c.id);
  if (r.players.size === 0) { rooms.delete(r.code); return; }
  if (r.host === c.id) r.host = r.players.keys().next().value;
  assignLanes(r);
  broadcastRoom(r);
}
function assignLanes(r) { let i = 0; for (const c of r.players.values()) c.lane = i++ % 4; }
function joinRoom(c, code) {
  leaveRoom(c);
  let r = rooms.get(code);
  if (!r) { r = { code, host: c.id, players: new Map(), mode: 1 }; rooms.set(code, r); }
  if (r.players.size >= MAX_ROOM) { c.ws.send(JSON.stringify({ t: 'error', msg: 'Room is full' })); return; }
  r.players.set(c.id, c);
  c.room = code;
  c.cfgChanged = true;
  assignLanes(r);
  broadcastRoom(r);
}

function onClient(ws, ip) {
  const c = { id: (nextId++ % 65000) + 1, ws, ip, name: 'Buddy', cfg: '', room: null, lane: 0, bins: 0, binWindow: now(), cfgChanged: true };
  clients.set(c.id, c);
  ws.send(JSON.stringify({ t: 'welcome', id: c.id, serverTime: now() }));
  ws.onclose = () => { leaveRoom(c); clients.delete(c.id); };
  ws.onmessage = (data) => {
    if (typeof data !== 'string') return relaySnapshot(c, data);
    let m; try { m = JSON.parse(data); } catch { return; }
    switch (m.t) {
      case 'hello': c.name = cleanName(m.name, 16); c.cfg = String(m.cfg || '').slice(0, 400); break;
      case 'cfg': {
        c.cfg = String(m.cfg || '').slice(0, 400); if (m.name) c.name = cleanName(m.name, 16); c.cfgChanged = true;
        const r = rooms.get(c.room); if (r) broadcastRoom(r);
        break;
      }
      case 'ping': ws.send(JSON.stringify({ t: 'pong', c: Number(m.c) || 0, s: now() })); break;
      case 'quick': {
        const open = [...rooms.values()].find((r) => r.players.size < MAX_ROOM && r.quick);
        if (open) joinRoom(c, open.code);
        else { const code = roomCode(); joinRoom(c, code); rooms.get(code).quick = true; }
        break;
      }
      case 'join': {
        const code = String(m.room || '').toUpperCase().replace(/[^A-Z0-9]/g, '').slice(0, 5) || roomCode();
        joinRoom(c, code);
        break;
      }
      case 'leave': leaveRoom(c); break;
      case 'start': {
        const r = rooms.get(c.room);
        if (!r || r.host !== c.id) return;
        r.mode = [0, 1, 2, 3, 4].includes(+m.mode) ? +m.mode : 1;
        const lanes = {};
        for (const p of r.players.values()) lanes[p.id] = p.lane;
        const msg = JSON.stringify({ t: 'start', mode: r.mode, startAt: now() + 4.5, lanes });
        for (const p of r.players.values()) p.ws.send(msg);
        break;
      }
      case 'chat': {
        const r = rooms.get(c.room); if (!r) return;
        const msg = JSON.stringify({ t: 'chat', from: c.name, msg: String(m.msg || '').slice(0, 120) });
        for (const p of r.players.values()) p.ws.send(msg);
        break;
      }
    }
  };
}

function relaySnapshot(c, buf) {
  const r = rooms.get(c.room);
  if (!r || buf.length !== SNAPSHOT_SIZE || buf[0] !== SNAPSHOT_TYPE) return;
  const t = now();
  if (t - c.binWindow > 1) { c.binWindow = t; c.bins = 0; }
  if (++c.bins > 40) return;          // cap at 40 Hz per client
  buf.writeUInt16LE(c.id, 2);          // anti-spoof: sender id is authoritative server-side
  for (const p of r.players.values()) if (p.id !== c.id) p.ws.send(buf);
}

if (require.main === module) {
  server.listen(PORT, () => console.log(`Mud Buddies server on http://localhost:${PORT}  (ws: /ws)`));
}
module.exports = { server, submitScore, BOARDS };
