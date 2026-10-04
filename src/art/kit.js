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
export function woodTexture({ base = '#d9934f', dark = '#9a5a26', light = '#f0b877', size = 512, seed = 1, grain = 1 } = {}) {
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
      // grain scales how strongly the lines show (1 = full strength).
      ctx.globalAlpha = grain
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

// ---------- Toy kit: sculpted replacements for raw primitives ----------
// House rule: no foreground object ships as an untouched primitive. These give pieces the
// inflated, heavily bevelled, manufactured-toy forms the key art uses.

/**
 * Pillowy X: one continuous outline (rounded terminals, rounded inner corners) extruded with a
 * deep, many-segment bevel so the whole piece reads as soft moulded plastic.
 */
export function toyXGeometry({ size = 0.8, arm = 0.26, tip = 0.2, depth = 0.08, bevel = 0.09 } = {}) {
  // Arms taper from a thick centre (`arm`) to rounded tips (`tip`), so the X has a heavier middle mass.
  const half = size / 2 - bevel
  const wc = arm / 2 - bevel * 0.6
  const wt = tip / 2 - bevel * 0.6
  const e = half - wt // centre of each tip's round cap
  const s = new THREE.Shape()
  s.moveTo(wc, wc)
  s.lineTo(e, wt)
  s.absarc(e, 0, wt, Math.PI / 2, -Math.PI / 2, true)
  s.lineTo(wc, -wc)
  s.lineTo(wt, -e)
  s.absarc(0, -e, wt, 0, -Math.PI, true)
  s.lineTo(-wc, -wc)
  s.lineTo(-e, -wt)
  s.absarc(-e, 0, wt, -Math.PI / 2, (-3 * Math.PI) / 2, true)
  s.lineTo(-wc, wc)
  s.lineTo(-wt, e)
  s.absarc(0, e, wt, Math.PI, 0, true)
  s.closePath()
  const g = new THREE.ExtrudeGeometry(s, { depth, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 10, curveSegments: 16 })
  g.center()
  g.rotateZ(Math.PI / 4)
  g.rotateX(-Math.PI / 2)
  g.computeVertexNormals()
  return g
}

/** Manufactured O: a ring whose cross-section is a rounded rectangle (flat top, soft edges), not a torus tube. */
export function toyOGeometry({ radius = 0.28, width = 0.24, height = 0.2, round = 0.09 } = {}) {
  const r0 = radius - width / 2
  const pts = []
  const corner = (cx, cy, a0) => {
    for (let i = 0; i <= 8; i++) {
      const a = a0 + (i / 8) * (Math.PI / 2)
      pts.push(new THREE.Vector2(cx + Math.cos(a) * round, cy + Math.sin(a) * round))
    }
  }
  corner(r0 + width - round, round, -Math.PI / 2) // outer bottom
  corner(r0 + width - round, height - round, 0) // outer top
  corner(r0 + round, height - round, Math.PI / 2) // inner top
  corner(r0 + round, round, Math.PI) // inner bottom
  pts.push(pts[0].clone())
  const g = new THREE.LatheGeometry(pts, 96)
  g.translate(0, -height / 2, 0)
  return g
}

/** Checker puck: stepped top (raised inner disc inside a recessed ring), rounded rim lip, ridged side, tucked base. */
export function toyPuckGeometry({ radius = 0.22, height = 0.2 } = {}) {
  const R = radius
  const H = height
  const p = [
    [0, H * 1.0], [R * 0.52, H * 0.99], [R * 0.58, H * 0.95], [R * 0.64, H * 0.9], [R * 0.7, H * 0.93],
    [R * 0.8, H * 0.99], [R * 0.92, H * 0.98], [R * 0.99, H * 0.91], [R * 1.01, H * 0.8],
    [R * 0.985, H * 0.68], [R * 1.01, H * 0.56], [R * 0.985, H * 0.44], [R * 1.01, H * 0.32],
    [R * 0.99, H * 0.12], [R * 0.93, H * 0.02], [R * 0.85, 0], [0, 0],
  ].map(([x, y]) => new THREE.Vector2(x, y))
  // Lathe profiles must run bottom-to-top, or the normals point inward and the piece renders as a hollow cup.
  return new THREE.LatheGeometry(p.reverse(), 64)
}

/**
 * Controlled microvariation so repeated pieces don't read as clones. Deterministic per `seed`.
 * Spec ranges (Game Pod rendering standard): rotation ±1.5°, scale ±1%, height ±0.8%,
 * colour value ±2.5%, roughness ±0.03.
 */
export function vary(object, seed, { rot = 1.5, scale = 0.01, height = 0.008, value = 0.025, rough = 0.03 } = {}) {
  let r = (seed * 9301 + 49297) % 233280 || 1
  const rand = () => ((r = (r * 16807) % 2147483647) / 2147483647) * 2 - 1
  const deg = Math.PI / 180
  object.rotation.x += rand() * rot * deg
  object.rotation.y += rand() * rot * deg
  object.rotation.z += rand() * rot * deg
  const sc = 1 + rand() * scale
  object.scale.multiplyScalar(sc)
  object.scale.y *= 1 + rand() * height
  object.traverse((o) => {
    if (!o.isMesh || !o.material?.isMeshStandardMaterial) return
    o.material = o.material.clone()
    o.material.color.offsetHSL(0, 0, rand() * value)
    o.material.roughness = Math.min(1, Math.max(0, o.material.roughness + rand() * rough))
  })
  return object
}

/** Faint tileable noise for roughness/bump maps: surfaces stop reading as flat shader colour. */
export function noiseTexture({ size = 256, contrast = 0.12, seed = 1 } = {}) {
  let r = seed
  const rand = () => ((r = (r * 16807) % 2147483647) / 2147483647)
  const tex = canvasTexture(size, size, (ctx, W) => {
    const img = ctx.createImageData(W, W)
    for (let i = 0; i < W * W; i++) {
      const v = Math.round(255 * (0.5 + (rand() - 0.5) * contrast))
      img.data[i * 4] = img.data[i * 4 + 1] = img.data[i * 4 + 2] = v
      img.data[i * 4 + 3] = 255
    }
    ctx.putImageData(img, 0, 0)
  })
  tex.colorSpace = THREE.NoColorSpace
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping
  return tex
}

/** Rounded-rectangle path centred on the origin. */
export function roundedRectPath(path, w, h, r) {
  const x = -w / 2
  const y = -h / 2
  path.moveTo(x + r, y)
  path.lineTo(x + w - r, y)
  path.absarc(x + w - r, y + r, r, -Math.PI / 2, 0)
  path.lineTo(x + w, y + h - r)
  path.absarc(x + w - r, y + h - r, r, 0, Math.PI / 2)
  path.lineTo(x + r, y + h)
  path.absarc(x + r, y + h - r, r, Math.PI / 2, Math.PI)
  path.lineTo(x, y + r)
  path.absarc(x + r, y + r, r, Math.PI, Math.PI * 1.5)
  return path
}

/**
 * One-piece tray frame: a rounded-rectangle block with a rounded-rectangle opening, bevelled all
 * round. Rounded outer corners and inner corners, no seams between walls.
 */
export function trayFrameGeometry({ outer = 4, inner = 3.1, height = 0.6, outerRadius = 0.4, innerRadius = 0.1, bevel = 0.07 } = {}) {
  const shape = roundedRectPath(new THREE.Shape(), outer - bevel * 2, outer - bevel * 2, outerRadius)
  shape.holes.push(roundedRectPath(new THREE.Path(), inner + bevel * 2, inner + bevel * 2, innerRadius))
  const g = new THREE.ExtrudeGeometry(shape, {
    depth: height - bevel * 2, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 6, curveSegments: 24,
  })
  g.rotateX(-Math.PI / 2)
  g.translate(0, bevel, 0) // bottom of the bevel sits at y = 0
  g.computeVertexNormals()
  return g
}

/** Closed polygon with each corner rounded by its own radius (quadratic fillets). */
export function roundedPolygonShape(points, radii) {
  const s = new THREE.Shape()
  const n = points.length
  const V = (p) => new THREE.Vector2(p[0], p[1])
  const toward = (a, b, d) => a.clone().add(b.clone().sub(a).setLength(d))
  for (let i = 0; i <= n; i++) {
    const P = V(points[i % n])
    const prev = V(points[(i - 1 + n) % n])
    const next = V(points[(i + 1) % n])
    const r = radii[i % n]
    const pin = toward(P, prev, r)
    const pout = toward(P, next, r)
    if (i === 0) s.moveTo(pout.x, pout.y)
    else {
      s.lineTo(pin.x, pin.y)
      if (i < n) s.quadraticCurveTo(P.x, P.y, pout.x, pout.y)
      else s.quadraticCurveTo(P.x, P.y, pout.x, pout.y)
    }
  }
  return s
}

/**
 * Moulded-toy X: a plus outline with softly rounded square-ish arm ends and small inner fillets,
 * extruded with a bevel taller than it is wide so the top crowns. Turned 45° and laid flat.
 */
export function mouldedXGeometry({ size = 0.9, arm = 0.26, endRadius = 0.05, innerRadius = 0.025, depth = 0.08, bevelHeight = 0.05, bevelWidth = 0.035 } = {}) {
  const L = size / 2 - bevelWidth
  const w = arm / 2 - bevelWidth
  const pts = [[L, w], [L, -w], [w, -w], [w, -L], [-w, -L], [-w, -w], [-L, -w], [-L, w], [-w, w], [-w, L], [w, L], [w, w]]
  const radii = pts.map(([x, y]) => (Math.abs(x) === L || Math.abs(y) === L ? endRadius : innerRadius))
  const g = new THREE.ExtrudeGeometry(roundedPolygonShape(pts, radii), {
    depth, bevelEnabled: true, bevelThickness: bevelHeight, bevelSize: bevelWidth, bevelSegments: 6, curveSegments: 12,
  })
  g.center()
  g.rotateZ(Math.PI / 4)
  g.rotateX(-Math.PI / 2)
  g.computeVertexNormals()
  return g
}
