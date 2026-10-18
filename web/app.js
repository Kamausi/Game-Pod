// Mud Buddies web shell: UI, Web Audio bridge, WebSocket multiplayer, leaderboards,
// gamepad menu navigation. The game itself (sim, rendering, audio DSP) is C++/WASM.
'use strict';

const $ = (id) => document.getElementById(id);
const store = {
  get(k, d) { try { const v = localStorage.getItem('mb_' + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('mb_' + k, JSON.stringify(v)); } catch { /* private mode */ } },
};

// Platform: phones/tablets get touch controls, lighter rendering defaults and a capped DPR.
const IS_TOUCH = (window.matchMedia && matchMedia('(pointer: coarse)').matches) || ('ontouchstart' in window && navigator.maxTouchPoints > 0);
if (IS_TOUCH) document.body.classList.add('touch');

const BOARDS = {
  bog_time: { label: 'Mud Bog — Fastest Full Pass', lower: true, fmt: fmtTime },
  bog_distance: { label: 'Mud Bog — Farthest', lower: false, fmt: (v) => `${Math.round(v * 3.281)} ft` },
  circuit_race: { label: 'Hill & Hole — Race', lower: true, fmt: fmtTime },
  circuit_lap: { label: 'Hill & Hole — Best Lap', lower: true, fmt: fmtTime },
  freestyle_score: { label: 'Mud Slinger — Score', lower: false, fmt: (v) => `${Math.round(v).toLocaleString()} pts` },
  king_time: { label: 'King of the Pit — Crown Time', lower: false, fmt: fmtTime },
  bounty_time: { label: 'Bounty Hole — Fastest Claim', lower: true, fmt: fmtTime },
  bounty_distance: { label: 'Bounty Hole — Farthest', lower: false, fmt: (v) => `${Math.round(v * 3.281)} ft` },
  burnout_score: { label: 'Burnout — Smoke Points', lower: false, fmt: (v) => `${Math.round(v).toLocaleString()} pts` },
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
    spec: c('mb_spec', 'string', ['string']),
  };
  $('loadbar').style.width = '100%';
  setTimeout(() => $('loading').classList.add('hide'), 250);
  initGarage();
  initSettings();
  initLobby();
  onResize();
  window.addEventListener('resize', onResize);
  if (window.visualViewport) visualViewport.addEventListener('resize', onResize);
  window.addEventListener('orientationchange', () => setTimeout(onResize, 250));
  initTouch();
  show('menu');
  api.setState(0);
  setInterval(updateHud, 50);
  requestAnimationFrame(padNavLoop);
  const params = new URLSearchParams(location.search);
  if (params.get('autostart')) startSolo(+params.get('autostart'));
}

function viewport() {
  const vv = window.visualViewport;
  return { w: Math.round(vv ? vv.width : innerWidth), h: Math.round(vv ? vv.height : innerHeight) };
}
function applyUiScale() {
  const { w, h } = viewport();
  // 1.0 at 1280x720; shrink for phones, grow a little on big monitors.
  const ui = Math.max(0.5, Math.min(1.5, Math.min(w / 1280, h / 720)));
  document.documentElement.style.setProperty('--ui', ui.toFixed(3));
  document.documentElement.style.setProperty('--tsize', String(+($('s_tsize') ? $('s_tsize').value : 1) * Math.max(0.75, Math.min(1.25, Math.min(w, h) / 420))));
}
function onResize() {
  applyUiScale();
  const { w, h } = viewport();
  const dprCap = IS_TOUCH ? 1.5 : 2;   // phones: fill-rate is the bottleneck
  const scale = +(store.get('scale', IS_TOUCH ? 0.75 : 1)) * Math.min(window.devicePixelRatio || 1, dprCap);
  api.resize(Math.max(320, Math.round(w * scale)), Math.max(200, Math.round(h * scale)));
}

