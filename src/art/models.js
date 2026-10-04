// 3D model builders for every game tile, category icon and header decoration.
// Each returns { object, view } where view = { pitch, yaw, fill } positions the camera.
import {
  THREE, mat, wood, metal, matte, mesh, group, rbox, cyl, sphere, torus, cone, capsule,
  canvasTexture, decal, text, starShape, extrude, drawSuit, roundRect, woodTexture,
} from './kit.js'

const C = {
  red: '#ff2f45', blue: '#2a6bff', yellow: '#ffc51f', green: '#33c94f', orange: '#ff8a1f',
  purple: '#8b45ff', pink: '#ff3df2', cyan: '#22c8ff', white: '#f6f7ff', black: '#1a1b26',
}

// ---------- reusable parts ----------

function xPiece(color = C.blue, s = 1) {
  const m = mat(color)
  return group([
    mesh(rbox(0.82, 0.2, 0.2, 0.09), m, [0, 0, 0], [0, Math.PI / 4, 0]),
    mesh(rbox(0.82, 0.2, 0.2, 0.09), m, [0, 0, 0], [0, -Math.PI / 4, 0]),
  ], [0, 0, 0], [0, 0, 0], s)
}
const oPiece = (color = C.red, s = 1) => mesh(torus(0.29, 0.1), mat(color), [0, 0, 0], [Math.PI / 2, 0, 0], s)

function card(face, { w = 1.4, h = 2, back = false } = {}) {
  const body = mesh(rbox(w, h, 0.05, 0.12), mat('#ffffff', { roughness: 0.4, clearcoat: 0.3 }))
  const tex = canvasTexture(280, 400, (ctx, W, H) => {
    if (back) {
      ctx.fillStyle = '#2449c9'
      roundRect(ctx, 14, 14, W - 28, H - 28, 22)
      ctx.fill()
      ctx.strokeStyle = 'rgba(255,255,255,.5)'
      ctx.lineWidth = 6
      for (let i = -H; i < W + H; i += 34) {
        ctx.beginPath()
        ctx.moveTo(i, 0)
        ctx.lineTo(i + H, H)
        ctx.stroke()
      }
      return
    }
    face(ctx, W, H)
  })
  const front = new THREE.Mesh(
    new THREE.PlaneGeometry(w * 0.94, h * 0.95),
    new THREE.MeshPhysicalMaterial({ map: tex, transparent: true, roughness: 0.35, clearcoat: 0.5, polygonOffset: true, polygonOffsetFactor: -2 }),
  )
  front.position.z = 0.03
  return group([body, front])
}

function rankFace(rank, suit) {
  const color = suit === 'spade' ? '#14151f' : '#e3203a'
  return (ctx, W, H) => {
    text(ctx, rank, 50, 56, { size: 76, color })
    drawSuit(ctx, suit, 50, 120, 58, color)
    drawSuit(ctx, suit, W / 2, H / 2 + 10, rank === 'A' ? 190 : 150, color)
    ctx.save()
    ctx.translate(W, H)
    ctx.rotate(Math.PI)
    text(ctx, rank, 50, 56, { size: 76, color })
    drawSuit(ctx, suit, 50, 120, 58, color)
    ctx.restore()
  }
}

function die(s = 1) {
  const body = mesh(rbox(1, 1, 1, 0.18, 5), mat('#ffffff', { roughness: 0.25 }))
  const pip = sphere(0.085, 20)
  const ink = mat('#15161f')
  const LAYOUT = {
    3: [[-0.25, -0.25], [0, 0], [0.25, 0.25]],
    4: [[-0.25, -0.25], [0.25, -0.25], [-0.25, 0.25], [0.25, 0.25]],
    5: [[-0.25, -0.25], [0.25, -0.25], [0, 0], [-0.25, 0.25], [0.25, 0.25]],
  }
  // Pips are spheres squashed flat against the face they sit on.
  const face = (n, place, flat) => LAYOUT[n].map(([a, b]) => mesh(pip, ink, place(a, b), [0, 0, 0], flat))
  return group([
    body,
    ...face(5, (a, b) => [a, 0.49, b], [1, 0.4, 1]),
    ...face(3, (a, b) => [a, b, 0.49], [1, 1, 0.4]),
    ...face(4, (a, b) => [0.49, b, a], [0.4, 1, 1]),
  ], [0, 0, 0], [0, 0, 0], s)
}

function gem(color, s = 1) {
  const g = new THREE.OctahedronGeometry(0.6, 0)
  g.scale(1, 1.25, 1)
  return mesh(g, mat(color, { roughness: 0.1, metalness: 0.15, clearcoat: 1, flatShading: true, emissive: color, emissiveIntensity: 0.25 }), [0, 0, 0], [0.3, 0.5, 0.2], s)
}

function star3d(color = C.yellow, s = 1) {
  const g = extrude(starShape(0.9, 0.42), 0.22, 0.12)
  g.center()
  return mesh(g, mat(color, { roughness: 0.22, emissive: '#ff9a00', emissiveIntensity: 0.12 }), [0, 0, 0], [0, 0, 0], s)
}

function basketballTexture() {
  return canvasTexture(1024, 512, (ctx, W, H) => {
    ctx.fillStyle = '#ff7a1a'
    ctx.fillRect(0, 0, W, H)
    // pebbled grain
    for (let i = 0; i < 9000; i++) {
      ctx.fillStyle = `rgba(120,40,0,${Math.random() * 0.12})`
      ctx.fillRect(Math.random() * W, Math.random() * H, 3, 3)
    }
    ctx.strokeStyle = '#2a1206'
    ctx.lineWidth = 14
    ctx.beginPath()
    ctx.moveTo(0, H / 2)
    ctx.lineTo(W, H / 2)
    ;[0.25, 0.75].forEach((u) => {
      ctx.moveTo(W * u, 0)
      ctx.lineTo(W * u, H)
    })
    ctx.stroke()
    ctx.beginPath()
    for (let x = 0; x <= W; x += 8) {
      const y = H / 2 + Math.sin((x / W) * Math.PI * 2) * H * 0.32
      x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)
    }
    ctx.stroke()
  })
}

