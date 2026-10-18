// Integration test for server/server.js using Node's built-in WebSocket client (Node >= 22).
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';

const port = 18000 + Math.floor(Math.random() * 1000);
const data = fs.mkdtempSync(path.join(os.tmpdir(), 'mb-'));
const srv = spawn(process.execPath, ['server/server.js', '--port', String(port)], { env: { ...process.env, MB_DATA_DIR: data }, stdio: 'inherit' });
let failed = 0;
const check = (c, m) => { if (!c) { failed++; console.log('  FAIL', m); } else console.log('  ok  ', m); };
await sleep(400);
try {
  const base = `http://localhost:${port}`;
  check((await (await fetch(`${base}/api/health`)).json()).ok, 'health');
  check((await fetch(`${base}/index.html`)).status === 200, 'serves web build');
  check((await fetch(`${base}/../server/server.js`)).status !== 200, 'no path traversal');
  const post = (b) => fetch(`${base}/api/score`, { method: 'POST', body: JSON.stringify(b) });
  const r1 = await (await post({ board: 'bog_distance', value: 51.2, name: 'Tester<script>', rig: 'Comanche' })).json();
  check(r1.ok && r1.rank === 1, 'score accepted with rank');
  await sleep(1600);
  check((await post({ board: 'bog_time', value: 0.4, name: 'Cheater' })).status === 400, 'implausible time rejected');
  const lb = await (await fetch(`${base}/api/leaderboards/bog_distance`)).json();
  check(lb[0].name === 'Testerscript' && !('ip' in lb[0]), 'names sanitized, ip hidden');

  const open = (name) => new Promise((res) => {
    const ws = new WebSocket(`ws://localhost:${port}/ws`);
    ws.binaryType = 'arraybuffer';
    ws.msgs = []; ws.bins = [];
    ws.onmessage = (e) => (typeof e.data === 'string' ? ws.msgs.push(JSON.parse(e.data)) : ws.bins.push(new Uint8Array(e.data)));
    ws.onopen = () => { ws.send(JSON.stringify({ t: 'hello', name, cfg: 'body=comanche' })); res(ws); };
  });
  const a = await open('Alice'), b = await open('Bob');
  await sleep(100);
  a.send(JSON.stringify({ t: 'join', room: 'BOG1' }));
  await sleep(100);
  b.send(JSON.stringify({ t: 'join', room: 'BOG1' }));
  await sleep(150);
  const room = b.msgs.filter((m) => m.t === 'room').pop();
  check(room && room.players.length === 2 && room.players.find((p) => p.name === 'Bob').lane === 1, 'room + lanes');
  const snap = new Uint8Array(96); snap[0] = 0x51; snap[2] = 0x99; snap[3] = 0x99;   // spoofed id
  a.send(snap);
  await sleep(150);
  const aid = a.msgs.find((m) => m.t === 'welcome').id;
  check(b.bins.length === 1 && (b.bins[0][2] | (b.bins[0][3] << 8)) === aid, 'snapshot relayed with authoritative sender id');
  a.send(JSON.stringify({ t: 'ping', c: 1 }));
  b.send(JSON.stringify({ t: 'start', mode: 1 }));     // not host: ignored
  a.send(JSON.stringify({ t: 'start', mode: 1 }));
  await sleep(150);
  const st = b.msgs.find((m) => m.t === 'start');
  check(st && st.mode === 1 && st.startAt > Date.now() / 1000, 'host-only synchronized start');
  check(a.msgs.some((m) => m.t === 'pong' && m.c === 1), 'clock sync pong');
  a.send(JSON.stringify({ t: 'start', mode: 6, wx: { w: 2, tod: 3, wet: 0.6 } }));
  await sleep(150);
  const st2 = b.msgs.filter((m) => m.t === 'start').pop();
  check(st2 && st2.mode === 6, 'burnout (and bounty) modes start in multiplayer');
  check(st2 && st2.weather && st2.weather.w === 2 && st2.weather.tod === 3 && st2.weather.wet === 0.6, "host's weather is shared with the room");
  a.close(); b.close();
} catch (e) { failed++; console.error(e); }
srv.kill();
console.log(failed ? `${failed} failed` : 'server tests passed');
process.exit(failed ? 1 : 0);