// ------------------------------------------------------------------------------------------
// Audio: C++ synthesizes every sample; Web Audio just pulls interleaved stereo frames.
let audioCtx = null;
// Preferred path: an AudioWorklet sink fed with blocks rendered by the C++ engine on the main
// thread, kept ~80 ms ahead of the audio clock. Fallback: ScriptProcessorNode.
const SINK_SRC = `class MBSink extends AudioWorkletProcessor {
  constructor() { super(); this.q = []; this.off = 0; this.port.onmessage = (e) => this.q.push(e.data); }
  process(_in, outputs) {
    const L = outputs[0][0], R = outputs[0][1] || outputs[0][0], n = L.length;
    let i = 0;
    while (i < n) {
      const b = this.q[0];
      if (!b) { L.fill(0, i); R.fill(0, i); break; }
      const take = Math.min((b.length >> 1) - this.off, n - i);
      for (let k = 0; k < take; k++) { L[i + k] = b[(this.off + k) * 2]; R[i + k] = b[(this.off + k) * 2 + 1]; }
      i += take; this.off += take;
      if (this.off >= (b.length >> 1)) { this.q.shift(); this.off = 0; }
    }
    if (this.q.length > 40) this.q.splice(0, this.q.length - 20);   // drop if we fell far behind
    return true;
  }
}
registerProcessor('mb-sink', MBSink);`;
async function startAudio() {
  if (audioCtx) { if (audioCtx.state === 'suspended') audioCtx.resume(); return; }
  try {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  } catch (err) { console.warn('audio unavailable', err); return; }
  api.audioInit(audioCtx.sampleRate);
  applyVolumes();
  if (audioCtx.audioWorklet && window.AudioWorkletNode) {
    try {
      await audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([SINK_SRC], { type: 'application/javascript' })));
      const node = new AudioWorkletNode(audioCtx, 'mb-sink', { numberOfInputs: 0, outputChannelCount: [2] });
      node.connect(audioCtx.destination);
      const BLOCK = 512, lead = Math.round(audioCtx.sampleRate * 0.08);
      let produced = 0;
      const pump = () => {
        const now = Math.round(audioCtx.currentTime * audioCtx.sampleRate);
        if (produced < now) produced = now;           // tab was throttled: skip, don't burst
        let guard = 0;
        while (produced < now + lead && guard++ < 24) {
          const ptr = Module._mb_audio_render(BLOCK) >> 2;
          const buf = Module.HEAPF32.slice(ptr, ptr + BLOCK * 2);
          node.port.postMessage(buf, [buf.buffer]);
          produced += BLOCK;
        }
      };
      setInterval(pump, 8);
      console.log('audio: AudioWorklet sink @', audioCtx.sampleRate);
      return;
    } catch (err) { console.warn('AudioWorklet failed, falling back', err); }
  }
  const node = audioCtx.createScriptProcessor(2048, 0, 2);
  node.onaudioprocess = (e) => {
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1), n = L.length;
    const ptr = Module._mb_audio_render(n) >> 2, heap = Module.HEAPF32;
    for (let i = 0; i < n; i++) { L[i] = heap[ptr + 2 * i]; R[i] = heap[ptr + 2 * i + 1]; }
  };
  node.connect(audioCtx.destination);
}
['pointerdown', 'keydown', 'touchstart'].forEach((ev) => window.addEventListener(ev, startAudio, { passive: true }));

// ------------------------------------------------------------------------------------------
// Screens
const screens = ['menu', 'play', 'garage', 'lobby', 'boards', 'settings', 'guide', 'help', 'pause', 'recover', 'results'];
let current = 'menu';
function show(name) {
  document.body.classList.toggle('ingame', name === null);
  for (const s of screens) $(s).classList.toggle('show', s === name);
  current = name;
  $('hud').classList.toggle('hidden', name !== 'pause');
  if (name === 'garage') { if (current !== 'garage') chargeRepairs(); api.setState(1); updateWallet(); }
  if (name === 'menu') updateWallet();
  if (name === 'boards') loadBoards();
  focusFirst();
}
function hideAll() {
  for (const s of screens) $(s).classList.remove('show');
  current = null;
  $('hud').classList.remove('hidden');
  document.body.classList.add('ingame');
}
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
function goFullscreen() {
  if (!IS_TOUCH || document.fullscreenElement) return;
  const el = document.documentElement;
  const req = el.requestFullscreen || el.webkitRequestFullscreen;
  if (req) Promise.resolve(req.call(el, { navigationUI: 'hide' })).then(() => {
    if (screen.orientation && screen.orientation.lock) screen.orientation.lock('landscape').catch(() => {});
  }).catch(() => {});
}
function startSolo(mode) {
  chargeRepairs();
  goFullscreen();
  touch.tiltZero = null;   // re-center tilt steering at the start of every event
  lastMode = mode;
  hideAll();
  Module._mb_set_ai_count(net.room ? 0 : +$('s_ai').value);
  loadGhost(net.room ? -1 : mode);
  api.startMode(mode, 0, 0);
  $('canvas').focus();
}
$('btnResume').onclick = () => { hideAll(); api.setState(2); };
$('btnRestart').onclick = () => { if (net.room && net.isHost) { hideAll(); net.send({ t: 'start', mode: lastMode }); } else if (!net.room) startSolo(lastMode); else hideAll(); };
$('btnTow').onclick = () => { hideAll(); api.setState(2); api.action('tow'); };
$('btnToGarage').onclick = () => show('garage');
// Recovery menu: the tow key pauses and offers the guide's escalation, least force first.
function openRecover() {
  api.action('recover');   // clear the request flag
  api.setState(3);
  const bog = hud.mode === 1 && hud.phase === 2;
  $('recNote').textContent = bog ? 'Judged pull: no self-recovery - the dozer ends your run.'
    : `Least force first. Boards: ${hud.boards}/2 · ${hud.winch ? (hud.winch === 3 ? 'winch line broken' : 'winch ready') : 'no winch fitted'}`;
  for (const k of ['shovel', 'boards', 'winch']) document.querySelector(`[data-rec="${k}"]`).disabled = bog || (k === 'winch' && hud.winch !== 1 && hud.winch !== 2);
  $('recWinch').firstChild.textContent = hud.winch === 2 ? 'Release winch ' : 'Winch ';
  show('recover');
}
document.querySelectorAll('[data-rec]').forEach((b) => (b.onclick = () => {
  if (b.dataset.rec === 'dozer' && hud.mode === 0) career.add(-75, 'dozer tow');
  hideAll(); api.setState(2); api.action(b.dataset.rec); $('canvas').focus();
}));
$('btnRecClose').onclick = () => { hideAll(); api.setState(2); $('canvas').focus(); };
$('btnQuit').onclick = () => { api.setState(0); show('menu'); };
$('btnAgain').onclick = () => { if (net.room) { hideAll(); api.startMode(lastMode, net.lane || 0, 0); } else startSolo(lastMode); };
$('btnResBoards').onclick = () => { api.setState(0); show('boards'); };
$('btnResMenu').onclick = () => { api.setState(0); show('menu'); };