const basketball = (s = 1) => mesh(sphere(1, 64), mat('#ffffff', { map: basketballTexture(), roughness: 0.75, clearcoat: 0.15 }), [0, 0, 0], [0.3, 0.6, 0.2], s)

function puzzleShape(t, r, b, l) {
  // Unit jigsaw piece centered on the origin; each side is a tab (1), blank (-1) or flat (0).
  const s = new THREE.Shape()
  const side = (x0, y0, x1, y1, kind) => {
    if (!kind) return s.lineTo(x1, y1)
    const dx = x1 - x0
    const dy = y1 - y0
    const nx = -dy * kind
    const ny = dx * kind
    const p = (u, v) => [x0 + dx * u + nx * v, y0 + dy * u + ny * v]
    s.lineTo(...p(0.36, 0))
    s.bezierCurveTo(...p(0.4, 0.12), ...p(0.28, 0.3), ...p(0.5, 0.3))
    s.bezierCurveTo(...p(0.72, 0.3), ...p(0.6, 0.12), ...p(0.64, 0))
    s.lineTo(x1, y1)
  }
  s.moveTo(-0.5, -0.5)
  side(-0.5, -0.5, 0.5, -0.5, b)
  side(0.5, -0.5, 0.5, 0.5, r)
  side(0.5, 0.5, -0.5, 0.5, t)
  side(-0.5, 0.5, -0.5, -0.5, l)
  return s
}

function puzzlePiece(color, sides, s = 1) {
  const g = extrude(puzzleShape(...sides), 0.16, 0.05)
  g.translate(0, 0, -0.08)
  return mesh(g, mat(color, { roughness: 0.4 }), [0, 0, 0], [-Math.PI / 2, 0, 0], s)
}

function gamepad(s = 1) {
  const body = mat('#2f5bff', { roughness: 0.25 })
  const dark = mat('#14183a')
  return group([
    mesh(rbox(2.2, 0.5, 1.1, 0.25), body),
    mesh(sphere(0.62), body, [-0.85, -0.05, 0.25], [0, 0, 0], [1, 0.42, 1.05]),
    mesh(sphere(0.62), body, [0.85, -0.05, 0.25], [0, 0, 0], [1, 0.42, 1.05]),
    mesh(rbox(0.5, 0.14, 0.16, 0.05), dark, [-0.62, 0.28, 0]),
    mesh(rbox(0.16, 0.14, 0.5, 0.05), dark, [-0.62, 0.28, 0]),
    mesh(sphere(0.11), mat(C.yellow), [0.62, 0.28, -0.18]),
    mesh(sphere(0.11), mat(C.green), [0.82, 0.28, 0]),
    mesh(sphere(0.11), mat(C.red), [0.62, 0.28, 0.18]),
    mesh(sphere(0.11), mat(C.cyan), [0.42, 0.28, 0]),
  ], [0, 0, 0], [0, 0, 0], s)
}

function joystick(s = 1) {
  return group([
    mesh(rbox(1.8, 0.45, 1.4, 0.16), mat('#1e1f2e', { roughness: 0.35 })),
    mesh(cyl(0.3, 0.36, 0.12), metal('#9aa3b8'), [-0.25, 0.28, 0]),
    mesh(cyl(0.07, 0.07, 1.0), metal('#c9d0de'), [-0.25, 0.8, 0], [0, 0, 0.12]),
    mesh(sphere(0.32), mat('#ff2436', { roughness: 0.15 }), [-0.31, 1.32, 0]),
    mesh(cyl(0.17, 0.17, 0.12), mat(C.yellow), [0.5, 0.28, -0.25]),
    mesh(cyl(0.17, 0.17, 0.12), mat(C.cyan), [0.5, 0.28, 0.3]),
  ], [0, 0, 0], [0, 0, 0], s)
}

function trophy(s = 1) {
  const gold = metal('#ffc533')
  // Cup profile from the stem up to the rim, spun around the y axis.
  const cup = new THREE.LatheGeometry(
    [[0.08, 0], [0.2, 0.06], [0.42, 0.25], [0.58, 0.55], [0.64, 0.9], [0.66, 1.05], [0.6, 1.08]].map(([x, y]) => new THREE.Vector2(x, y)),
    48,
  )
  return group([
    mesh(cup, gold, [0, 0.75, 0]),
    mesh(torus(0.26, 0.07), gold, [-0.68, 1.4, 0]),
    mesh(torus(0.26, 0.07), gold, [0.68, 1.4, 0]),
    mesh(cyl(0.09, 0.14, 0.5), gold, [0, 0.5, 0]),
    mesh(rbox(0.8, 0.2, 0.6, 0.06), gold, [0, 0.2, 0]),
    mesh(rbox(1.0, 0.18, 0.75, 0.05), wood('#4a2c14'), [0, 0.02, 0]),
  ], [0, 0, 0], [0, 0, 0], s)
}

function target(s = 1) {
  const rings = [
    [1.0, '#ffffff'], [0.82, C.red], [0.62, '#ffffff'], [0.42, C.red], [0.2, '#ffffff'],
  ].map(([r, c], i) => mesh(cyl(r, r, 0.14 + i * 0.02), mat(c, { roughness: 0.4 }), [0, i * 0.02, 0]))
  const dart = group([
    mesh(cyl(0.04, 0.04, 0.9), metal('#aeb6c8'), [0, 0.45, 0]),
    mesh(cone(0.05, 0.16), metal(), [0, -0.05, 0], [Math.PI, 0, 0]),
    mesh(new THREE.PlaneGeometry(0.3, 0.32), mat(C.red, { side: THREE.DoubleSide }), [0, 0.82, 0]),
    mesh(new THREE.PlaneGeometry(0.3, 0.32), mat(C.red, { side: THREE.DoubleSide }), [0, 0.82, 0], [0, Math.PI / 2, 0]),
  ], [0.15, 0.25, 0.1], [0.5, 0, -0.45])
  return group([group(rings, [0, 0, 0], [Math.PI / 2, 0, 0]), group([dart], [0, 0, 0], [Math.PI / 2, 0, 0])], [0, 0, 0], [0, 0, 0], s)
}

