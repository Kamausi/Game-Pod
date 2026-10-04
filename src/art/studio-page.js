// Bakes one asset: studio.html?asset=<name>[&tone=aces|agx|neutral]. Leaves a PNG data URL on
// window.__studio for scripts/bake-art.mjs, and shows the result for review in a browser.
import '@fontsource/fredoka/latin-600.css'
import '@fontsource/fredoka/latin-700.css'
import hdrUrl from './env/studio.hdr?url'
import { ASSETS } from './manifest.js'
import { bloom, glow, renderAsset, setupStudio } from './studio.js'
import { MODELS } from './models.js'

const SUPERSAMPLE = 2

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
  canvas = bloom(canvas, spec.bloom)
  const g = MODELS[name]().look?.glow
  if (g) canvas = glow(canvas, g)
  document.getElementById('out').append(canvas)
  window.__studio = { done: true, dataUrl: canvas.toDataURL('image/png') }
}

main().catch((err) => {
  console.error(err)
  window.__studio = { done: true, error: String(err.stack ?? err) }
})