// ------------------------------------------------------------------------------------------
// Garage
let catalog = { bodies: [], engines: [] };
const cfg = Object.assign({ name: 'Buddy', body: 'comanche', engine: 'i6_40', tire: 35, width: 12.5, tread: 'mt', lift: 4, axle: '4.56',
  lockF: false, lockR: true, awd: true, snorkel: false, psi: 18, paint: '5d625d', bead: false, btc: false, winch: true }, store.get('cfg', {}));
// Older saves only had locker on/off flags.
if (!cfg.diffR) cfg.diffR = cfg.lockR ? 'locker' : 'open';
if (!cfg.diffF) cfg.diffF = cfg.lockF ? 'locker' : 'open';
function cfgString() {
  return `name=${cfg.name.replace(/[;=]/g, '')};body=${cfg.body};engine=${cfg.engine};tire=${cfg.tire};width=${cfg.width};tread=${cfg.tread};` +
    `lift=${cfg.lift};axle=${cfg.axle};diffF=${cfg.diffF};diffR=${cfg.diffR};lockF=0;lockR=${+(cfg.diffR !== 'open' && cfg.diffR !== 'lsd')};` +
    `awd=${+cfg.awd};snorkel=${+cfg.snorkel};bead=${+cfg.bead};btc=${+cfg.btc};winch=${+cfg.winch};psi=${cfg.psi};paint=${cfg.paint}`;
}
function initGarage() {
  catalog = JSON.parse(api.catalog());
  $('g_body').innerHTML = catalog.bodies.map((b) => `<option value="${b.id}">${b.name}</option>`).join('');
  $('g_engine').innerHTML = catalog.engines.map((e) => `<option value="${e.id}">${e.name} — ${e.hp} hp</option>`).join('');
  const bind = (id, key, conv = (v) => v) => {
    const el = $(id);
    if (el.type === 'checkbox') el.checked = !!cfg[key]; else el.value = key === 'paint' ? '#' + cfg[key] : cfg[key];
    el.addEventListener('input', () => {
      const nv = el.type === 'checkbox' ? el.checked : conv(el.value);
      if (!buy(key, nv)) {   // can't afford it: put the control back
        if (el.type === 'checkbox') el.checked = !!cfg[key]; else el.value = key === 'paint' ? '#' + cfg[key] : cfg[key];
        return;
      }
      cfg[key] = nv;
      if (key === 'body') {   // pick sensible defaults for the new body
        const b = catalog.bodies.find((x) => x.id === cfg.body);
        if (b) { cfg.engine = b.engine; cfg.tire = b.tire; cfg.awd = b.awd; cfg.paint = b.paint; refreshGarage(); }
      }
      applyCfg();
    });
  };
  bind('g_name', 'name'); bind('g_body', 'body'); bind('g_engine', 'engine'); bind('g_tire', 'tire', Number);
  bind('g_width', 'width', Number); bind('g_tread', 'tread'); bind('g_lift', 'lift', Number); bind('g_axle', 'axle');
  bind('g_psi', 'psi', Number); bind('g_awd', 'awd'); bind('g_diffF', 'diffF'); bind('g_diffR', 'diffR');
  bind('g_btc', 'btc'); bind('g_bead', 'bead'); bind('g_winch', 'winch');
  bind('g_snorkel', 'snorkel'); bind('g_paint', 'paint', (v) => v.replace('#', ''));
  applyCfg();
  updateWallet();
}
const DIFF = { open: 'open', lsd: 'LSD', locker: 'locker', spool: 'spool' };
$('btnWash').onclick = () => { chargeRepairs(); career.add(-20, 'wash'); api.action('wash'); toast('Washed and repaired'); };
// ------------------------------------------------------------------------------------------
// Career economy: prize money from events buys trucks and parts; repairs and tows cost money.
const career = {
  money: store.get('money', 2500),
  owned: new Set(store.get('owned', ['body:comanche', 'engine:i6_40', 'tread:at', 'tread:mt', 'axle:4.56', 'diffF:open', 'diffR:open', 'diffR:locker'])),
  maxTire: store.get('maxTire', 35), maxLift: store.get('maxLift', 4),
  on() { return !$('s_career') || $('s_career').checked; },
  save() { store.set('money', this.money); store.set('owned', [...this.owned]); store.set('maxTire', this.maxTire); store.set('maxLift', this.maxLift); updateWallet(); },
  add(n, why) { if (!this.on() || !n) return; this.money += n; this.save(); if (why) toast(`${n > 0 ? '+' : '−'}$${Math.abs(n).toLocaleString()} ${why}`); },
};
const BODY_PRICE = { comanche: 0, bogcar76: 1500, squarebody: 2500, fullsize_suv: 3000, atv: 4000, utv: 6000, silverado: 9000, megatruck: 30000 };
const DIFF_PRICE = { open: 0, lsd: 500, locker: 900, spool: 250 };
function engPrice(id) { const e = catalog.engines.find((x) => x.id === id); return e ? Math.round((e.hp || 100) * 14 / 50) * 50 : 0; }
// Cost of changing cfg[key] to value (0 if owned/free). Returns [cost, ownKey, label].
function priceOf(key, value) {
  const k = `${key}:${value}`;
  switch (key) {
    case 'body': return career.owned.has(k) ? [0] : [BODY_PRICE[value] ?? 5000, k, (catalog.bodies.find((b) => b.id === value) || {}).name || value];
    case 'engine': return career.owned.has(k) ? [0] : [engPrice(value), k, (catalog.engines.find((e) => e.id === value) || {}).name || value];
    case 'tread': return career.owned.has(k) ? [0] : [1200, k, 'paddle tires'];
    case 'axle': return career.owned.has(k) ? [0] : [900, k, `${value} gears (regear)`];
    case 'diffF': case 'diffR': return career.owned.has(k) ? [0] : [DIFF_PRICE[value] || 0, k, `${key === 'diffF' ? 'front' : 'rear'} ${value}`];
    case 'tire': { const t = +value; if (t <= career.maxTire) return [0]; let c = 0; for (let i = career.maxTire + 1; i <= t; i++) c += i > 44 ? 400 : 180; return [c, 'maxTire', `${t}" tires`]; }
    case 'lift': { const l = +value; return l <= career.maxLift ? [0] : [Math.ceil((l - career.maxLift) * 150), 'maxLift', `${l}" lift`]; }
    case 'bead': return value && !career.owned.has('bead') ? [800, 'bead', 'beadlocks'] : [0];
    case 'btc': return value && !career.owned.has('btc') ? [400, 'btc', 'brake traction control'] : [0];
    case 'winch': return value && !career.owned.has('winch') ? [1200, 'winch', 'front winch'] : [0];
    case 'snorkel': return value && !career.owned.has('snorkel') ? [350, 'snorkel', 'snorkel'] : [0];
    case 'awd': return value && !career.owned.has('awd') && !(catalog.bodies.find((b) => b.id === cfg.body) || {}).awd ? [2000, 'awd', '4WD conversion'] : [0];
    default: return [0];
  }
}
function buy(key, value) {
  if (!career.on()) return true;
  const [cost, ownKey, label] = priceOf(key, value);
  if (!cost) return true;
  if (career.money < cost) { toast(`${label}: $${cost.toLocaleString()} — you have $${career.money.toLocaleString()}. Win some prize money!`); return false; }
  career.money -= cost;
  if (ownKey === 'maxTire') career.maxTire = +value; else if (ownKey === 'maxLift') career.maxLift = +value; else career.owned.add(ownKey);
  if (key === 'body') {   // a truck comes with its stock engine, tires and drivetrain
    const b = catalog.bodies.find((x) => x.id === value);
    if (b) { career.owned.add('engine:' + b.engine); career.maxTire = Math.max(career.maxTire, b.tire); if (b.awd) career.owned.add('awd'); }
  }
  career.save();
  toast(`Bought ${label} — $${cost.toLocaleString()}`);
  return true;
}
// First run of the career: whatever is already on your truck is yours.
if (store.get('owned', null) == null) {
  for (const k of ['body', 'engine', 'tread', 'axle', 'diffF', 'diffR']) career.owned.add(`${k}:${cfg[k]}`);
  for (const k of ['bead', 'btc', 'winch', 'snorkel', 'awd']) if (cfg[k]) career.owned.add(k);
  career.maxTire = Math.max(career.maxTire, +cfg.tire); career.maxLift = Math.max(career.maxLift, +cfg.lift);
  store.set('owned', [...career.owned]); store.set('maxTire', career.maxTire); store.set('maxLift', career.maxLift);
}
function updateWallet() {
  const s = career.on() ? `$${career.money.toLocaleString()}` : '';
  for (const id of ['walletMenu', 'walletGarage']) { const el = $(id); if (el) { el.textContent = s; el.classList.toggle('neg', career.money < 0); } }
  refreshPrices();
}
// Price tags on the garage's locked options.
function refreshPrices() {
  if (!catalog.bodies.length) return;
  const tag = (key, v, base) => { const [c] = career.on() ? priceOf(key, v) : [0]; return c ? `${base} — 🔒 $${c.toLocaleString()}` : base; };
  for (const o of $('g_body').options) o.textContent = tag('body', o.value, (catalog.bodies.find((b) => b.id === o.value) || {}).name || o.value);
  for (const o of $('g_engine').options) { const e = catalog.engines.find((x) => x.id === o.value) || {}; o.textContent = tag('engine', o.value, `${e.name} — ${e.hp} hp`); }
  for (const o of $('g_axle').options) o.textContent = tag('axle', o.value, o.value);
  for (const o of $('g_tread').options) if (o.value === 'paddle') o.textContent = tag('tread', 'paddle', 'Paddles');
  for (const id of ['g_diffF', 'g_diffR']) for (const o of $(id).options) o.textContent = tag(id.slice(2), o.value, o.dataset.base || (o.dataset.base = o.textContent));
}
// Pit crew bill for anything broken (charged when you go back to the garage or start an event).
function repairBill() {
  if (!hud || !career.on()) return 0;
  return (hud.broken ? 1200 : 0) + (hud.shaft ? 300 : 0) + (hud.bead ? 60 : 0) + (hud.beltBurnt ? 250 : 0) + (hud.winch === 3 ? 150 : 0);
}
function chargeRepairs() { const c = repairBill(); if (c) career.add(-c, 'pit crew repairs'); }
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
    `${cfg.awd ? '4WD' : '2WD'} · ${DIFF[cfg.diffF]} / ${DIFF[cfg.diffR]}${cfg.snorkel ? ' · snorkel' : ''}${cfg.bead ? ' · beadlocks' : ''}${cfg.winch ? ' · winch' : ''}`;
  try {
    const sp = JSON.parse(api.spec(cfgString()));
    $('specSheet').innerHTML =
      `<div><small>Class</small><b class="cls-${sp.cls}">${sp.cls}</b></div>` +
      `<div><small>Crawl ratio</small><b>${sp.crawl.toFixed(1)}:1</b></div>` +
      `<div><small>Diff clearance</small><b>${sp.diffClear.toFixed(1)}"</b></div>` +
      `<div><small>Ground pressure</small><b>${sp.kpa} kPa</b></div>` +
      `<div><small>Weight / hp</small><b>${sp.lbhp.toFixed(1)} lb</b></div>` +
      `<div class="wide"><small>Gearing</small><b>${cfg.axle}s on ${cfg.tire}" tires feel like ${sp.eff.toFixed(2)}s on factory tires` +
      `${Math.abs(sp.regear - +cfg.axle) > 0.2 ? ` &mdash; re-gear to ~${sp.regear.toFixed(2)}` : ' &mdash; matched'}</b></div>` +
      (cfg.psi < 8 && !cfg.bead ? '<div class="wide warn">Under 8 psi without beadlocks: you can roll a bead off the rim</div>' : '');
  } catch { $('specSheet').textContent = ''; }
  store.set('cfg', cfg);
  api.setConfig(cfgString());
  if (net.ws && net.ws.readyState === 1) net.send({ t: 'cfg', cfg: cfgString(), name: cfg.name });
}