function mole(s = 1) {
  const fur = mat('#8a5530', { roughness: 0.6, clearcoat: 0.2 })
  const light = mat('#d29a68', { roughness: 0.6, clearcoat: 0.2 })
  const white = mat('#ffffff', { roughness: 0.2 })
  const black = mat('#111')
  return group([
    mesh(capsule(0.62, 0.7), fur, [0, 0.62, 0]),
    mesh(sphere(0.32), light, [0, 0.82, 0.45], [0, 0, 0], [1.1, 0.8, 0.8]),
    mesh(sphere(0.11), mat('#2a140a'), [0, 0.95, 0.72]),
    mesh(sphere(0.2), white, [-0.22, 1.22, 0.45]),
    mesh(sphere(0.2), white, [0.22, 1.22, 0.45]),
    mesh(sphere(0.1), black, [-0.2, 1.22, 0.62]),
    mesh(sphere(0.1), black, [0.2, 1.22, 0.62]),
    mesh(sphere(0.035), white, [-0.17, 1.27, 0.71]),
    mesh(sphere(0.035), white, [0.23, 1.27, 0.71]),
    mesh(rbox(0.1, 0.15, 0.05, 0.02), white, [-0.06, 0.66, 0.68]),
    mesh(rbox(0.1, 0.15, 0.05, 0.02), white, [0.06, 0.66, 0.68]),
    mesh(sphere(0.12), mat('#ff8aa0'), [-0.38, 0.92, 0.42], [0, 0, 0], [1, 0.6, 0.4]),
    mesh(sphere(0.12), mat('#ff8aa0'), [0.38, 0.92, 0.42], [0, 0, 0], [1, 0.6, 0.4]),
    mesh(sphere(0.16), light, [-0.5, 0.3, 0.42], [0, 0, 0], [1, 0.6, 0.8]),
    mesh(sphere(0.16), light, [0.5, 0.3, 0.42], [0, 0, 0], [1, 0.6, 0.8]),
  ], [0, 0, 0], [0, 0, 0], s)
}

function grassTuft(pos, s = 1) {
  const m = mat('#4fd65a', { roughness: 0.5 })
  return group([-0.1, 0, 0.1].map((x, i) => mesh(cone(0.07, 0.4 - i * 0.05, 8), m, [x, 0.18, 0], [0, 0, x * 2.5])), pos, [0, 0, 0], s)
}

// ---------- games ----------

