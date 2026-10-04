// Tiny Web Audio layer: synthesized sound effects and a generative background loop (no audio files).

let ctx = null
let master = null
let musicGain = null
let musicTimer = null
let sfxOn = true

function ensure() {
  if (ctx) return ctx
  const AC = window.AudioContext || window.webkitAudioContext
  if (!AC) return null
  ctx = new AC()
  master = ctx.createGain()
  master.gain.value = 0.9
  master.connect(ctx.destination)
  return ctx
}

// Must run inside a user gesture (tap/click/key) or browsers keep audio muted.
export function unlockAudio() {
  const c = ensure()
  if (c?.state === 'suspended') c.resume()
}

function tone({ freq, type = 'sine', start = 0, dur = 0.15, gain = 0.2, slide = 0, dest = master }) {
  const c = ensure()
  if (!c) return
  const t = c.currentTime + start
  const osc = c.createOscillator()
  const g = c.createGain()
  osc.type = type
  osc.frequency.setValueAtTime(freq, t)
  if (slide) osc.frequency.exponentialRampToValueAtTime(freq * slide, t + dur)
  g.gain.setValueAtTime(0.0001, t)
  g.gain.exponentialRampToValueAtTime(gain, t + 0.01)
  g.gain.exponentialRampToValueAtTime(0.0001, t + dur)
  osc.connect(g).connect(dest)
  osc.start(t)
  osc.stop(t + dur + 0.05)
}

const SFX = {
  tap: () => tone({ freq: 660, type: 'triangle', dur: 0.06, gain: 0.08 }),
  place: () => tone({ freq: 420, type: 'triangle', dur: 0.12, gain: 0.18, slide: 1.5 }),
  cpu: () => tone({ freq: 300, type: 'triangle', dur: 0.12, gain: 0.14, slide: 1.3 }),
  win: () => [523, 659, 784, 1047].forEach((f, i) => tone({ freq: f, type: 'triangle', start: i * 0.09, dur: 0.3, gain: 0.16 })),
  lose: () => [392, 330, 262].forEach((f, i) => tone({ freq: f, type: 'sine', start: i * 0.14, dur: 0.35, gain: 0.14 })),
  draw: () => [440, 440].forEach((f, i) => tone({ freq: f, type: 'triangle', start: i * 0.15, dur: 0.18, gain: 0.12 })),
  unlock: () => [784, 988, 1175, 1568].forEach((f, i) => tone({ freq: f, type: 'sine', start: i * 0.07, dur: 0.4, gain: 0.12 })),
}

export function setSoundEnabled(on) {
  sfxOn = on
}

export function sfx(name) {
  if (!sfxOn || !ctx || ctx.state !== 'running') return
  SFX[name]?.()
}

// Four-chord pad with a soft pentatonic arpeggio, scheduled a bar ahead.
const CHORDS = [
  [261.63, 329.63, 392.0, 493.88], // Cmaj7
  [220.0, 261.63, 329.63, 392.0], // Am7
  [174.61, 220.0, 261.63, 329.63], // Fmaj7
  [196.0, 246.94, 293.66, 392.0], // G
]
const BAR = 2.4

export function startMusic(volume) {
  const c = ensure()
  if (!c || musicTimer) return setMusicVolume(volume)
  musicGain = c.createGain()
  musicGain.gain.value = 0
  const filter = c.createBiquadFilter()
  filter.type = 'lowpass'
  filter.frequency.value = 1800
  musicGain.connect(filter).connect(master)
  setMusicVolume(volume)

  let bar = 0
  let nextTime = c.currentTime + 0.1
  const schedule = () => {
    while (nextTime < c.currentTime + BAR) {
      const chord = CHORDS[bar % CHORDS.length]
      const start = nextTime - c.currentTime
      chord.forEach((f) => tone({ freq: f / 2, type: 'sine', start, dur: BAR * 1.05, gain: 0.05, dest: musicGain }))
      for (let i = 0; i < 8; i++) {
        const note = chord[(i * 3 + bar) % chord.length] * (i % 4 === 3 ? 2 : 1)
        tone({ freq: note, type: 'triangle', start: start + (i * BAR) / 8, dur: 0.35, gain: 0.035, dest: musicGain })
      }
      bar++
      nextTime += BAR
    }
  }
  schedule()
  musicTimer = setInterval(schedule, 500)
}

export function setMusicVolume(volume) {
  if (musicGain && ctx) musicGain.gain.setTargetAtTime(volume * 0.6, ctx.currentTime, 0.2)
}

export function stopMusic() {
  if (!musicTimer) return
  clearInterval(musicTimer)
  musicTimer = null
  const g = musicGain
  if (g && ctx) {
    g.gain.setTargetAtTime(0, ctx.currentTime, 0.15)
    setTimeout(() => g.disconnect(), 800)
  }
  musicGain = null
}

export function vibrate(on, ms = 12) {
  if (on) navigator.vibrate?.(ms)
}
