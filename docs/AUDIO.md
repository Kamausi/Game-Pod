# Audio: procedural synthesis

Every sample is synthesized in C++ from simulation state (`src/audio/`). The browser's Web Audio
graph only pulls interleaved stereo blocks from `mb_audio_render()`. No recorded samples ship.

## Engine (`EngineVoice`)
1. **Crank phase** over the 720° four-stroke cycle: `φ += 720 · (rpm/120) / f_s`.
2. **Combustion pulses** — each cylinder fires at its real angle (GM V8 1-8-4-3-6-5-7-2; Jeep I6
   1-5-3-6-2-4; diesel 4 for the dozer; 270° twin for the UTV), as a raised-cosine pulse
   75° wide, routed to its **exhaust bank**. A cross-plane V8's banks receive pulses at uneven
   90/180/270° spacing — that is where the burble comes from, not a filter.
3. **Cycle-to-cycle variation**: ±13 % amplitude jitter; open-header cams drop ~22 % of idle
   pulses to half strength (lope); fuel-cut cycles occasionally fire 3× in the header (pops).
4. **Exhaust waveguide** per bank: delay `2L/c_gas` (c ≈ 480 m/s in hot gas), open-end reflection
   −0.62…−0.74, loss low-pass 1.6–4.2 kHz scaled by "openness".
5. Muffler/tone low-pass, body resonance peak (110 Hz V8, 150 Hz I6), Padé-tanh soft clip with
   drive rising with openness, DC blocker.
6. Extras: induction roar (band-passed noise × throttle × rpm), mechanical hiss, diesel
   injector clatter (1.5 ms clicks per firing), Roots blower whine (3.6 × crank),
   turbo whistle (1.8–8.8 kHz with boost²) and blow-off on lift, backfire thump + crackle.

## Tires, mud and water (`RoadVoice`)
* **Lug slap grains** at `f = ω · lugs / 2π` (capped 95 Hz): low-passed noise burst
  (cutoff 240–760 Hz, 16 ms decay) + a 55–110 Hz "thwop" sine (30 ms). Intensity from slip and
  mud weight. Paddles (12 lugs) give the slow heavy whump, A/Ts (26) a buzz.
* **Squelch**: band-passed brown noise (260 Hz, Q 1.8) with a wandering modulator.
* **Water**: high-passed wash + **Minnaert bubbles** `f₀ = (1/2πr)·√(3γp₀/ρ) ≈ 3.26/r Hz`
  for r = 1.2–8 mm, each with van den Doel's damping `d = 0.13 f + 0.0072 f^1.5` and a rising chirp.
* Firm ground: rolling rumble, M/T **tread hum** at the lug passing frequency, squeal on slip.
* **Debris patter**: Poisson clicks at a rate ∝ roost volume. **Belly scrape**: band noise.

## One-shots (`SfxType`)
Impact (decaying 75→40 Hz thud + clank), Splash (two-band noise burst + droplet ticks),
**Barrel** (modal synthesis of a steel drum: modes 1 : 1.59 : 2.3), Slurp (down-swept resonant
band-pass 900→200 Hz), Fence rattle, Shift clunk, two-tone Horn.

## Space and mix
* Distance gain `1 / (1 + max(0, d − 6)/12)`, equal-power pan, air absorption low-pass
  `16 kHz / (1 + d/25)`, **Doppler** `(c + v_l·r̂)/(c + v_s·r̂)` on remote trucks.
* Ambience: idling dozer at its world position, crowd (pink noise through 650 Hz / 1.8 kHz
  formants, cheer envelope + whistles), wind ∝ speed.
* **Slapback** off the tree line: 190 / 230 ms cross-fed delays, 2.2 kHz damping.
* Peak limiter (instant attack, 150 ms release) at −0.9 dBFS.

## Calibrating against your footage
```
ffmpeg -i clip.mp4 -ac 1 -ar 48000 -sample_fmt s16 clip.wav
./build-native/audio_probe clip.wav 8 clip.csv
./build-native/render_wav v8_350 synth.wav && ./build-native/audio_probe synth.wav 8 synth.csv
```
Compare per-frame firing frequency / RPM, spectral centroid (brightness: open headers ≈ high,
muffled ≈ low), onset rate (lug slap, limiter bounce) and band energy split, then adjust
`EngineSpec::openness`, `pipeLen`, `RoadVoice` grain cutoffs and levels. Note: on cross-plane V8s
the harmonic-product estimator can lock to the 720° cycle (rpm/120) sub-harmonic; read the
firing order from the dominant harmonic family rather than the single peak.