const GAMES = {
  'tic-tac-toe': () => {
    // Varnished wooden tray with a raised frame, engraved grid and glossy plastic pieces.
    const grain = woodTexture({ base: '#c4702a', dark: '#6e3410', light: '#dc8c42', seed: 7 })
    // Satin varnish: enough sheen to read as finished wood without mirroring the studio lights.
    const varnish = (color, map) => mat(color, { map, roughness: 0.55, clearcoat: 0.25, clearcoatRoughness: 0.45 })
    const frameWood = varnish('#ffffff', woodTexture({ base: '#9e5520', dark: '#7e4014', light: '#b0642a', seed: 3 }))
    const tray = mesh(rbox(3.3, 0.3, 3.3, 0.14), frameWood, [0, 0, 0])
    const field = mesh(new THREE.PlaneGeometry(2.86, 2.86), varnish('#ffffff', grain), [0, 0.151, 0], [-Math.PI / 2, 0, 0])
    const frame = [
      [0, 1.53, 3.3, 0.24], [0, -1.53, 3.3, 0.24],
      [1.53, 0, 0.24, 3.3], [-1.53, 0, 0.24, 3.3],
    ].map(([x, z, w, d]) => mesh(rbox(w, 0.18, d, 0.08), frameWood, [x, 0.2, z]))
    const groove = mat('#5a2e10', { roughness: 0.8, clearcoat: 0 })
    const lines = [-0.48, 0.48].flatMap((o) => [
      mesh(rbox(2.78, 0.02, 0.07, 0.01), groove, [0, 0.152, o]),
      mesh(rbox(0.07, 0.02, 2.78, 0.01), groove, [o, 0.152, 0]),
    ])
    const blue = mat('#0d3dff', { roughness: 0.2, clearcoat: 1, clearcoatRoughness: 0.08 })
    const red = mat('#e8001f', { roughness: 0.2, clearcoat: 1, clearcoatRoughness: 0.08 })
    const X = () => group([
      mesh(capsule(0.1, 0.62), blue, [0, 0, 0], [0, Math.PI / 4, Math.PI / 2]),
      mesh(capsule(0.1, 0.62), blue, [0, 0, 0], [0, -Math.PI / 4, Math.PI / 2]),
    ])
    const O = () => mesh(torus(0.27, 0.105), red, [0, 0, 0], [Math.PI / 2, 0, 0])
    const layout = ['X', 'O', 'O', 'O', 'X', 'O', 'X', 'X', 'O']
    const pieces = layout.map((p, i) => {
      const piece = p === 'X' ? X() : O()
      piece.position.set(((i % 3) - 1) * 0.96, 0.26, (Math.floor(i / 3) - 1) * 0.96)
      return piece
    })
    return { object: group([tray, field, ...frame, ...lines, ...pieces]), view: { pitch: 0.78, yaw: -0.38, fill: 0.98 } }
  },

  checkers: () => {
    const tex = canvasTexture(512, 512, (ctx, W) => {
      const n = 5
      for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) {
        ctx.fillStyle = (r + c) % 2 ? '#2a1a10' : '#e8c48c'
        ctx.fillRect((c * W) / n, (r * W) / n, W / n, W / n)
      }
    })
    const board = mesh(rbox(3.3, 0.28, 3.3, 0.1), wood('#3b2414'))
    const top = mesh(new THREE.PlaneGeometry(3.05, 3.05), mat('#ffffff', { map: tex, roughness: 0.45 }), [0, 0.145, 0], [-Math.PI / 2, 0, 0])
    const sq = 3.05 / 5
    const piece = (c, r, color, h = 0) =>
      group([
        mesh(cyl(0.25, 0.26, 0.14), mat(color, { roughness: 0.25 })),
        mesh(torus(0.17, 0.025, 40), mat(color, { roughness: 0.25 }), [0, 0.075, 0], [Math.PI / 2, 0, 0]),
      ], [(c - 2) * sq, 0.22 + h * 0.15, (r - 2) * sq])
    const red = '#e0322f'
    const blk = '#1c1c24'
    return {
      object: group([board, top, piece(1, 0, blk), piece(3, 0, blk), piece(0, 1, blk), piece(2, 1, blk), piece(4, 1, blk),
        piece(1, 4, red), piece(3, 4, red), piece(0, 3, red), piece(4, 3, red), piece(2, 3, red), piece(2, 3, red, 1), piece(3, 2, blk)]),
      view: { pitch: 0.72, yaw: -0.5, fill: 1.2 },
    }
  },

  'ring-toss': () => {
    const w = wood('#b8743a')
    return {
      object: group([
        mesh(cyl(1.15, 1.25, 0.28), wood('#8a5228'), [0, 0.14, 0]),
        mesh(cyl(0.16, 0.18, 2.3), w, [0, 1.4, 0]),
        mesh(sphere(0.16), w, [0, 2.55, 0]),
        mesh(torus(0.62, 0.17), mat(C.blue), [0, 0.46, 0], [Math.PI / 2, 0, 0]),
        mesh(torus(0.62, 0.17), mat(C.red), [0.05, 0.85, 0], [Math.PI / 2 - 0.25, 0, 0.1]),
        mesh(torus(0.58, 0.17), mat(C.yellow), [0.55, 1.45, 0.2], [Math.PI / 2 - 0.9, 0.3, 0.4]),
      ]),
      view: { pitch: 0.35, yaw: -0.4, fill: 1.05 },
    }
  },

  'whack-a-mole': () => {
    const grass = mat('#3fc048', { roughness: 0.55, clearcoat: 0.2 })
    const dirt = mat('#6b3f1f', { roughness: 0.8, clearcoat: 0 })
    const hole = matte('#160b05')
    const holes = [[0, 0.2], [-1.15, -0.55], [1.15, -0.5], [-0.9, 0.95], [1.0, 0.95]]
    const hammer = group([
      mesh(cyl(0.42, 0.42, 1.05), mat('#e3262e', { roughness: 0.25 }), [0, 0, 0], [0, 0, Math.PI / 2]),
      mesh(cyl(0.44, 0.44, 0.12), mat('#f0b23a'), [-0.55, 0, 0], [0, 0, Math.PI / 2]),
      mesh(cyl(0.44, 0.44, 0.12), mat('#f0b23a'), [0.55, 0, 0], [0, 0, Math.PI / 2]),
      mesh(cyl(0.09, 0.11, 1.9), wood('#c98a4b'), [0.2, -0.95, 0], [0, 0, -0.2]),
    ], [1.05, 2.05, -0.2], [0.2, 0.3, 0.8], 0.85)
    return {
      object: group([
        mesh(cyl(2.0, 2.1, 0.5), dirt, [0, -0.1, 0]),
        mesh(cyl(2.0, 2.0, 0.16), grass, [0, 0.2, 0]),
        ...holes.map(([x, z]) => mesh(cyl(0.48, 0.48, 0.02), hole, [x, 0.29, z], [0, 0, 0], [1, 1, 0.7])),
        group([mole()], [0, 0.05, 0.25], [0, 0, 0], 1.25),
        grassTuft([-1.6, 0.25, 0.3]), grassTuft([1.6, 0.25, 0.4], 1.2), grassTuft([0.5, 0.25, 1.5]), grassTuft([-0.4, 0.25, -1.2], 0.9),
        hammer,
      ]),
      view: { pitch: 0.4, yaw: -0.2, fill: 1.15 },
    }
  },

  darts: () => {
    const tex = canvasTexture(1024, 1024, (ctx, W) => {
      const c = W / 2
      const ring = (r, colors, inner) => {
        for (let i = 0; i < 20; i++) {
          const a0 = ((i - 0.5) / 20) * Math.PI * 2
          const a1 = ((i + 0.5) / 20) * Math.PI * 2
          ctx.beginPath()
          ctx.arc(c, c, r, a0, a1)
          ctx.arc(c, c, inner, a1, a0, true)
          ctx.closePath()
          ctx.fillStyle = colors[i % 2]
          ctx.fill()
        }
      }
      ctx.fillStyle = '#151515'
      ctx.fillRect(0, 0, W, W)
      ring(c * 0.86, ['#d9262e', '#1f9b4a'], c * 0.8)
      ring(c * 0.8, ['#141414', '#f2e6c8'], c * 0.52)
      ring(c * 0.52, ['#d9262e', '#1f9b4a'], c * 0.46)
      ring(c * 0.46, ['#141414', '#f2e6c8'], c * 0.1)
      ctx.beginPath()
      ctx.arc(c, c, c * 0.1, 0, Math.PI * 2)
      ctx.fillStyle = '#1f9b4a'
      ctx.fill()
      ctx.beginPath()
      ctx.arc(c, c, c * 0.045, 0, Math.PI * 2)
      ctx.fillStyle = '#d9262e'
      ctx.fill()
      ctx.strokeStyle = '#c9ccd6'
      ctx.lineWidth = 3
      ;[0.86, 0.8, 0.52, 0.46].forEach((f) => {
        ctx.beginPath()
        ctx.arc(c, c, c * f, 0, Math.PI * 2)
        ctx.stroke()
      })
    })
    const board = mesh(cyl(1.5, 1.5, 0.32, 96), [matte('#111'), mat('#fff', { map: tex, roughness: 0.6, clearcoat: 0 }), matte('#111')], [0, 0, 0], [Math.PI / 2, 0, 0])
    const rim = mesh(torus(1.52, 0.08, 96), mat('#2b2b33'), [0, 0, 0.1])
    const dart = group([
      mesh(cone(0.04, 0.3), metal(), [0, -0.15, 0], [Math.PI, 0, 0]),
      mesh(cyl(0.08, 0.06, 0.55), metal('#8e97ad'), [0, 0.25, 0]),
      mesh(cyl(0.035, 0.035, 0.7), mat('#1d1d26'), [0, 0.85, 0]),
      mesh(new THREE.PlaneGeometry(0.42, 0.5), mat(C.red, { side: THREE.DoubleSide }), [0, 1.2, 0]),
      mesh(new THREE.PlaneGeometry(0.42, 0.5), mat(C.red, { side: THREE.DoubleSide }), [0, 1.2, 0], [0, Math.PI / 2, 0]),
    ], [0.08, 0.06, 0.18], [1.05, 0, -0.55], 1.5)
    return { object: group([board, rim, dart]), view: { pitch: 0.12, yaw: -0.45, fill: 1.1 }, shadow: false }
  },

  'mini-golf': () => {
    const flag = new THREE.Shape()
    flag.moveTo(0, 0)
    flag.lineTo(0.7, -0.22)
    flag.lineTo(0, -0.44)
    flag.closePath()
    return {
      object: group([
        mesh(rbox(3.6, 0.45, 2.8, 0.18), mat('#7a4a22', { roughness: 0.8, clearcoat: 0 }), [0, -0.15, 0]),
        mesh(rbox(3.5, 0.16, 2.7, 0.08), mat('#3fc048', { roughness: 0.6, clearcoat: 0.2 }), [0, 0.14, 0]),
        mesh(rbox(2.6, 0.04, 1.8, 0.02), mat('#5ee06a', { roughness: 0.5 }), [0.1, 0.23, 0]),
        mesh(cyl(0.2, 0.2, 0.02), matte('#0c0c0c'), [-0.6, 0.255, -0.2]),
        mesh(cyl(0.025, 0.025, 1.8), mat('#f4f4f8'), [-0.6, 1.15, -0.2]),
        mesh(new THREE.ShapeGeometry(flag), mat('#ff2a3a', { side: THREE.DoubleSide }), [-0.58, 2.02, -0.2]),
        mesh(sphere(0.16), mat('#ffffff', { roughness: 0.2 }), [0.55, 0.42, 0.45]),
        grassTuft([1.5, 0.2, -1.1], 0.9), grassTuft([-1.5, 0.2, 1.0]),
      ]),
      view: { pitch: 0.55, yaw: -0.3, fill: 1.18 },
    }
  },

  basketball: () => {
    const net = new THREE.LatheGeometry([[0.78, 0], [0.62, -0.45], [0.5, -0.85]].map(([x, y]) => new THREE.Vector2(x, y)), 16, 0, Math.PI * 2)
    return {
      object: group([
        mesh(rbox(2.6, 1.7, 0.1, 0.06), mat('#ffffff', { roughness: 0.3 }), [0.3, 1.9, -1.0]),
        mesh(rbox(1.0, 0.7, 0.02, 0.02), mat('#ff3b30'), [0.3, 1.75, -0.94], [0, 0, 0], [1, 1, 1]),
        mesh(rbox(0.82, 0.54, 0.04, 0.02), mat('#ffffff'), [0.3, 1.75, -0.92]),
        mesh(torus(0.78, 0.06), mat('#ff3b30', { roughness: 0.3 }), [0.3, 1.3, -0.25], [Math.PI / 2, 0, 0]),
        mesh(net, new THREE.MeshBasicMaterial({ color: '#ffffff', wireframe: true }), [0.3, 1.3, -0.25]),
        basketball(0.95),
      ]),
      view: { pitch: 0.2, yaw: -0.35, fill: 1.0 },
    }
  },

  'bubble-pop': () => {
    const cols = [C.pink, C.yellow, C.cyan, C.blue, C.green, C.orange, C.purple, '#ff4d6d', '#2fe0c8']
    const pos = [[0, 0, 0], [1.05, 0, 0], [-1.05, 0, 0], [0.52, 0.9, 0], [-0.52, 0.9, 0], [0.52, -0.9, 0], [-0.52, -0.9, 0], [1.57, 0.9, -0.2], [-1.57, -0.9, -0.2]]
    return {
      object: group(pos.map((p, i) => mesh(sphere(0.55), mat(cols[i], { roughness: 0.08, clearcoat: 1, clearcoatRoughness: 0.05, sheen: 0.5 }), p))),
      view: { pitch: 0.15, yaw: 0, fill: 1.05 },
      shadow: false,
    }
  },

  'block-blast': () => {
    const cube = (x, z, color, y = 0) => mesh(rbox(0.92, 0.92, 0.92, 0.14), mat(color, { roughness: 0.25 }), [x, 0.5 + y, z])
    return {
      object: group([
        mesh(rbox(4.4, 0.25, 4.4, 0.12), mat('#1d2a7a', { roughness: 0.5 }), [0, -0.1, 0]),
        cube(-1.5, -1.5, C.orange), cube(-0.5, -1.5, C.orange), cube(0.5, -1.5, C.orange), cube(0.5, -0.5, C.orange),
        cube(-1.5, -0.5, C.red), cube(-1.5, 0.5, C.red), cube(-0.5, 0.5, C.red),
        cube(-1.5, 1.5, C.green), cube(-0.5, 1.5, C.green), cube(0.5, 1.5, C.blue), cube(1.5, 1.5, C.blue), cube(1.5, 0.5, C.blue),
        cube(1.5, -1.5, C.yellow, 0.7),
      ]),
      view: { pitch: 0.7, yaw: -0.45, fill: 1.15 },
    }
  },

  solitaire: () => ({
    object: group([
      group([card(rankFace('A', 'spade'))], [-0.85, 0.1, 0], [0, 0, 0.3]),
      group([card(rankFace('K', 'diamond'))], [0, 0.25, 0.05], [0, 0, 0]),
      group([card(rankFace('Q', 'heart'))], [0.85, 0.1, 0.1], [0, 0, -0.3]),
    ], [0, 0, 0], [-0.25, 0, 0]),
    view: { pitch: 0.1, yaw: 0, fill: 1.12 },
    shadow: false,
  }),

  'memory-match': () => {
    const starFace = (ctx, W, H) => {
      ctx.fillStyle = '#fff6e2'
      ctx.fillRect(0, 0, W, H)
      ctx.save()
      ctx.translate(W / 2, H / 2)
      ctx.beginPath()
      for (let i = 0; i < 10; i++) {
        const r = i % 2 ? 45 : 105
        const a = (i / 10) * Math.PI * 2 - Math.PI / 2
        ctx.lineTo(Math.cos(a) * r, Math.sin(a) * r)
      }
      ctx.closePath()
      const g = ctx.createLinearGradient(0, -100, 0, 100)
      g.addColorStop(0, '#ffe066')
      g.addColorStop(1, '#ffad0a')
      ctx.fillStyle = g
      ctx.fill()
      ctx.lineWidth = 10
      ctx.strokeStyle = '#e58a00'
      ctx.stroke()
      ctx.restore()
    }
    return {
      object: group([
        group([card(null, { back: true, w: 1.3, h: 1.8 })], [0, 0.35, -0.6], [0, 0, 0]),
        group([card(starFace, { w: 1.3, h: 1.8 })], [-0.72, 0, 0], [0, 0.25, 0.12]),
        group([card(starFace, { w: 1.3, h: 1.8 })], [0.72, 0, 0.1], [0, -0.25, -0.12]),
      ], [0, 0, 0], [-0.35, 0, 0]),
      view: { pitch: 0.15, yaw: 0, fill: 1.05 },
      shadow: false,
    }
  },

  'maze-runner': () => {
    const grid = [
      '#######',
      '#.....#',
      '#.###.#',
      '#.#...#',
      '#.#.###',
      '#...#.#',
      '###.#.#',
    ]
    const wall = mat('#2f6bff', { roughness: 0.3 })
    const walls = []
    grid.forEach((row, r) => [...row].forEach((ch, c) => {
      if (ch === '#') walls.push(mesh(rbox(0.5, 0.42, 0.5, 0.08), wall, [(c - 3) * 0.5, 0.3, (r - 3) * 0.5]))
    }))
    return {
      object: group([
        mesh(rbox(3.8, 0.2, 3.8, 0.12), mat('#1a3fb8', { roughness: 0.5 }), [0, 0, 0]),
        ...walls,
        mesh(sphere(0.18), mat('#ffffff', { roughness: 0.15 }), [0, 0.28, 0]),
      ]),
      view: { pitch: 0.85, yaw: -0.4, fill: 1.22 },
    }
  },

  'air-hockey': () => ({
    object: group([
      mesh(rbox(3.6, 0.25, 2.5, 0.14), mat('#f4f6ff', { roughness: 0.2 }), [0, 0, 0]),
      mesh(rbox(3.7, 0.18, 0.12, 0.05), mat('#2a6bff'), [0, 0.18, 1.25]),
      mesh(rbox(3.7, 0.18, 0.12, 0.05), mat('#2a6bff'), [0, 0.18, -1.25]),
      mesh(torus(0.6, 0.025), mat('#ff2f45'), [0, 0.13, 0], [Math.PI / 2, 0, 0]),
      mesh(rbox(0.04, 0.01, 2.4, 0.004), mat('#ff2f45'), [0, 0.13, 0]),
      group([
        mesh(cyl(0.48, 0.5, 0.18), mat('#e3262e', { roughness: 0.2 }), [0, 0.09, 0]),
        mesh(cyl(0.2, 0.24, 0.35), mat('#e3262e', { roughness: 0.2 }), [0, 0.35, 0]),
        mesh(sphere(0.24), mat('#ff3b47', { roughness: 0.15 }), [0, 0.58, 0]),
      ], [-0.85, 0.12, 0.15]),
      mesh(cyl(0.33, 0.33, 0.1), mat('#15161f', { roughness: 0.2 }), [0.7, 0.18, -0.15]),
    ]),
    view: { pitch: 0.55, yaw: -0.3, fill: 1.25 },
  }),

  'puzzle-jigsaw': () => ({
    object: group([
      group([puzzlePiece('#3fc04f', [1, -1, 0, 0])], [-0.5, 0.1, -0.5]),
      group([puzzlePiece('#2a8cff', [-1, 0, 0, 1])], [0.5, 0.1, -0.5]),
      group([puzzlePiece('#ffb31f', [0, 1, -1, 0])], [-0.5, 0.1, 0.5]),
      group([puzzlePiece('#ff4d6d', [0, 0, 1, -1])], [0.85, 0.45, 0.75], [0.25, 0.4, -0.3]),
    ], [0, 0, 0], [0, 0, 0], 1.3),
    view: { pitch: 0.75, yaw: -0.3, fill: 1.18 },
  }),

  'pocket-racer': () => {
    const red = mat('#e8232f', { roughness: 0.15, clearcoat: 1 })
    const glass = mat('#1a2a4a', { roughness: 0.05, metalness: 0.3, clearcoat: 1 })
    const wheel = (x, z) =>
      group([
        mesh(cyl(0.34, 0.34, 0.3), mat('#16161c', { roughness: 0.7, clearcoat: 0 }), [0, 0, 0], [Math.PI / 2, 0, 0]),
        mesh(cyl(0.17, 0.17, 0.32), metal('#c9d0de'), [0, 0, 0], [Math.PI / 2, 0, 0]),
      ], [x, 0.34, z])
    return {
      object: group([
        mesh(rbox(2.7, 0.45, 1.25, 0.2), red, [0, 0.55, 0]),
        mesh(rbox(1.2, 0.42, 1.0, 0.2), red, [-0.15, 0.95, 0]),
        mesh(rbox(1.0, 0.3, 1.04, 0.12), glass, [-0.15, 0.98, 0]),
        mesh(rbox(0.12, 0.3, 0.12, 0.04), mat('#16161c'), [-1.2, 0.95, 0.4]),
        mesh(rbox(0.12, 0.3, 0.12, 0.04), mat('#16161c'), [-1.2, 0.95, -0.4]),
        mesh(rbox(0.4, 0.08, 1.4, 0.04), red, [-1.25, 1.12, 0]),
        mesh(sphere(0.1), mat('#fff6b0', { emissive: '#ffe066', emissiveIntensity: 0.6 }), [1.33, 0.6, 0.38]),
        mesh(sphere(0.1), mat('#fff6b0', { emissive: '#ffe066', emissiveIntensity: 0.6 }), [1.33, 0.6, -0.38]),
        mesh(rbox(0.6, 0.06, 1.3, 0.03), mat('#ffffff'), [0.75, 0.79, 0]),
        wheel(0.85, 0.62), wheel(0.85, -0.62), wheel(-0.85, 0.62), wheel(-0.85, -0.62),
      ]),
      view: { pitch: 0.38, yaw: 0.65, fill: 1.0 },
    }
  },

  2048: () => {
    const tile = (x, z, n, color, ink) =>
      group([
        mesh(rbox(1.25, 0.42, 1.25, 0.16), mat(color, { roughness: 0.3 }), [0, 0.21, 0]),
        decal(1.1, (ctx, W) => text(ctx, n, W / 2, W / 2 + 6, { size: n.length > 1 ? 120 : 150, color: ink }), [0, 0.425, 0]),
      ], [x, 0, z])
    return {
      object: group([
        mesh(rbox(2.95, 0.22, 2.95, 0.14), mat('#b88a5a', { roughness: 0.5 }), [0, -0.05, 0]),
        tile(-0.68, -0.68, '2', '#f6e6c9', '#6b4a2a'), tile(0.68, -0.68, '4', '#f3d79e', '#6b4a2a'),
        tile(-0.68, 0.68, '8', '#f7a24a', '#ffffff'), tile(0.68, 0.68, '16', '#f5743a', '#ffffff'),
      ]),
      view: { pitch: 0.85, yaw: -0.3, fill: 1.25 },
    }
  },

  'flappy-bird': () => {
    const yellow = mat('#ffd21f', { roughness: 0.3 })
    const white = mat('#ffffff', { roughness: 0.2 })
    const pipe = mat('#3ccf4e', { roughness: 0.3 })
    const bird = group([
      mesh(sphere(0.9), yellow, [0, 0, 0], [0, 0, 0], [1.05, 0.95, 0.95]),
      mesh(sphere(0.62), mat('#fff3b0', { roughness: 0.4 }), [0.25, -0.35, 0], [0, 0, 0], [1, 0.8, 0.9]),
      mesh(sphere(0.32), white, [0.55, 0.35, 0.42]),
      mesh(sphere(0.15), mat('#111'), [0.72, 0.38, 0.55]),
      mesh(sphere(0.05), white, [0.78, 0.45, 0.64]),
      mesh(cone(0.26, 0.6), mat('#ff8a1f'), [1.05, 0.02, 0], [0, 0, -Math.PI / 2]),
      mesh(sphere(0.5), mat('#ffb21f'), [-0.25, -0.05, 0.75], [0.2, 0, 0.4], [1, 0.5, 0.25]),
      mesh(sphere(0.18), mat('#ff3b30'), [0, 0.95, 0]),
      mesh(sphere(0.16), mat('#ff3b30'), [-0.25, 0.9, 0]),
      mesh(sphere(0.14), mat('#ff3b30'), [0.22, 0.88, 0]),
    ], [-0.35, 0.25, 0.3], [0, -0.3, 0.12], 1.45)
    return {
      object: group([
        bird,
        mesh(cyl(0.5, 0.5, 1.8), pipe, [1.55, -0.8, -1.2]),
        mesh(cyl(0.62, 0.62, 0.4), pipe, [1.55, 0.15, -1.2]),
      ]),
      view: { pitch: 0.12, yaw: 0.35, fill: 1.0 },
      shadow: false,
    }
  },

  snake: () => {
    const curve = new THREE.CatmullRomCurve3([
      [-1.4, 0.2, 0.9], [-0.6, 0.2, 1.2], [0.1, 0.2, 0.6], [-0.4, 0.2, -0.1], [0.3, 0.2, -0.6], [1.0, 0.45, -0.3],
    ].map((p) => new THREE.Vector3(...p)))
    const green = mat('#33c94f', { roughness: 0.3 })
    const body = mesh(new THREE.TubeGeometry(curve, 80, 0.22, 20, false), green)
    const tail = mesh(sphere(0.22), green, [-1.4, 0.2, 0.9])
    const head = group([
      mesh(sphere(0.36), green, [0, 0, 0], [0, 0, 0], [1.2, 0.9, 1]),
      mesh(sphere(0.12), mat('#ffffff'), [0.15, 0.2, 0.18]),
      mesh(sphere(0.12), mat('#ffffff'), [0.15, 0.2, -0.18]),
      mesh(sphere(0.06), mat('#111'), [0.24, 0.22, 0.2]),
      mesh(sphere(0.06), mat('#111'), [0.24, 0.22, -0.2]),
      mesh(rbox(0.3, 0.03, 0.06, 0.01), mat('#ff3060'), [0.5, -0.05, 0]),
    ], [1.15, 0.5, -0.28], [0, 0.4, 0.2])
    const apple = group([
      mesh(sphere(0.38), mat('#e3262e', { roughness: 0.2 }), [0, 0, 0], [0, 0, 0], [1, 0.92, 1]),
      mesh(cyl(0.03, 0.03, 0.22), wood('#6b3f1f'), [0, 0.42, 0], [0, 0, 0.2]),
      mesh(sphere(0.12), mat('#3fc048'), [0.13, 0.46, 0], [0, 0, 0.6], [1.4, 0.5, 0.8]),
    ], [1.4, 0.38, 0.9])
    return { object: group([body, tail, head, apple]), view: { pitch: 0.55, yaw: -0.2, fill: 1.08 } }
  },

  pinball: () => {
    const flipShape = new THREE.Shape()
    flipShape.absarc(0, 0, 0.2, Math.PI / 2, (Math.PI * 3) / 2, false)
    flipShape.lineTo(1.1, -0.07)
    flipShape.absarc(1.1, 0, 0.07, -Math.PI / 2, Math.PI / 2, false)
    flipShape.lineTo(0, 0.2)
    const flipper = (x, flip) =>
      mesh(extrude(flipShape, 0.18, 0.04), mat('#ffffff', { roughness: 0.2 }), [x, 0.12, 1.1], [-Math.PI / 2, 0, flip ? Math.PI + 0.35 : -0.35])
    const bumper = (x, z, color) =>
      group([
        mesh(cyl(0.32, 0.36, 0.3), mat('#ffffff', { roughness: 0.2 }), [0, 0.2, 0]),
        mesh(cyl(0.3, 0.3, 0.08), mat(color, { emissive: color, emissiveIntensity: 0.9 }), [0, 0.38, 0]),
        mesh(torus(0.36, 0.05), mat(color, { emissive: color, emissiveIntensity: 1.2 }), [0, 0.1, 0], [Math.PI / 2, 0, 0]),
      ], [x, 0, z])
    return {
      object: group([
        mesh(rbox(3.2, 0.14, 3.6, 0.1), mat('#141a5a', { roughness: 0.4 }), [0, 0, 0]),
        flipper(-1.05, false), flipper(1.05, true),
        bumper(-0.8, -0.7, '#ff3df2'), bumper(0.8, -0.7, '#22c8ff'), bumper(0, -1.3, '#ffc51f'),
        mesh(sphere(0.32), metal('#e6ecf5'), [0.15, 0.4, 0.15]),
      ]),
      view: { pitch: 0.62, yaw: 0, fill: 1.15 },
    }
  },

  'word-search': () => {
    const rows = ['FREZ', 'GAME', 'PLAY']
    const tiles = []
    rows.forEach((row, r) => [...row].forEach((ch, c) => {
      const hot = r === 1
      tiles.push(group([
        mesh(rbox(0.82, 0.24, 0.82, 0.1), mat(hot ? '#39d353' : '#f6efe0', { roughness: 0.35 }), [0, 0.12, 0]),
        decal(0.72, (ctx, W) => text(ctx, ch, W / 2, W / 2 + 8, { size: 170, color: hot ? '#ffffff' : '#1d2340' }), [0, 0.245, 0]),
      ], [(c - 1.5) * 0.92, 0, (r - 1) * 0.92]))
    }))
    return {
      object: group([mesh(rbox(3.95, 0.16, 3.0, 0.1), mat('#2c3aa8', { roughness: 0.5 }), [0, -0.06, 0]), ...tiles], [0, 0, 0], [0, 0, 0]),
      view: { pitch: 0.95, yaw: -0.25, fill: 1.22 },
    }
  },
}

