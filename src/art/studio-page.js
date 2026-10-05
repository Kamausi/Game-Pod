// Bakes one asset: studio.html?asset=<name>[&tone=aces|agx|neutral]. Leaves a PNG data URL on
// window.__studio for scripts/bake-art.mjs, and shows the result for review in a browser.
import '@fontsource/fredoka/latin-600.css'
import '@fontsource/fredoka/latin-700.css'
import hdrUrl from './env/studio.hdr?url'
import { ASSETS } from './manifest.js'
import { bloom, glow, renderAsset, setupStudio } from './studio.js'
import { MODELS } from './models.js'

const SUPERSAMPLE = 2

// Bakes must be repeatable: seed Math.random (ambient-occlusion noise, painted textures) so the same
// model always renders to the same pixels.
let seed = 1234567
Math.random = () => ((seed = (seed * 16807) % 2147483647) - 1) / 2147483646

async function main() {
  const params = new URLSearchParams(location.search)
  const name = params.get('asset')
  globalThis.__BAKE_DEBUG = params.has('debug')
  const spec = ASSETS[name]
  if (!spec) throw new Error(`Unknown asset ${name}`)
  await Promise.all([document.fonts.load('700 64px Fredoka'), document.fonts.load('600 64px Fredoka')])
  await setupStudio(hdrUrl)
  const tone = params.get('tone') ?? undefined
  const width = spec.width * SUPERSAMPLE
  const height = spec.height * SUPERSAMPLE
  let canvas = renderAsset(name, { width, height, tone })
  const look = MODELS[name]().look ?? {}
  canvas = bloom(canvas, look.bloom ?? spec.bloom)
  const g = look.glow
  if (g) canvas = glow(canvas, g)
  // Photographic softness: blend toward a blurred copy with distance from the centre (look.soften:
  // { blur, inner, outer } in fractions of the frame).
  if (look.soften) {
    const { blur = 0.006, inner = 0.3, outer = 0.75 } = look.soften
    const w = canvas.width, h = canvas.height
    const soft = document.createElement('canvas')
    soft.width = w
    soft.height = h
    const sctx = soft.getContext('2d')
    // Blur an edge-padded copy (border pixels stretched outward) so the frame's edges don't darken by
    // blurring in transparent pixels from outside it.
    const px = Math.max(1, Math.round(w * blur))
    const margin = px * 3
    const pad = document.createElement('canvas')
    pad.width = w + 2 * margin
    pad.height = h + 2 * margin
    const pctx = pad.getContext('2d')
    pctx.drawImage(canvas, margin, margin)
    pctx.drawImage(canvas, 0, 0, w, 1, margin, 0, w, margin) // top
    pctx.drawImage(canvas, 0, h - 1, w, 1, margin, margin + h, w, margin) // bottom
    pctx.drawImage(pad, margin, 0, 1, h + 2 * margin, 0, 0, margin, h + 2 * margin) // left (incl. corners)
    pctx.drawImage(pad, margin + w - 1, 0, 1, h + 2 * margin, margin + w, 0, margin, h + 2 * margin) // right (incl. corners)
    const blurred = document.createElement('canvas')
    blurred.width = pad.width
    blurred.height = pad.height
    const bctx = blurred.getContext('2d')
    bctx.filter = `blur(${px}px)`
    bctx.drawImage(pad, 0, 0)
    sctx.drawImage(blurred, -margin, -margin)
    const sharp = document.createElement('canvas')
    sharp.width = w
    sharp.height = h
    const hctx = sharp.getContext('2d')
    hctx.drawImage(canvas, 0, 0)
    hctx.globalCompositeOperation = 'destination-in'
    const m = hctx.createRadialGradient(w / 2, h * 0.45, w * inner, w / 2, h * 0.45, w * outer)
    m.addColorStop(0, 'rgba(0,0,0,1)')
    m.addColorStop(1, 'rgba(0,0,0,0)')
    hctx.fillStyle = m
    hctx.fillRect(0, 0, w, h)
    sctx.drawImage(sharp, 0, 0)
    canvas = soft
  }
  document.getElementById('out').append(canvas)
  window.__studio = { done: true, dataUrl: canvas.toDataURL('image/png') }
}

main().catch((err) => {
  console.error(err)
  window.__studio = { done: true, error: String(err.stack ?? err) }
})
