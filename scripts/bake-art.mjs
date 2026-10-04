// Bakes 3D models to transparent WebP images, one asset at a time.
//
//   npm run bake -- tic-tac-toe           bake one asset into src/assets/art/
//   npm run bake -- tic-tac-toe --compare  also write ACES / AgX / Neutral tone-mapping variants for review
//   npm run bake -- --all                 bake everything in src/art/manifest.js
//
// Uses Vite to serve studio.html and Playwright's Chromium to render it (headless WebGL).
import { execFileSync } from 'node:child_process'
import { mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const args = process.argv.slice(2)
const compare = args.includes('--compare')
const outDir = 'src/assets/art'
const reviewDir = process.env.BAKE_REVIEW_DIR ?? join(tmpdir(), 'gamepod-bake-review')

const { ASSETS } = await import('../src/art/manifest.js')
const names = args.includes('--all') ? Object.keys(ASSETS) : args.filter((a) => !a.startsWith('--'))
if (!names.length) {
  console.error('Usage: npm run bake -- <asset> [...] | --all  (assets listed in src/art/manifest.js)')
  process.exit(1)
}

mkdirSync(outDir, { recursive: true })
mkdirSync(reviewDir, { recursive: true })
const server = await createServer({ server: { port: 5310, strictPort: false }, logLevel: 'error' })
await server.listen()
const base = server.resolvedUrls.local[0]
const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] })
const page = await browser.newPage()

async function bake(name, tone) {
  await page.goto(`${base}studio.html?asset=${encodeURIComponent(name)}${tone ? `&tone=${tone}` : ''}`)
  await page.waitForFunction(() => window.__studio?.done, null, { timeout: 180000 })
  const result = await page.evaluate(() => window.__studio)
  if (result.error) throw new Error(`${name}: ${result.error}`)
  const png = join(tmpdir(), `bake-${name}-${tone ?? 'final'}.png`)
  writeFileSync(png, Buffer.from(result.dataUrl.split(',')[1], 'base64'))
  return png
}

let failed = false
for (const name of names) {
  try {
    if (compare) {
      for (const tone of ['aces', 'agx', 'neutral']) {
        const png = await bake(name, tone)
        execFileSync('convert', [png, '-filter', 'Lanczos', '-resize', '50%', join(reviewDir, `${name}-${tone}.png`)])
        rmSync(png)
      }
      console.log(`compared ${name} -> ${reviewDir}`)
      continue
    }
    const png = await bake(name)
    const out = join(outDir, `${name}.webp`)
    // Downsample the 2x render with Lanczos for clean edges; keep alpha.
    execFileSync('convert', [png, '-filter', 'Lanczos', '-resize', '50%', '-quality', '90', '-define', 'webp:alpha-quality=95', out])
    execFileSync('convert', [png, '-filter', 'Lanczos', '-resize', '50%', join(reviewDir, `${name}.png`)])
    rmSync(png)
    console.log(`baked ${out}`)
  } catch (err) {
    failed = true
    console.error(String(err.message ?? err))
  }
}

await browser.close()
await server.close()
process.exit(failed ? 1 : 0)