// ---------- icons & decorations ----------

const PROPS = {
  star: () => ({ object: star3d(), view: { pitch: 0.15, yaw: -0.3, fill: 1.05 }, shadow: false }),
  gamepad: () => ({ object: gamepad(), view: { pitch: 0.6, yaw: -0.2, fill: 1.05 }, shadow: false }),
  joystick: () => ({ object: joystick(), view: { pitch: 0.35, yaw: -0.4, fill: 1.05 }, shadow: false }),
  'puzzle-piece': () => ({ object: puzzlePiece(C.purple, [1, 1, -1, -1], 1.6), view: { pitch: 0.9, yaw: -0.3, fill: 1.0 }, shadow: false }),
  ball: () => ({ object: basketball(), view: { pitch: 0.2, yaw: 0, fill: 1.08 }, shadow: false }),
  cards: () => ({
    object: group([
      group([card(rankFace('A', 'spade'))], [-0.4, 0, 0], [0, 0, 0.2]),
      group([card(rankFace('A', 'heart'))], [0.4, -0.05, 0.05], [0, 0, -0.18]),
    ], [0, 0, 0], [-0.2, 0.25, 0]),
    view: { pitch: 0.1, yaw: 0, fill: 1.05 },
    shadow: false,
  }),
  dice: () => ({ object: die(), view: { pitch: 0.55, yaw: -0.65, fill: 1.12 }, shadow: false }),
  trophy: () => ({ object: trophy(), view: { pitch: 0.15, yaw: -0.2, fill: 1.05 }, shadow: false }),
  target: () => ({ object: target(), view: { pitch: 0.2, yaw: -0.2, fill: 1.0 }, shadow: false }),
  'gem-purple': () => ({ object: gem(C.purple), view: { pitch: 0.2, yaw: 0, fill: 1.3 }, shadow: false }),
  'gem-green': () => ({ object: gem('#2fe06a'), view: { pitch: 0.2, yaw: 0, fill: 1.3 }, shadow: false }),
  'gem-blue': () => ({ object: gem('#2f8bff'), view: { pitch: 0.2, yaw: 0, fill: 1.3 }, shadow: false }),
  rings: () => ({
    object: group([
      mesh(cyl(0.12, 0.13, 1.8), wood('#b8743a'), [0, 0.9, 0]),
      mesh(torus(0.55, 0.15), mat(C.blue), [0, 0.3, 0], [Math.PI / 2, 0, 0]),
      mesh(torus(0.55, 0.15), mat(C.red), [0, 0.62, 0], [Math.PI / 2 - 0.2, 0, 0.1]),
      mesh(torus(0.52, 0.15), mat(C.yellow), [0.45, 1.1, 0.15], [Math.PI / 2 - 0.8, 0.3, 0.4]),
    ]),
    view: { pitch: 0.35, yaw: -0.4, fill: 1.05 },
    shadow: false,
  }),
  mole: () => ({
    object: group([mesh(cyl(1.0, 1.1, 0.3), mat('#3fc048', { roughness: 0.5 }), [0, 0, 0]), mesh(cyl(0.6, 0.6, 0.02), matte('#160b05'), [0, 0.16, 0]), group([mole()], [0, 0.1, 0], [0, 0, 0], 0.9), grassTuft([-0.8, 0.15, 0.3]), grassTuft([0.8, 0.15, 0.2])]),
    view: { pitch: 0.3, yaw: -0.2, fill: 1.05 },
    shadow: false,
  }),
}

export const MODELS = { ...GAMES, ...PROPS }
