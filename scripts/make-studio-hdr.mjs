// Generates the studio lighting environment as Radiance .hdr files (equirectangular, RGBE).
// A dark indigo-to-violet dome with a big white key softbox, an overhead strip light, a cool fill,
// and hot-pink / electric-cyan rim lights behind the subject: the colors the UI is built around.
//
//   node scripts/make-studio-hdr.mjs
import { writeFileSync, mkdirSync } from 'node:fs'

// Directions use three.js conventions: +Y up, camera looks toward -Z, so "front" is +Z.
const dir = (azimuthDeg, elevationDeg) => {
  const a = (azimuthDeg * Math.PI) / 180
  const e = (elevationDeg * Math.PI) / 180
  return [Math.cos(e) * Math.sin(a), Math.sin(e), Math.cos(e) * Math.cos(a)]
}

// Softboxes: direction, angular half-size (deg) along the horizontal / vertical axes, edge softness, color.
const LIGHTS = [
  { d: dir(-40, 40), w: 15, h: 11, soft: 4, c: [16, 15.2, 14.2] }, // key: warm-white softbox, front-left above
  { d: dir(10, 70), w: 34, h: 3.5, soft: 2.5, c: [8, 8, 8.6] }, // overhead strip: long specular streak
  { d: dir(65, 10), w: 14, h: 10, soft: 8, c: [1.6, 2.2, 3.8] }, // fill: soft cool blue, front-right
  { d: dir(150, 15), w: 7, h: 16, soft: 5, c: [11, 1.6, 7.5] }, // rim: hot pink, behind-left
  { d: dir(-150, 15), w: 7, h: 16, soft: 5, c: [1.4, 7.5, 11] }, // rim: electric cyan, behind-right
  { d: dir(180, 40), w: 16, h: 5, soft: 6, c: [1.4, 1.0, 2.8] }, // back top: soft violet kicker
]

const sub = (a, b) => a.map((v, i) => v - b[i])
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
const norm = (a) => {
  const l = Math.hypot(...a)
  return a.map((v) => v / l)
}
const smooth = (e0, e1, x) => {
  const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0)))
  return t * t * (3 - 2 * t)
}

// Each light gets a local frame so the softbox can be a rounded rectangle, not just a disc.
for (const L of LIGHTS) {
  const up = Math.abs(L.d[1]) > 0.95 ? [0, 0, 1] : [0, 1, 0]
  L.right = norm(cross(up, L.d))
  L.up = cross(L.d, L.right)
}

function radiance(d) {
  // Dome: indigo above, violet glow at the horizon, deep blue floor.
  const y = d[1]
  const horizon = Math.exp(-Math.abs(y) * 6)
  let c = y > 0
    ? [0.025 + 0.02 * (1 - y), 0.03 + 0.015 * (1 - y), 0.09 + 0.06 * (1 - y)]
    : [0.02, 0.025, 0.06]
  c = c.map((v, i) => v + horizon * [0.16, 0.06, 0.3][i])

  for (const L of LIGHTS) {
    if (dot(d, L.d) <= 0) continue
    // Project onto the light's plane and measure angular offsets along its axes.
    const p = sub(d, L.d.map((v) => v * dot(d, L.d)))
    const ax = (Math.atan2(Math.abs(dot(p, L.right)), dot(d, L.d)) * 180) / Math.PI
    const ay = (Math.atan2(Math.abs(dot(p, L.up)), dot(d, L.d)) * 180) / Math.PI
    const k = (1 - smooth(L.w - L.soft, L.w + L.soft, ax)) * (1 - smooth(L.h - L.soft, L.h + L.soft, ay))
    if (k > 0) c = c.map((v, i) => v + L.c[i] * k)
  }
  return c
}

function encodeRGBE(width, height) {
  const header = Buffer.from(`#?RADIANCE\n# Game Pod studio environment\nFORMAT=32-bit_rle_rgbe\n\n-Y ${height} +X ${width}\n`, 'ascii')
  const data = Buffer.alloc(width * height * 4)
  for (let y = 0; y < height; y++) {
    // Row 0 is the top of the image; the loader flips it so v=1 is up.
    const v = 1 - (y + 0.5) / height
    const theta = (v - 0.5) * Math.PI
    for (let x = 0; x < width; x++) {
      const u = (x + 0.5) / width
      const phi = (u - 0.5) * Math.PI * 2
      // Inverse of three.js equirectUv: u = atan(z, x) / 2π + 0.5, v = asin(y) / π + 0.5.
      const d = [Math.cos(theta) * Math.cos(phi), Math.sin(theta), Math.cos(theta) * Math.sin(phi)]
      const [r, g, b] = radiance(d)
      const m = Math.max(r, g, b)
      const i = (y * width + x) * 4
      if (m < 1e-32) continue
      const e = Math.ceil(Math.log2(m) + 1e-9)
      const scale = 256 / 2 ** e
      data[i] = Math.min(255, Math.floor(r * scale))
      data[i + 1] = Math.min(255, Math.floor(g * scale))
      data[i + 2] = Math.min(255, Math.floor(b * scale))
      data[i + 3] = e + 128
    }
  }
  return Buffer.concat([header, data])
}

mkdirSync('src/art/env', { recursive: true })
// Full size for baking art; a small one is plenty for live reflections in the game scene.
writeFileSync('src/art/env/studio.hdr', encodeRGBE(1024, 512))
writeFileSync('src/art/env/studio-small.hdr', encodeRGBE(256, 128))
console.log('wrote src/art/env/studio.hdr and studio-small.hdr')
