// Mud Buddies web shell: UI, Web Audio bridge, WebSocket multiplayer, leaderboards,
// gamepad menu navigation. The game itself (sim, rendering, audio DSP) is C++/WASM.
'use strict';

const $ = (id) => document.getElementById(id);
const store = {
  get(k, d) { try { const v = localStorage.getItem('mb_' + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('mb_' + k, JSON.stringify(v)); } catch { /* private mode */ } },
};

const BOARDS = {
  bog_time: { label: 'Mud Bog — Fastest Full Pass', lower: true, fmt: fmtTime },
  bog_distance: { label: 'Mud Bog — Farthest', lower: false, fmt: (v) => `${Math.round(v * 3.281)} ft` },
  circuit_race: { label: 'Hill & Hole — Race', lower: true, fmt: fmtTime },
  circuit_lap: { label: 'Hill & Hole — Best Lap', lower: true, fmt: fmtTime },
  freestyle_score: { label: 'Mud Slinger — Score', lower: false, fmt: (v) => `${Math.round(v).toLocaleString()} pts` },
  king_time: { label: 'King of the Pit — Crown Time', lower: false, fmt: fmtTime },
};
function fmtTime(t) { const m = Math.floor(t / 60); return `${m}:${(t - m * 60).toFixed(2).padStart(5, '0')}`; }

// ------------------------------------------------------------------------------------------
// Emscripten module hookup
let api = null;
window.Module = {
  canvas: document.getElementById('canvas'),
  print: (t) => console.log(t),
  printErr: (t) => console.warn(t),
  setStatus: (s) => { const m = /\((\d+)\/(\d+)\)/.exec(s); if (m) $('loadbar').style.width = `${(100 * m[1]) / m[2]}%`; },
  onFatal: (msg) => { $('loadmsg').textContent = msg; },
  onGameReady: () => boot(),
};

function boot() {
  const c = Module.cwrap;
  api = {
    resize: c('mb_resize', null, ['number', 'number']),
    setConfig: c('mb_set_config', null, ['string']),
    setState: c('mb_set_state', null, ['number']),
    startMode: c('mb_start_mode', null, ['number', 'number', 'number']),
    action: c('mb_action', null, ['string']),
    hud: c('mb_hud', 'string', []),
    quality: c('mb_set_quality', null, ['number']),
    assist: c('mb_set_assist', null, ['number']),
    audioInit: c('mb_audio_init', null, ['number']),
    volume: c('mb_set_volume', null, ['number', 'number', 'number', 'number']),
    remoteAdd: c('mb_remote_add', null, ['number', 'string', 'string']),
    remoteRemove: c('mb_remote_remove', null, ['number']),
    clockOffset: c('mb_set_clock_offset', null, ['number']),
    localId: c('mb_set_local_id', null, ['number']),
    catalog: c('mb_catalog', 'string', []),
  };
  $('loadbar').style.width = '100%';
  setTimeout(() => $('loading').classList.add('hide'), 250);
  initGarage();
  initSettings();
  initLobby();
  onResize();
  window.addEventListener('resize', onResize);
  show('menu');
  api.setState(0);
  setInterval(updateHud, 50);
  requestAnimationFrame(padNavLoop);
  const params = new URLSearchParams(location.search);
  if (params.get('autostart')) startSolo(+params.get('autostart'));
}

function onResize() {
  const scale = +(store.get('scale', 1)) * Math.min(window.devicePixelRatio || 1, 2);
  api.resize(Math.round(innerWidth * scale), Math.round(innerHeight * scale));
}

// ------------------------------------------------------------------------------------------
// Audio: C++ synthesizes every sample; Web Audio just pulls interleaved stereo frames.
let audioCtx = null;
function startAudio() {
  if (audioCtx) { if (audioCtx.state === 'suspended') audioCtx.resume(); return; }
  try {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
    api.audioInit(audioCtx.sampleRate);
    const node = audioCtx.createScriptProcessor(2048, 0, 2);
    node.onaudioprocess = (e) => {
      const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      const n = L.length;
      const ptr = Module._mb_audio_render(n) >> 2;
      const heap = Module.HEAPF32;
      for (let i = 0; i < n; i++) { L[i] = heap[ptr + 2 * i]; R[i] = heap[ptr + 2 * i + 1]; }
    };
    node.connect(audioCtx.destination);
    applyVolumes();
  } catch (err) { console.warn('audio unavailable', err); }
}
['pointerdown', 'keydown', 'touchstart'].forEach((ev) => window.addEventListener(ev, startAudio, { passive: true }));

// ------------------------------------------------------------------------------------------
// Screens
const screens = ['menu', 'play', 'garage', 'lobby', 'boards', 'settings', 'help', 'pause', 'results'];
let current = 'menu';
function show(name) {
  for (const s of screens) $(s).classList.toggle('show', s === name);
  current = name;
  $('hud').classList.toggle('hidden', name !== 'pause');
  if (name === 'garage') api.setState(1);
  if (name === 'boards') loadBoards();
  focusFirst();
}
function hideAll() { for (const s of screens) $(s).classList.remove('show'); current = null; $('hud').classList.remove('hidden'); }
document.addEventListener('click', (e) => {
  const go = e.target.closest('[data-go]');
  if (go) {
    const t = go.dataset.go;
    if (t === 'menu' && (current === 'garage')) api.setState(0);
    show(t);
  }
  const card = e.target.closest('[data-mode]');
  if (card) startSolo(+card.dataset.mode);
});
function toast(msg) { const t = $('toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toast.h); toast.h = setTimeout(() => t.classList.remove('show'), 2600); }

let lastMode = 1;
function startSolo(mode) {
  lastMode = mode;
  hideAll();
  api.startMode(mode, 0, 0);
  $('canvas').focus();
}
$('btnResume').onclick = () => { hideAll(); api.setState(2); };
$('btnRestart').onclick = () => { hideAll(); if (net.room && net.isHost) net.send({ t: 'start', mode: lastMode }); else api.startMode(lastMode, net.lane || 0, 0); };
$('btnTow').onclick = () => { hideAll(); api.setState(2); api.action('tow'); };
$('btnToGarage').onclick = () => show('garage');
$('btnQuit').onclick = () => { api.setState(0); show('menu'); };
$('btnAgain').onclick = () => { hideAll(); api.startMode(lastMode, net.lane || 0, 0); };
$('btnResBoards').onclick = () => { api.setState(0); show('boards'); };
$('btnResMenu').onclick = () => { api.setState(0); show('menu'); };

// ------------------------------------------------------------------------------------------
// Garage
let catalog = { bodies: [], engines: [] };
const cfg = Object.assign({ name: 'Buddy', body: 'comanche', engine: 'i6_40', tire: 35, width: 12.5, tread: 'mt', lift: 4, axle: '4.56',
  lockF: false, lockR: true, awd: true, snorkel: false, psi: 18, paint: '5d625d' }, store.get('cfg', {}));
function cfgString() {
  return `name=${cfg.name.replace(/[;=]/g, '')};body=${cfg.body};engine=${cfg.engine};tire=${cfg.tire};width=${cfg.width};tread=${cfg.tread};` +
    `lift=${cfg.lift};axle=${cfg.axle};lockF=${+cfg.lockF};lockR=${+cfg.lockR};awd=${+cfg.awd};snorkel=${+cfg.snorkel};psi=${cfg.psi};paint=${cfg.paint}`;
}
function initGarage() {
  catalog = JSON.parse(api.catalog());
  $('g_body').innerHTML = catalog.bodies.map((b) => `<option value="${b.id}">${b.name}</option>`).join('');
  $('g_engine').innerHTML = catalog.engines.map((e) => `<option value="${e.id}">${e.name} — ${e.hp} hp</option>`).join('');
  const bind = (id, key, conv = (v) => v) => {
    const el = $(id);
    if (el.type === 'checkbox') el.checked = !!cfg[key]; else el.value = key === 'paint' ? '#' + cfg[key] : cfg[key];
    el.addEventListener('input', () => {
      cfg[key] = el.type === 'checkbox' ? el.checked : conv(el.value);
      if (key === 'body') {   // pick sensible defaults for the new body
        const b = catalog.bodies.find((x) => x.id === cfg.body);
        if (b) { cfg.engine = b.engine; cfg.tire = b.tire; cfg.awd = b.awd; cfg.paint = b.paint; refreshGarage(); }
      }
      applyCfg();
    });
  };
  bind('g_name', 'name'); bind('g_body', 'body'); bind('g_engine', 'engine'); bind('g_tire', 'tire', Number);
  bind('g_width', 'width', Number); bind('g_tread', 'tread'); bind('g_lift', 'lift', Number); bind('g_axle', 'axle');
  bind('g_psi', 'psi', Number); bind('g_awd', 'awd'); bind('g_lockR', 'lockR'); bind('g_lockF', 'lockF');
  bind('g_snorkel', 'snorkel'); bind('g_paint', 'paint', (v) => v.replace('#', ''));
  applyCfg();
}
function refreshGarage() {
  $('g_engine').value = cfg.engine; $('g_tire').value = cfg.tire; $('g_awd').checked = cfg.awd; $('g_paint').value = '#' + cfg.paint;
}
function applyCfg() {
  $('v_tire').textContent = `${cfg.tire}"`;
  $('v_width').textContent = `${cfg.width}"`;
  $('v_lift').textContent = `${cfg.lift}"`;
  $('v_psi').textContent = `${cfg.psi} psi`;
  const e = catalog.engines.find((x) => x.id === cfg.engine) || {};
  $('specLine').textContent = `${e.hp || '?'} hp · ${e.tq || '?'} N·m · ${cfg.tire}x${cfg.width} ${cfg.tread.toUpperCase()} · ${cfg.axle} gears · ` +
    `${cfg.awd ? '4WD' : '2WD'}${cfg.lockR ? ' · rear locker' : ''}${cfg.lockF ? ' · front locker' : ''}${cfg.snorkel ? ' · snorkel' : ''}`;
  store.set('cfg', cfg);
  api.setConfig(cfgString());
  if (net.ws && net.ws.readyState === 1) net.send({ t: 'cfg', cfg: cfgString(), name: cfg.name });
}

// ------------------------------------------------------------------------------------------
// Settings
function initSettings() {
  const s = store.get('settings', {});
  for (const k of ['master', 'engine', 'sfx', 'amb']) if (s[k] != null) $('s_' + k).value = s[k];
  if (s.quality != null) $('s_quality').value = s.quality;
  $('s_scale').value = store.get('scale', 1);
  if (s.assist != null) $('s_assist').checked = s.assist;
  const save = () => {
    store.set('settings', { master: +$('s_master').value, engine: +$('s_engine').value, sfx: +$('s_sfx').value, amb: +$('s_amb').value,
      quality: +$('s_quality').value, assist: $('s_assist').checked });
    applyVolumes();
    api.quality(+$('s_quality').value);
    api.assist($('s_assist').checked ? 1 : 0);
  };
  document.querySelectorAll('#settings input, #settings select').forEach((el) => el.addEventListener('input', save));
  $('s_scale').addEventListener('input', () => { store.set('scale', +$('s_scale').value); onResize(); });
  save();
}
function applyVolumes() { if (api) api.volume(+$('s_master').value, +$('s_engine').value, +$('s_sfx').value, +$('s_amb').value); }

// ------------------------------------------------------------------------------------------
// HUD
let lastSerial = 0, hud = {};
function updateHud() {
  try { hud = JSON.parse(api.hud()); } catch { return; }
  if (hud.pause && current === null) { api.setState(3); show('pause'); }
  $('padHint').textContent = hud.pad ? `🎮 ${hud.padName.slice(0, 40)}` : 'Connect a gamepad — press any button';
  if (current !== null) return;
  const rn = hud.rpm / hud.redline;
  $('tachFill').style.width = `${Math.min(100, rn * 100)}%`;
  $('hudSpeed').textContent = Math.round(hud.mph);
  $('hudGear').textContent = hud.stalled ? '!' : hud.gear < 0 ? 'R' : hud.gear === 0 ? 'N' : hud.gear;
  $('lamp4wd').classList.toggle('on', hud.awd);
  $('lampLo').classList.toggle('on', hud.low);
  $('lampLockR').classList.toggle('on', hud.lockR);
  $('lampLockF').classList.toggle('on', hud.lockF);
  $('lampMan').classList.toggle('on', hud.manual);
  $('hudPsi').textContent = hud.psi.toFixed(0);
  $('hudTemp').textContent = `${Math.round(hud.temp * 9 / 5 + 32)}°`;
  $('hudTemp').classList.toggle('hot', hud.temp > 115);
  $('hudClog').textContent = `${Math.round(hud.clog * 100)}%`;
  $('hudClog').classList.toggle('hot', hud.clog > 0.5);
  $('hudMode').textContent = hud.modeName;
  let timer = '', sub = '';
  switch (hud.mode) {
    case 1: timer = fmtTime(hud.elapsed); sub = `${Math.round(hud.distance * 3.281)} ft / 262 ft · lane ${hud.lane + 1}`; break;
    case 2: timer = fmtTime(hud.elapsed + hud.penalty); sub = `Lap ${Math.min(hud.lap + 1, hud.laps)}/${hud.laps}${hud.bestLap > 0 ? ' · best ' + fmtTime(hud.bestLap) : ''}`; break;
    case 3: timer = fmtTime(Math.max(0, hud.limit - hud.elapsed)); sub = `${Math.round(hud.score).toLocaleString()} pts · x${hud.combo.toFixed(1)}`; break;
    case 4: timer = fmtTime(Math.max(0, hud.limit - hud.elapsed)); sub = `Crown ${fmtTime(hud.king)}`; break;
    default: timer = ''; sub = 'Free roam · T = dozer tow';
  }
  $('hudTimer').textContent = timer;
  $('hudSub').textContent = sub;
  const cd = hud.phase === 1 ? Math.ceil(hud.countdown) : 0;
  $('countdown').textContent = hud.phase === 1 && cd > 0 && cd <= 5 ? cd : '';
  $('hudMsg').textContent = hud.message || '';
  $('stuckHint').style.display = hud.stuck ? 'block' : 'none';
  drawMinimap();
  $('others').innerHTML = (hud.others || []).map((o) => `${esc(net.names[o.id] || 'Buddy ' + o.id)}: ${hud.mode === 1 ? Math.round(o.dist * 3.281) + ' ft' : Math.round(o.score)}`).join('<br>');
  if (hud.finishSerial !== lastSerial) {
    lastSerial = hud.finishSerial;
    if (hud.results && hud.results.length) onResults(hud.results);
  }
}

function drawMinimap() {
  const c = $('minimap'), g = c.getContext('2d');
  const W = c.width, s = W / 192;
  g.clearRect(0, 0, W, W);
  const P = (x, z) => [W / 2 + x * s, W / 2 + z * s];
  g.fillStyle = '#3a2a1c'; let [x, y] = P(-40, -43); g.fillRect(x, y, 80 * s, 26 * s);
  g.beginPath(); [x, y] = P(60, -66); g.arc(x, y, 17 * s, 0, 7); g.fill();
  g.fillStyle = '#5b6e8a'; g.beginPath(); [x, y] = P(0, 32); g.arc(x, y, 9 * s, 0, 7); g.fill();
  g.fillStyle = '#ff5a14';
  for (const o of hud.others || []) { [x, y] = P(o.x, o.z); g.beginPath(); g.arc(x, y, 4, 0, 7); g.fill(); }
  [x, y] = P(hud.x, hud.z);
  g.save(); g.translate(x, y); g.rotate(-hud.heading + Math.PI);
  g.fillStyle = '#f2c418'; g.beginPath(); g.moveTo(0, -7); g.lineTo(5, 6); g.lineTo(-5, 6); g.closePath(); g.fill();
  g.restore();
}

// ------------------------------------------------------------------------------------------
// Results + leaderboards (server first, localStorage fallback)
function serverHttpBase() {
  const u = $('serverUrl').value.trim();
  if (u) return u.replace(/^ws/, 'http').replace(/\/ws$/, '');
  return location.protocol.startsWith('http') ? '' : null;
}
async function postScore(r) {
  const entry = { board: r.board, value: r.value, name: cfg.name, rig: rigLabel(), cfg: cfgString(), at: Date.now() };
  const local = store.get('boards', {});
  (local[r.board] = local[r.board] || []).push(entry);
  local[r.board].sort((a, b) => (BOARDS[r.board]?.lower ? a.value - b.value : b.value - a.value));
  local[r.board] = local[r.board].slice(0, 50);
  store.set('boards', local);
  const base = serverHttpBase();
  if (base == null) return null;
  try {
    const res = await fetch(base + '/api/score', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(entry) });
    if (res.ok) return await res.json();
  } catch { /* offline */ }
  return null;
}
function rigLabel() {
  const b = catalog.bodies.find((x) => x.id === cfg.body);
  return `${b ? b.name : cfg.body} · ${cfg.tire}" ${cfg.tread}`;
}
async function onResults(results) {
  api.setState(3);
  $('resTitle').textContent = hud.modeName;
  $('resBody').innerHTML = results.map((r) => `<div>${r.summary}</div><div class="rank" id="rank_${r.board}">&nbsp;</div>`).join('');
  show('results');
  for (const r of results) {
    const reply = await postScore(r);
    const el = $('rank_' + r.board);
    if (el) el.textContent = reply && reply.rank ? `#${reply.rank} on ${BOARDS[r.board].label}` : `Saved to local ${BOARDS[r.board].label}`;
  }
}
let boardKey = 'bog_distance';
async function loadBoards() {
  $('boardTabs').innerHTML = Object.entries(BOARDS).map(([k, b]) => `<button data-nav data-board="${k}" class="${k === boardKey ? 'on' : ''}">${b.label}</button>`).join('');
  document.querySelectorAll('[data-board]').forEach((b) => (b.onclick = () => { boardKey = b.dataset.board; loadBoards(); }));
  let rows = null, source = 'Local (this browser)';
  const base = serverHttpBase();
  if (base != null) {
    try { const r = await fetch(`${base}/api/leaderboards/${boardKey}`); if (r.ok) { rows = await r.json(); source = 'Global (server)'; } } catch { /* fallback */ }
  }
  if (!rows) rows = (store.get('boards', {})[boardKey]) || [];
  const b = BOARDS[boardKey];
  $('boardTable').querySelector('tbody').innerHTML = rows.slice(0, 50).map((e, i) =>
    `<tr><td>${i + 1}</td><td>${esc(e.name)}</td><td>${esc(e.rig || '')}</td><td>${b.fmt(e.value)}</td><td>${new Date(e.at).toLocaleDateString()}</td></tr>`).join('') ||
    '<tr><td colspan="5">No runs yet. Go get muddy.</td></tr>';
  $('boardSource').textContent = source;
}
function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }

// ------------------------------------------------------------------------------------------
// Multiplayer (WebSocket). Text = JSON lobby protocol, binary = 72-byte vehicle snapshots.
const net = { ws: null, id: 0, room: null, isHost: false, lane: 0, names: {}, offsets: [], snapPtr: 0, timer: 0 };
net.send = (o) => { if (net.ws && net.ws.readyState === 1) net.ws.send(JSON.stringify(o)); };
function initLobby() {
  $('serverUrl').value = store.get('server', location.protocol.startsWith('http') ? `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws` : '');
  $('btnConnect').onclick = connect;
  $('btnQuick').onclick = () => { ensureConnected(() => net.send({ t: 'quick' })); };
  $('btnJoin').onclick = () => { ensureConnected(() => net.send({ t: 'join', room: $('roomCode').value.trim().toUpperCase() })); };
  $('btnStart').onclick = () => net.send({ t: 'start', mode: +$('mpMode').value });
  $('chatIn').addEventListener('keydown', (e) => { if (e.key === 'Enter' && e.target.value.trim()) { net.send({ t: 'chat', msg: e.target.value.trim() }); e.target.value = ''; } });
}
function ensureConnected(fn) { if (net.ws && net.ws.readyState === 1) fn(); else { connect(); net.pending = fn; } }
function connect() {
  const url = $('serverUrl').value.trim();
  if (!url) { toast('Enter a server URL (run: node server/server.js)'); return; }
  store.set('server', url);
  try { net.ws = new WebSocket(url); } catch (e) { toast('Bad server URL'); return; }
  net.ws.binaryType = 'arraybuffer';
  $('netStatus').textContent = 'Connecting…';
  net.ws.onopen = () => {
    $('netStatus').textContent = 'Connected';
    net.send({ t: 'hello', name: cfg.name, cfg: cfgString() });
    clearInterval(net.ping); net.ping = setInterval(ping, 2000); ping();
  };
  net.ws.onclose = () => {
    $('netStatus').textContent = 'Disconnected';
    clearInterval(net.ping); clearInterval(net.timer);
    for (const id of Object.keys(net.names)) api.remoteRemove(+id);
    net.names = {}; net.room = null; renderRoom(null);
  };
  net.ws.onerror = () => toast('Could not reach the server');
  net.ws.onmessage = (ev) => {
    if (typeof ev.data !== 'string') {
      const bytes = new Uint8Array(ev.data);
      const p = Module._malloc(bytes.length);
      Module.HEAPU8.set(bytes, p);
      Module._mb_net_receive(p, bytes.length);
      Module._free(p);
      return;
    }
    let m; try { m = JSON.parse(ev.data); } catch { return; }
    onNet(m);
  };
}
function ping() { net.send({ t: 'ping', c: performance.now() / 1000 }); }
function onNet(m) {
  switch (m.t) {
    case 'welcome':
      net.id = m.id; api.localId(m.id);
      if (net.pending) { net.pending(); net.pending = null; }
      break;
    case 'pong': {
      const t3 = performance.now() / 1000;
      const off = ((m.s - m.c) + (m.s - t3)) / 2;   // NTP-style
      net.offsets.push({ off, rtt: t3 - m.c });
      if (net.offsets.length > 8) net.offsets.shift();
      const best = [...net.offsets].sort((a, b) => a.rtt - b.rtt)[0];
      api.clockOffset(best.off);
      $('netStatus').textContent = `Connected · ${Math.round(best.rtt * 1000)} ms`;
      break;
    }
    case 'room': {
      net.room = m.code; net.isHost = m.host === net.id;
      const ids = new Set(m.players.map((p) => p.id));
      for (const id of Object.keys(net.names)) if (!ids.has(+id)) { api.remoteRemove(+id); delete net.names[id]; }
      for (const p of m.players) {
        if (p.id === net.id) { net.lane = p.lane; continue; }
        if (net.names[p.id] !== p.name || p.cfgChanged) api.remoteAdd(p.id, p.cfg, p.name);
        net.names[p.id] = p.name;
      }
      renderRoom(m);
      startSnapshots();
      break;
    }
    case 'start':
      lastMode = m.mode;
      net.lane = (m.lanes && m.lanes[net.id]) || 0;
      hideAll();
      api.startMode(m.mode, net.lane, m.startAt);
      break;
    case 'chat': addChat(`<b>${esc(m.from)}</b>: ${esc(m.msg)}`); break;
    case 'error': toast(m.msg); break;
  }
}
function renderRoom(m) {
  if (!m) { $('roomInfo').innerHTML = ''; $('hostControls').classList.add('hidden'); return; }
  $('roomInfo').innerHTML = `<div>Room <b>${m.code}</b> · ${m.players.length}/8</div>` +
    m.players.map((p) => `<div class="p"><span>${esc(p.name)}${p.id === m.host ? ' 👑' : ''}${p.id === net.id ? ' (you)' : ''}</span><span>lane ${p.lane + 1}</span></div>`).join('');
  $('hostControls').classList.toggle('hidden', !net.isHost);
}
function addChat(html) { const l = $('chatLog'); l.insertAdjacentHTML('beforeend', `<div>${html}</div>`); l.scrollTop = l.scrollHeight; }
function startSnapshots() {
  if (net.timer) return;
  if (!net.snapPtr) net.snapPtr = Module._malloc(128);
  net.timer = setInterval(() => {   // 20 Hz state replication
    if (!net.ws || net.ws.readyState !== 1 || !net.room) return;
    const len = Module._mb_net_snapshot(net.snapPtr);
    if (len > 0) net.ws.send(Module.HEAPU8.slice(net.snapPtr, net.snapPtr + len));
  }, 50);
}

// ------------------------------------------------------------------------------------------
// Gamepad menu navigation (D-pad / stick to move, A select, B back, Start pause/resume)
let navPrev = {}, navRepeat = 0;
function focusables() {
  if (current === null) return [];
  return [...$(current).querySelectorAll('[data-nav]')].filter((e) => e.offsetParent !== null);
}
function focusFirst() { const f = focusables(); if (f.length && !f.includes(document.activeElement)) f[0].focus({ preventScroll: true }); }
function padNavLoop(t) {
  requestAnimationFrame(padNavLoop);
  const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
  if (!pads.length) return;
  const p = pads[0];
  const b = (i) => !!(p.buttons[i] && p.buttons[i].pressed);
  const ax = p.axes[0] || 0, ay = p.axes[1] || 0;
  const st = { up: b(12) || ay < -0.6, down: b(13) || ay > 0.6, left: b(14) || ax < -0.6, right: b(15) || ax > 0.6, a: b(0), b: b(1), start: b(9) };
  const edge = (k) => st[k] && !navPrev[k];
  const repeat = (k) => st[k] && (edge(k) || t > navRepeat);
  if (current === null) { navPrev = st; return; }   // in-game input is read by C++
  const f = focusables();
  let i = f.indexOf(document.activeElement);
  const el = f[i];
  if (repeat('down') || repeat('up')) {
    i = i < 0 ? 0 : (i + (st.down ? 1 : -1) + f.length) % f.length;
    f[i] && f[i].focus(); navRepeat = t + (edge('down') || edge('up') ? 350 : 120);
  }
  if (el && (repeat('left') || repeat('right'))) {
    const d = st.right ? 1 : -1;
    if (el.type === 'range') { el.value = +el.value + d * +el.step; el.dispatchEvent(new Event('input')); }
    else if (el.tagName === 'SELECT') { el.selectedIndex = Math.max(0, Math.min(el.options.length - 1, el.selectedIndex + d)); el.dispatchEvent(new Event('input')); }
    navRepeat = t + (edge('left') || edge('right') ? 300 : 90);
  }
  if (edge('a') && el) {
    if (el.type === 'checkbox') { el.checked = !el.checked; el.dispatchEvent(new Event('input')); } else el.click();
  }
  if (edge('b') || (edge('start') && current === 'pause')) {
    if (current === 'pause') $('btnResume').click();
    else { const back = $(current).querySelector('.back'); if (back) back.click(); }
  }
  navPrev = st;
}
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && current === 'pause') { $('btnResume').click(); e.preventDefault(); }
  else if (e.key === 'Escape' && current && current !== 'menu' && current !== 'results') { const back = $(current).querySelector('.back'); if (back) back.click(); }
});
window.addEventListener('gamepadconnected', (e) => toast(`Gamepad connected: ${e.gamepad.id.slice(0, 40)}`));
