// Small helpers for building the glossy toy-style 3D models used across the app.
import * as THREE from 'three'
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js'

export { THREE }

// Glossy plastic by default; pass overrides for wood, metal, felt, etc.
export const mat = (color, o = {}) =>
  new THREE.MeshPhysicalMaterial({ color, roughness: 0.32, metalness: 0, clearcoat: 0.7, clearcoatRoughness: 0.18, ...o })

export const wood = (color = '#c98a4b') => mat(color, { roughness: 0.55, clearcoat: 0.35, clearcoatRoughness: 0.4 })
export const metal = (color = '#dfe6f0') => mat(color, { metalness: 1, roughness: 0.18, clearcoat: 0 })
export const matte = (color) => mat(color, { roughness: 0.8, clearcoat: 0 })

export function mesh(geo, material, pos = [0, 0, 0], rot = [0, 0, 0], scale = 1) {
  const m = new THREE.Mesh(geo, material)
  m.position.set(...pos)
  m.rotation.set(...rot)
  if (Array.isArray(scale)) m.scale.set(...scale)
  else m.scale.setScalar(scale)
  return m
}

export function group(children = [], pos = [0, 0, 0], rot = [0, 0, 0], scale = 1) {
  const g = new THREE.Group()
  children.forEach((c) => c && g.add(c))
  g.position.set(...pos)
  g.rotation.set(...rot)
  if (Array.isArray(scale)) g.scale.set(...scale)
  else g.scale.setScalar(scale)
  return g
}

export const rbox = (w, h, d, r = 0.08, seg = 4) => new RoundedBoxGeometry(w, h, d, seg, Math.min(r, w / 2, h / 2, d / 2) * 0.99)
export const cyl = (rt, rb, h, seg = 48) => new THREE.CylinderGeometry(rt, rb, h, seg)
export const sphere = (r, seg = 40) => new THREE.SphereGeometry(r, seg, Math.round(seg * 0.75))
export const torus = (r, tube, seg = 64) => new THREE.TorusGeometry(r, tube, 24, seg)
export const cone = (r, h, seg = 32) => new THREE.ConeGeometry(r, h, seg)
export const capsule = (r, len) => new THREE.CapsuleGeometry(r, len, 12, 32)

export function canvasTexture(w, h, draw) {
  const c = document.createElement('canvas')
  c.width = w
  c.height = h
  draw(c.getContext('2d'), w, h)
  const t = new THREE.CanvasTexture(c)
  t.colorSpace = THREE.SRGBColorSpace
  t.anisotropy = 8
  return t
}

// A flat textured square lying on top of a surface (y up), e.g. a number on a tile.
export function decal(size, draw, pos, { res = 256, rotY = 0, upright = false, transparent = true } = {}) {
  const tex = canvasTexture(res, res, draw)
  const m = new THREE.Mesh(
    new THREE.PlaneGeometry(size, size),
    new THREE.MeshPhysicalMaterial({ map: tex, transparent, roughness: 0.4, clearcoat: 0.4, depthWrite: !transparent, polygonOffset: true, polygonOffsetFactor: -2 }),
  )
  m.position.set(...pos)
  if (upright) m.rotation.set(0, rotY, 0)
  else m.rotation.set(-Math.PI / 2, 0, rotY)
  return m
}

export function text(ctx, str, x, y, { size = 120, color = '#fff', weight = 700, stroke = null, strokeWidth = 0, font = 'Fredoka' } = {}) {
  ctx.font = `${weight} ${size}px ${font}, system-ui, sans-serif`
  ctx.textAlign = 'center'
  ctx.textBaseline = 'middle'
  if (stroke) {
    ctx.lineJoin = 'round'
    ctx.lineWidth = strokeWidth
    ctx.strokeStyle = stroke
    ctx.strokeText(str, x, y)
  }
  ctx.fillStyle = color
  ctx.fillText(str, x, y)
}

export function starShape(outer = 1, inner = 0.45, points = 5) {
  const s = new THREE.Shape()
  for (let i = 0; i < points * 2; i++) {
    const r = i % 2 ? inner : outer
    const a = (i / (points * 2)) * Math.PI * 2 + Math.PI / 2
    const x = Math.cos(a) * r
    const y = Math.sin(a) * r
    if (i === 0) s.moveTo(x, y)
    else s.lineTo(x, y)
  }
  s.closePath()
  return s
}

export const extrude = (shape, depth = 0.3, bevel = 0.08) =>
  new THREE.ExtrudeGeometry(shape, { depth, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 4, curveSegments: 24 })

// Suit symbols drawn with canvas paths so they don't depend on font glyphs.
export function drawSuit(ctx, suit, cx, cy, s, color) {
  ctx.save()
  ctx.translate(cx, cy)
  ctx.scale(s, s)
  ctx.fillStyle = color
  ctx.beginPath()
  if (suit === 'heart') {
    ctx.moveTo(0, 0.35)
    ctx.bezierCurveTo(-0.55, -0.05, -0.45, -0.55, 0, -0.25)
    ctx.bezierCurveTo(0.45, -0.55, 0.55, -0.05, 0, 0.35)
  } else if (suit === 'diamond') {
    ctx.moveTo(0, -0.45)
    ctx.lineTo(0.32, 0)
    ctx.lineTo(0, 0.45)
    ctx.lineTo(-0.32, 0)
  } else {
    // spade
    ctx.moveTo(0, -0.42)
    ctx.bezierCurveTo(0.5, -0.05, 0.42, 0.32, 0.08, 0.2)
    ctx.lineTo(0.16, 0.42)
    ctx.lineTo(-0.16, 0.42)
    ctx.lineTo(-0.08, 0.2)
    ctx.bezierCurveTo(-0.42, 0.32, -0.5, -0.05, 0, -0.42)
  }
  ctx.closePath()
  ctx.fill()
  ctx.restore()
}

// Varnished wood: warm base with long wavy grain lines and a few darker streaks.
export function woodTexture({ base = '#d9934f', dark = '#9a5a26', light = '#f0b877', size = 512, seed = 1 } = {}) {
  let r = seed
  const rand = () => ((r = (r * 16807) % 2147483647) / 2147483647)
  return canvasTexture(size, size, (ctx, W, H) => {
    const g = ctx.createLinearGradient(0, 0, W, H)
    g.addColorStop(0, light)
    g.addColorStop(0.5, base)
    g.addColorStop(1, light)
    ctx.fillStyle = g
    ctx.fillRect(0, 0, W, H)
    for (let i = 0; i < 70; i++) {
      const y0 = rand() * H
      const amp = 4 + rand() * 10
      const freq = 0.004 + rand() * 0.01
      const phase = rand() * 10
      ctx.strokeStyle = i % 7 === 0 ? dark : `rgba(120,60,20,${0.08 + rand() * 0.16})`
      ctx.lineWidth = i % 7 === 0 ? 2.2 : 1 + rand() * 1.5
      ctx.beginPath()
      for (let x = -10; x <= W + 10; x += 8) {
        const y = y0 + Math.sin(x * freq + phase) * amp + Math.sin(x * freq * 3.1 + phase) * amp * 0.25
        x === -10 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)
      }
      ctx.stroke()
    }
  })
}

export function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath()
  ctx.roundRect(x, y, w, h, r)
}