// ------------------------------------------------------------------------------------------
// Settings
function initSettings() {
  // First run on a phone/tablet: lighter defaults (user changes are remembered after that).
  if (IS_TOUCH && !store.get('settings', null)) store.set('settings', { quality: 0, post: false, ai: 2, tilt: false, tsize: 1 });
  const s = store.get('settings', {});
  for (const k of ['master', 'engine', 'sfx', 'amb']) if (s[k] != null) $('s_' + k).value = s[k];
  if (s.quality != null) $('s_quality').value = s.quality;
  $('s_scale').value = store.get('scale', 1);
  if (s.assist != null) $('s_assist').checked = s.assist;
  if (s.ai != null) $('s_ai').value = s.ai;
  if (s.ghost != null) $('s_ghost').checked = s.ghost;
  if (s.post != null) $('s_post').checked = s.post;
  if (s.tilt != null) $('s_tilt').checked = s.tilt;
  if (s.tsize != null) $('s_tsize').value = s.tsize;
  if (s.career != null) $('s_career').checked = s.career;
  if (s.weather != null) $('s_weather').value = s.weather;
  if (s.tod != null) $('s_tod').value = s.tod;
  $('s_scale').value = store.get('scale', IS_TOUCH ? 0.75 : 1);
  const save = () => {
    store.set('settings', { master: +$('s_master').value, engine: +$('s_engine').value, sfx: +$('s_sfx').value, amb: +$('s_amb').value,
      quality: +$('s_quality').value, assist: $('s_assist').checked, ai: +$('s_ai').value, ghost: $('s_ghost').checked, post: $('s_post').checked,
      tilt: $('s_tilt').checked, tsize: +$('s_tsize').value, career: $('s_career').checked, weather: +$('s_weather').value, tod: +$('s_tod').value });
    api.action('weather:' + $('s_weather').value);
    api.action('tod:' + $('s_tod').value);
    api.action('challenges:' + (store.get('challenges', 0) | 0));
    updateWallet();
    applyUiScale();
    if ($('s_tilt').checked) enableTilt();
    else if (touch.tiltOn) { touch.tiltOn = false; $('tSteer').classList.remove('tilt'); }
    Module._mb_set_post(+$('s_post').checked);
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
let lastSerial = 0, hud = {}, lastReward = null;
function updateHud() {
  try { hud = JSON.parse(api.hud()); } catch { return; }
  if (hud.pause && current === null) { api.setState(3); show('pause'); }
  if (current === 'pause') $('challengeList').innerHTML = hud.mode === 0 && hud.challenges ? '<h3>Open Pits challenges</h3>' +
    hud.challenges.map((c) => `<div class="${c.done ? 'done' : ''}"><b>${c.done ? '✔' : '○'} ${esc(c.name)}</b> <span>$${c.pay}</span><small>${esc(c.hint)}</small></div>`).join('') : '';
  $('padHint').textContent = hud.pad ? `🎮 ${hud.padName.slice(0, 40)}` : 'Connect a gamepad — press any button';
  if (current !== null) return;
  const rn = hud.rpm / hud.redline;
  $('tachFill').style.width = `${Math.min(100, rn * 100)}%`;
  $('hudSpeed').textContent = Math.round(hud.mph);
  $('hudGear').textContent = hud.stalled ? '!' : hud.gear < 0 ? 'R' : hud.gear === 0 ? 'N' : hud.gear;
  $('lampTcase').textContent = ['2H', '4H', '4L'][hud.tcase] || '2H';
  $('lampTcase').classList.toggle('lo', hud.tcase === 2);
  $('lampLockR').textContent = hud.diffR === 'spool' ? 'R-SPOOL' : hud.diffR === 'lsd' ? 'R-LSD' : 'R-LOCK';
  $('lampLockF').textContent = hud.diffF === 'spool' ? 'F-SPOOL' : hud.diffF === 'lsd' ? 'F-LSD' : 'F-LOCK';
  $('lampLockR').classList.toggle('on', hud.lockR);
  $('lampLockF').classList.toggle('on', hud.lockF && hud.tcase > 0);
  $('lampLockR').style.display = hud.diffR === 'open' ? 'none' : '';
  $('lampLockF').style.display = hud.diffF === 'open' ? 'none' : '';
  $('lampWinch').style.display = hud.winch ? '' : 'none';
  $('lampWinch').classList.toggle('on', hud.winch === 2);
  $('lampWinch').classList.toggle('bad', hud.winch === 3);
  $('lampWinch').textContent = hud.winch === 2 ? `WINCH ${Math.round(hud.winchKn)} kN` : hud.winch === 3 ? 'LINE SNAPPED' : 'WINCH';
  const warn = hud.broken ? 'ENGINE DEAD' : hud.flooded ? 'FLOODED' : hud.shaft ? 'FRONT SHAFT' : hud.bead ? 'BEAD OFF' :
    hud.cvt && hud.belt > 120 ? `BELT ${Math.round(hud.belt)}°C` : hud.windup > 0.45 ? 'BINDING' : hud.brakeWet > 0.35 ? 'WET BRAKES' : '';
  $('lampWarn').textContent = warn;
  $('lampWarn').style.display = warn ? '' : 'none';
  $('hudSoil').textContent = hud.soil;
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
    case 6: timer = fmtTime(Math.max(0, hud.limit - hud.elapsed)); sub = `${Math.round(hud.score).toLocaleString()} smoke pts · x${hud.combo.toFixed(1)} · GAS+BRAKE = line lock`; break;
    case 5: timer = fmtTime(hud.elapsed); sub = `$${hud.pot.toLocaleString()} bounty · ${Math.round(hud.distance * 3.281)} ft / 105 ft`; break;
    default: timer = ''; sub = 'Open pits · T = recovery';
  }
  $('hudTimer').textContent = timer;
  $('hudSub').textContent = sub;
  const cd = hud.phase === 1 ? Math.ceil(hud.countdown) : 0;
  $('countdown').textContent = hud.phase === 1 && cd > 0 && cd <= 5 ? cd : '';
  if (hud.rewardSerial !== lastReward) {
    if (lastReward !== null && hud.reward) career.add(hud.reward, 'challenge prize');
    if (lastReward !== null && hud.challenges) store.set('challenges', hud.challenges.reduce((m, c, i) => m | (c.done ? 1 << i : 0), 0));
    lastReward = hud.rewardSerial;
  }
  $('hudMsg').textContent = hud.message || '';
  $('announce').textContent = hud.announce ? `📢 ${hud.announce}` : '';
  $('announce').style.display = hud.announce ? 'block' : 'none';
  $('coach').textContent = hud.coach || '';
  $('coach').style.display = hud.coach ? 'block' : 'none';
  $('stuckHint').style.display = hud.stuck && !hud.coach ? 'block' : 'none';
  if (hud.recover && current === null) openRecover();
  drawMinimap();
  $('others').innerHTML = hud.mode && (hud.standings || []).length > 1
    ? hud.standings.map((r, i) => `<div class="${r.you ? 'you' : ''}">${i + 1}. ${esc(r.name)} <span>${esc(r.text)}</span></div>`).join('') : '';
  if (hud.finishSerial !== lastSerial) {
    lastSerial = hud.finishSerial;
    if (hud.results && hud.results.length) onResults(hud.results);
  }
}

// Minimap: the C++ overview image of the whole map, north-up, following the player at ~200 m across.
let mapImg = null, mapSize = 384;
function ensureMapImage() {
  if (mapImg || !Module._mb_map_res) return;
  const n = Module._mb_map_res(), ptr = Module._mb_map_image();
  if (!n || !ptr) return;
  mapSize = Module._mb_map_size();
  const cv = document.createElement('canvas');
  cv.width = cv.height = n;
  const img = new ImageData(new Uint8ClampedArray(Module.HEAPU8.buffer, ptr, n * n * 4).slice(), n, n);
  cv.getContext('2d').putImageData(img, 0, 0);
  mapImg = cv;
}
function drawMinimap() {
  ensureMapImage();
  const c = $('minimap'), g = c.getContext('2d');
  const W = c.width, view = 200, s = W / view;
  g.clearRect(0, 0, W, W);
  g.save();
  g.beginPath(); g.arc(W / 2, W / 2, W / 2, 0, 7); g.clip();
  const cx = hud.x || 0, cz = hud.z || 0;
  if (mapImg) {
    const k = mapImg.width / mapSize;   // image px per meter
    g.imageSmoothingEnabled = true;
    g.drawImage(mapImg, (cx + mapSize / 2 - view / 2) * k, (cz + mapSize / 2 - view / 2) * k, view * k, view * k, 0, 0, W, W);
  }
  const P = (x, z) => [W / 2 + (x - cx) * s, W / 2 + (z - cz) * s];
  let x, y;
  for (const o of hud.others || []) { g.fillStyle = o.ai ? '#e8e2d0' : '#ff5a14'; [x, y] = P(o.x, o.z); g.beginPath(); g.arc(x, y, 4, 0, 7); g.fill(); }
  g.restore();
  [x, y] = P(cx, cz);
  g.save(); g.translate(x, y); g.rotate(-hud.heading + Math.PI);
  g.fillStyle = '#f2c418'; g.strokeStyle = '#000a'; g.lineWidth = 1.5;
  g.beginPath(); g.moveTo(0, -7); g.lineTo(5, 6); g.lineTo(-5, 6); g.closePath(); g.fill(); g.stroke();
  g.restore();
}

// ------------------------------------------------------------------------------------------
// Ghost replay of your personal best (stored per mode in localStorage as base64 float32).
const PRIMARY = { 1: 'bog_distance', 2: 'circuit_race' };
function f32ToB64(f) { const u = new Uint8Array(f.buffer, f.byteOffset, f.byteLength); let s = ''; for (let i = 0; i < u.length; i += 8192) s += String.fromCharCode.apply(null, u.subarray(i, i + 8192)); return btoa(s); }
function b64ToF32(b) { const s = atob(b); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return new Float32Array(u.buffer); }
function loadGhost(mode) {
  let f = new Float32Array(0);
  try { const g = localStorage.getItem('mb_ghost_' + mode); if (g && $('s_ghost').checked) f = b64ToF32(g); } catch { /* none */ }
  const p = Module._malloc(Math.max(4, f.byteLength));
  Module.HEAPF32.set(f, p >> 2);
  Module._mb_ghost_load(p, f.length);
  Module._free(p);
}
function maybeSaveGhost(mode, results) {
  const board = PRIMARY[mode];
  if (!board) return false;
  // Bog: a full pass (time) beats any distance; otherwise compare distance.
  const r = results.find((x) => x.board === 'bog_time') || results.find((x) => x.board === board);
  if (!r) return false;
  const key = 'ghostbest_' + mode;
  const prev = store.get(key, null);
  const score = r.board === 'bog_time' ? 1000 - r.value : r.board === 'circuit_race' ? -r.value : r.value;
  if (prev != null && prev >= score) return false;
  const len = Module._mb_ghost_trace_len();
  if (len < 16) return false;
  const f = Module.HEAPF32.slice(Module._mb_ghost_trace_ptr() >> 2, (Module._mb_ghost_trace_ptr() >> 2) + len);
  try { localStorage.setItem('mb_ghost_' + mode, f32ToB64(f)); store.set(key, score); return true; } catch { return false; }
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
  const pb = !net.room && maybeSaveGhost(hud.mode, results);
  const st = (hud.standings || []).length > 1 ? `<ol class="standings">${hud.standings.map((r) => `<li class="${r.you ? 'you' : ''}">${esc(r.name)} <span>${esc(r.text)}</span></li>`).join('')}</ol>` : '';
  // Prize money, with a podium bonus when racing rivals.
  let prize = hud.payout || 0;
  const place = (hud.standings || []).findIndex((r) => r.you);
  if ((hud.standings || []).length > 1) prize = Math.round(prize * (place === 0 ? 1.5 : place === 1 ? 1.15 : 1));
  const paid = career.on() && prize > 0;
  if (paid) career.add(prize);
  $('resBody').innerHTML = results.map((r) => `<div>${r.summary}</div><div class="rank" id="rank_${r.board}">&nbsp;</div>`).join('') +
    (paid ? `<div class="prize">+$${prize.toLocaleString()} prize money${place === 0 && hud.standings.length > 1 ? ' (winner bonus)' : ''} · bank $${career.money.toLocaleString()}</div>` : '') +
    (pb ? '<div class="rank">New personal best — ghost saved</div>' : '') + st;
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
      Module._mb_set_ai_count(0);
      loadGhost(-1);
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
  return [...$(current).querySelectorAll('[data-nav]')].filter((e) => e.offsetParent !== null && !e.disabled);
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

// ------------------------------------------------------------------------------------------
// Touch controls: floating steering stick (or tilt), analog pedals, buttons -> C++ input.
const touch = { steer: 0, thr: 0, brk: 0, hb: false, airUp: false, airDown: false, lookX: 0, lookY: 0, tilt: 0, tiltOn: false, tiltZero: null };
function initTouch() {
  if (!IS_TOUCH) return;
  const zone = $('tSteer'), base = $('tBase'), knob = $('tKnob');
  let sid = null, sx = 0;
  const steerAt = (x) => {
    const r = base.clientWidth * 0.5;
    const dx = Math.max(-r, Math.min(r, x - sx));
    touch.steer = -dx / r;   // drag right = steer right (negative = right in the sim)
    knob.style.transform = `translateX(${dx}px)`;
  };
  zone.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    if (touch.tiltOn) return;
    sid = e.pointerId; sx = e.clientX;
    zone.setPointerCapture(sid);
    const rz = zone.getBoundingClientRect();   // stick appears under the thumb
    base.style.left = `${Math.max(8, e.clientX - rz.left - base.clientWidth / 2)}px`;
    base.style.bottom = `${Math.max(8, rz.bottom - e.clientY - base.clientHeight / 2)}px`;
    steerAt(e.clientX);
  });
  zone.addEventListener('pointermove', (e) => { if (e.pointerId === sid) steerAt(e.clientX); });
  const endSteer = (e) => { if (e.pointerId !== sid) return; sid = null; touch.steer = 0; knob.style.transform = ''; base.style.left = ''; base.style.bottom = ''; };
  zone.addEventListener('pointerup', endSteer);
  zone.addEventListener('pointercancel', endSteer);

  // Pedals: higher on the pedal = more throttle/brake (40%..100%), so you can feather it in the slop.
  const pedal = (el, key) => {
    let id = null;
    const val = (e) => { const r = el.getBoundingClientRect(); touch[key] = Math.max(0.4, Math.min(1, 1 - (e.clientY - r.top) / r.height * 0.6)); };
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); id = e.pointerId; el.setPointerCapture(id); el.classList.add('on'); val(e); });
    el.addEventListener('pointermove', (e) => { if (e.pointerId === id) val(e); });
    const up = (e) => { if (e.pointerId !== id) return; id = null; touch[key] = 0; el.classList.remove('on'); };
    el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
  };
  pedal($('tGas'), 'thr');
  pedal($('tBrake'), 'brk');

  document.querySelectorAll('#touch [data-press]').forEach((b) => b.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    Module.ccall('mb_press', null, ['string'], [b.dataset.press]);
    b.classList.add('on'); setTimeout(() => b.classList.remove('on'), 150);
    if (navigator.vibrate) navigator.vibrate(12);
  }));
  const holdMap = { hb: 'hb', airup: 'airUp', airdown: 'airDown' };
  document.querySelectorAll('#touch [data-hold]').forEach((b) => {
    const k = holdMap[b.dataset.hold];
    b.addEventListener('pointerdown', (e) => { e.preventDefault(); b.setPointerCapture(e.pointerId); touch[k] = true; b.classList.add('on'); });
    const up = () => { touch[k] = false; b.classList.remove('on'); };
    b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
  });
  $('tPause').addEventListener('pointerdown', (e) => { e.preventDefault(); if (current === null) { api.setState(3); show('pause'); } });

  // Drag on the open view (canvas) to look around / orbit the garage camera.
  const cv = $('canvas');
  let lid = null, lx = 0, ly = 0;
  cv.addEventListener('pointerdown', (e) => { if (e.pointerType !== 'touch') return; e.preventDefault(); lid = e.pointerId; lx = e.clientX; ly = e.clientY; });
  cv.addEventListener('pointermove', (e) => {
    if (e.pointerId !== lid) return;
    touch.lookX += (e.clientX - lx) * 0.006; touch.lookY += (e.clientY - ly) * 0.004;
    lx = e.clientX; ly = e.clientY;
  });
  const lend = (e) => { if (e.pointerId === lid) lid = null; };
  cv.addEventListener('pointerup', lend); cv.addEventListener('pointercancel', lend);

  const pump = () => {
    requestAnimationFrame(pump);
    if (!Module._mb_touch) return;
    let steer = touch.steer;
    if (touch.tiltOn && touch.tiltZero != null) steer = Math.max(-1, Math.min(1, -(touch.tilt - touch.tiltZero) / 28));
    Module._mb_touch(touch.thr, touch.brk, steer, touch.hb ? 1 : 0, touch.airUp ? 1 : (touch.airDown ? -1 : 0), touch.lookX, touch.lookY);
    touch.lookX = touch.lookY = 0;
  };
  requestAnimationFrame(pump);
}

// Tilt steering: phone held sideways like a wheel. Calibrates "straight" when enabled.
async function enableTilt() {
  if (touch.tiltOn || !IS_TOUCH || !window.DeviceOrientationEvent) return;
  try {
    if (typeof DeviceOrientationEvent.requestPermission === 'function') {   // iOS 13+
      if ((await DeviceOrientationEvent.requestPermission()) !== 'granted') { $('s_tilt').checked = false; return; }
    }
  } catch { $('s_tilt').checked = false; return; }
  touch.tiltOn = true;
  $('tSteer').classList.add('tilt');
  window.addEventListener('deviceorientation', (e) => {
    const angle = (screen.orientation && screen.orientation.angle) || window.orientation || 0;
    const v = angle === 90 ? e.beta : (angle === -90 || angle === 270 ? -e.beta : e.gamma);
    if (v == null) return;
    touch.tilt = v;
    if (touch.tiltZero == null) touch.tiltZero = v;
  });
  toast('Tilt steering on — hold the phone level to go straight');
}
