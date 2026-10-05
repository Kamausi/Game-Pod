// 3D model builders for every game tile, category icon and header decoration.
// Each returns { object, view } where view = { pitch, yaw, fill } positions the camera.
import {
  THREE, mat, wood, metal, matte, mesh, group, rbox, cyl, sphere, torus, cone, capsule,
  canvasTexture, decal, text, starShape, extrude, drawSuit, roundRect, woodTexture, toyXGeometry, toyOGeometry, toyPuckGeometry, vary, noiseTexture, trayFrameGeometry, mouldedXGeometry,
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

// Expressive mole for the Whack a Mole tile: big head, cream muzzle, open grin with buck teeth,
// huge eyes, raised brows and hair tufts, front paws resting on the hole's rim.
function heroMole() {
  const fur = mat('#a8561e', { roughness: 0.5, clearcoat: 0.3, clearcoatRoughness: 0.35, sheen: 0.4, sheenColor: '#ffcc99' })
  const cream = mat('#e7b07a', { roughness: 0.55, clearcoat: 0.2 })
  const white = mat('#ffffff', { roughness: 0.15, clearcoat: 0.8 })
  const ink = mat('#140a05', { roughness: 0.2, clearcoat: 1 })
  const iris = mat('#5a3214', { roughness: 0.2, clearcoat: 1 })
  const mouth = mat('#3a0e0a', { roughness: 0.6 })
  const tongue = mat('#ff6a7a', { roughness: 0.35 })
  const eye = (x) => group([
    mesh(sphere(0.26), white, [0, 0, 0], [0, 0, 0], [1, 1.15, 0.7]),
    mesh(sphere(0.15), iris, [0.02 * Math.sign(x), -0.02, 0.13], [0, 0, 0], [1, 1.1, 0.6]),
    mesh(sphere(0.09), ink, [0.02 * Math.sign(x), -0.02, 0.19], [0, 0, 0], [1, 1.1, 0.5]),
    mesh(sphere(0.045), white, [0.07, 0.07, 0.23]),
  ], [x, 1.6, 0.6], [0, 0, 0], 1.25)
  const brow = (x) => mesh(capsule(0.045, 0.2), mat('#4a220c'), [x, 1.95, 0.72], [0, 0, x > 0 ? 1.2 : -1.2])
  return group([
    mesh(sphere(0.86), fur, [0, 1.05, 0], [0, 0, 0], [1, 1.12, 0.92]), // head + body
    mesh(sphere(0.25), fur, [-0.72, 1.62, -0.05], [0, 0, 0], [1, 1, 0.6]), // ears
    mesh(sphere(0.25), fur, [0.72, 1.62, -0.05], [0, 0, 0], [1, 1, 0.6]),
    mesh(sphere(0.13), cream, [-0.74, 1.62, 0.06], [0, 0, 0], [1, 1, 0.4]),
    mesh(sphere(0.13), cream, [0.74, 1.62, 0.06], [0, 0, 0], [1, 1, 0.4]),
    mesh(sphere(0.42), cream, [0.05, 1.12, 0.62], [0, 0, 0], [1.25, 0.8, 0.7]), // muzzle
    // Wide open grin: a dark half-ellipse with a pink tongue, buck teeth hanging from the top lip.
    mesh(new THREE.SphereGeometry(0.34, 40, 20, 0, Math.PI * 2, Math.PI / 2, Math.PI / 2), mouth, [0.05, 1.02, 0.86], [-0.25, 0, 0], [1.25, 1.0, 0.45]),
    mesh(sphere(0.17), tongue, [0.05, 0.8, 0.95], [0, 0, 0], [1.4, 0.5, 0.5]),
    mesh(rbox(0.11, 0.13, 0.05, 0.03), white, [-0.01, 0.96, 1.0]), // buck teeth
    mesh(rbox(0.11, 0.13, 0.05, 0.03), white, [0.11, 0.96, 1.0]),
    mesh(sphere(0.12), ink, [0.08, 1.28, 1.02], [0, 0, 0], [1.35, 0.9, 0.8]), // nose
    mesh(sphere(0.035), white, [0.03, 1.32, 1.1]),
    eye(-0.26), eye(0.34), brow(-0.28), brow(0.38),
    mesh(sphere(0.14), mat('#ff8a8a', { roughness: 0.6 }), [-0.55, 1.05, 0.62], [0, 0, 0], [1, 0.6, 0.3]), // cheeks
    mesh(sphere(0.14), mat('#ff8a8a', { roughness: 0.6 }), [0.55, 1.05, 0.62], [0, 0, 0], [1, 0.6, 0.3]),
    ...[[-0.12, 0.25], [0.05, -0.1], [0.2, -0.4]].map(([x, r]) => mesh(cone(0.07, 0.3, 12), fur, [x, 2.05, 0.1], [0, 0, r])), // hair tufts
    mesh(sphere(0.24), fur, [-0.75, 0.25, 0.55], [0, 0, 0], [1.2, 0.6, 1]), // paws on the rim
    mesh(sphere(0.24), fur, [0.75, 0.25, 0.55], [0, 0, 0], [1.2, 0.6, 1]),
  ])
}

function grassTuft(pos, s = 1) {
  const m = mat('#4fd65a', { roughness: 0.5 })
  return group([-0.1, 0, 0.1].map((x, i) => mesh(cone(0.07, 0.4 - i * 0.05, 8), m, [x, 0.18, 0], [0, 0, x * 2.5])), pos, [0, 0, 0], s)
}

// ---------- games ----------

const GAMES = {
  'tic-tac-toe': () => {
    // Reference: one solid caramel wood tray with large rounded outer corners, a glossy rim lit from
    // the upper right, thin flat dividers over black cells, crisp royal-blue X bars and thick glossy
    // red rings with a dark gap around each piece; blurred carnival-at-dusk behind.
    // Colour gains (linear RGB multipliers on the wood textures), tuned so rendered colours equal the
    // reference's sampled pixels exactly.
    const WOOD_GAIN = [1.015722, 1.458231, 1.13363]
    const RAIL_GAIN = [1.63664, 1.759611, 0.040691]
    const wood = mat(new THREE.Color().setRGB(...WOOD_GAIN), {
      map: woodTexture({ base: '#f97233', dark: '#eb6b31', light: '#ff7c3b', seed: 7, grain: 0.4 }), // honey caramel, sampled against the reference
      roughness: 0.38, clearcoat: 0.16, clearcoatRoughness: 0.42, envMapIntensity: 0.3, // catches the light without looking lacquered
    })
    // Cross bars read a little lighter than the frame in the reference.
    const railWood = mat(new THREE.Color().setRGB(...RAIL_GAIN), {
      map: woodTexture({ base: '#ff9530', dark: '#ff8b2d', light: '#ff9e35', seed: 11 }),
      roughness: 0.55, clearcoat: 0, envMapIntensity: 0.2, specularIntensity: 0.2, // a little less glossy than the frame
    })
    const black = mat('#120a10', { roughness: 1, clearcoat: 0, envMapIntensity: 0 }) // very dark cell floor: bright piece, dark cavity, bright divider
    // The opening is fixed by the reference (its four inner corners are matched exactly by the camera):
    // 2.81 wide, 2.406 deep. Nine equal cells fill it, so the cell pitch follows the divider width.
    const INNER = 2.81
    const INNER_DEPTH = 2.406
    // Painted face shading, tuned so the outer left and front faces equal the reference's sampled colours.
    const WOOD_LEFT = [0.65087, 1.546565, 4.746508]
    const WOOD_FRONT = [0.602813, 0.65738, 0.894438]
    // Top-face gradient, tuned to the reference's rim samples: lighter and yellower toward the back and
    // right, a little darker at the front.
    const TOP_BACK = [1.040722, 2.212584, 3.049769]
    const TOP_RIGHT = [1.833755, 1.702495, 0.29334]
    const TOP_FRONT = [0.699675, 0.74028, 1.219953]
    wood.userData.faceTint = {
      left: WOOD_LEFT, front: WOOD_FRONT, side: [0.82, 0.72, 0.68], outer: [INNER / 2 + 0.05, INNER_DEPTH / 2 + 0.05], // darker reddish-brown sides
      topBack: TOP_BACK, topRight: TOP_RIGHT, topFront: TOP_FRONT, extent: [INNER / 2 + 0.25, INNER_DEPTH / 2 + 0.25],
    }
    const DIV_W = 0.10 // divider width
    const S = (INNER + DIV_W) / 3 // side-to-side cell pitch (pieces and dividers sit on it)
    const SZ = (INNER_DEPTH + DIV_W) / 3 // front-to-back cell pitch
    const RIM = 0.25 // frame wall thickness
    // The right wall is a little thicker so it looks as wide on screen as the left (it is foreshortened).
    const RIM_RIGHT = 0.31
    const OUTER = INNER + RIM + RIM_RIGHT
    const OUTER_DEPTH = INNER_DEPTH + 2 * RIM
    const floorTop = 0.52 // black cell floor (deep wells); pieces rest on it
    const H = 0.88 // whole tray height: tall side walls show
    // Pieces rest on the floor; their tops sit PIECE_DROP just below the board face.
    const PIECE_DROP = 0.07
    const PIECE_HEIGHT = 0.88 - 0.6 - PIECE_DROP // fixed piece height (0.21), independent of the floor depth
    // Map the grain once across the whole frame; repeating it every unit showed up as seams on the rim.
    // Rounded corners in plan, but crisp edges: only a tight bevel where top meets sides.
    const WALL_EXTRA = 0.15 // outer walls run this much further down below the board, for a taller left side
    const frameGeo = trayFrameGeometry({ outer: OUTER, outerDepth: OUTER_DEPTH, outerCenter: [(RIM_RIGHT - RIM) / 2, 0], inner: INNER, innerDepth: INNER_DEPTH, height: H + WALL_EXTRA, outerRadius: 0.2, innerRadius: 0.06, bevel: 0.025 })
    frameGeo.translate(0, -WALL_EXTRA, 0)
    const uv = frameGeo.attributes.uv
    for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) / OUTER + 0.5, uv.getY(i) / OUTER + 0.5)
    const parts = [
      // One-piece frame: rounded outer corners (radius 0.45), rounded inner corners, no seams.
      mesh(frameGeo, wood),
      mesh(rbox(INNER + 0.1, floorTop, INNER_DEPTH + 0.1, 0.02, 2), wood, [0, floorTop / 2, 0]), // floor slab, hidden under cells
      mesh(new THREE.PlaneGeometry(INNER, INNER_DEPTH), black, [0, floorTop + 0.002, 0], [-Math.PI / 2, 0, 0]),
    ]
    // Dividers are flush with the frame top (a hair under, to avoid z-fighting where they run into the walls);
    // their ends run into the walls so no rounded stub shows.
    const DIV_DROP = 0.05 // dividers sit a little below the frame's top edge
    const DIV_H = H - floorTop - DIV_DROP
    const RAIL_GAP = -0.05 // bars run into the frame walls: flush, no gap
    for (const o of [-0.5, 0.5]) {
      parts.push(mesh(rbox(INNER - 2 * RAIL_GAP, DIV_H, DIV_W, 0.025, 4), railWood, [0, floorTop + DIV_H / 2, o * SZ]))
      parts.push(mesh(rbox(DIV_W, DIV_H, INNER_DEPTH - 2 * RAIL_GAP, 0.025, 4), railWood, [o * S, floorTop + DIV_H / 2, 0]))
    }
    // Raised square blocks where the bars cross: flush with the bars' sides (a hair inside, to avoid
    // z-fighting), standing proud of them only in height.
    const JOINT_W = DIV_W - 0.002
    const JOINT_H = DIV_H + 0.06
    for (const ox of [-0.5, 0.5]) for (const oz of [-0.5, 0.5])
      parts.push(mesh(rbox(JOINT_W, JOINT_H, JOINT_W, 0.03, 4), railWood, [ox * S, floorTop + JOINT_H / 2, oz * SZ]))
    // Crisp X: two long thin bars with flat tops and squared, slightly rounded ends.
    // X and O share one proportion: same band width (X arm = O ring), same height, same footprint
    // (the X's on-board width matches the O's outer diameter).
    const PIECE_BAND = 0.16
    const PIECE_SPAN = 0.64 // ~74% of the 0.86 cell, leaving a dark gap around each piece
    const X_BAND = 0.145 // X arm width, a touch thinner than PIECE_BAND (arm length unchanged)
    const O_BAND = X_BAND // O ring the same width as the X arms (same outer size)
    const X_ARM = 0.9 // X arms a little shorter than the full span
    const PIECE_ROUND = 0.015 // slight edge rounding on X and O
    // Slightly cyan blue; the light, not the colour, makes bright face / mid bevel / dark side.
    const BLUE = [0.0, 0.172, 0.516] // linear RGB, tuned to the reference's X tops
    const blue = mat(new THREE.Color().setRGB(...BLUE), { roughness: 0.4, clearcoat: 0, specularIntensity: 0, envMapIntensity: 0.05 }) // vivid blue: no white reflections washing it out
    const X_SIDE = [0.0, 0.070194, 2.419624] // painted shading on the X's vertical faces, tuned to the reference's deep-blue sides
    blue.userData.faceTint = { side: X_SIDE }
    // Moulded X: squared-but-rounded arm ends, small inner fillets, crowned top.
    // No rounding: square arm ends, sharp inner corners, flat top, only a hairline edge bevel.
    const xGeo = mouldedXGeometry({ size: (PIECE_SPAN / Math.SQRT2 * 2 - PIECE_BAND) * X_ARM, arm: X_BAND, endRadius: 0.006, innerRadius: 0.004, depth: PIECE_HEIGHT - 2 * PIECE_ROUND, bevelHeight: PIECE_ROUND, bevelWidth: PIECE_ROUND })
    const X = () => mesh(xGeo, blue)
    // Thick glossy ring with a small hole; outer diameter ~65% of the cell, so a dark gap shows around it.
    const red = mat('#ff4930', { roughness: 0.3, clearcoat: 0.22, clearcoatRoughness: 0.3 }) // warm red-orange
    const ringGeo = toyOGeometry({ radius: PIECE_SPAN / 2 - O_BAND / 2, width: O_BAND, height: PIECE_HEIGHT, round: PIECE_ROUND }) // flat top, slightly rounded edges
    const layout = ['X', 'X', 'O', 'O', 'O', 'O', 'X', 'X', 'O']
    layout.forEach((p, i) => {
      // Both pieces are centred on their origin and PIECE_HEIGHT tall: rest on the floor, tops flush with the board face.
      const piece = p === 'X' ? X() : mesh(ringGeo, red)
      piece.position.set(((i % 3) - 1) * S, floorTop + PIECE_HEIGHT / 2, (Math.floor(i / 3) - 1) * SZ)
      parts.push(vary(piece, i + 1, { rot: 1.5, scale: 0.005, value: 0.01, rough: 0.02 }))
    })
    return {
      // Board rotated in the world and tilted toward the camera, as in the reference.
      // Camera front-left so the left and front side faces show, as in the reference.
      object: group(parts, [0, 0, 0], [0.15, 0, -0.15]),
      // Exact camera solved so the four inner corners of the frame land exactly on the reference's
      // (82,100) (448,60) (158,432) (543,358) at 600 px.
      view: { camera: { position: [-1.72327, 8.96642, 4.44024], target: [0.20528, -0.75146, -0.12564], roll: 0.15066, fov: 25.30057 } },
      look: {
        // Key-art rig: warm key from upper left / front (soft shadows), strong cyan rim from the board's top
        // right, orange bounce from below-front-left, cool sky / warm ground fill, a broad warm softbox for
        // photographic highlights, raised exposure and restrained bloom.
        keyFrom: [-2.6, 3.6, 0.2],
        keyIntensity: 2.16, keyColor: '#ffb35c', envIntensity: 0.25, exposure: 1.12, shadowSoftness: 14,
        lights: [
          { type: 'hemi', sky: '#68bfff', ground: '#ff8b3d', intensity: 0.42 },
          // Cool cyan key from the upper right: tints the top-right wood, right side of the frame, piece tops and
          // divider edges cyan instead of drawing a white outline.
          { type: 'dir', color: '#4bbcff', intensity: 2.71, from: [2.0, 2.6, 2.0] },
          { type: 'point', color: '#ff6b24', intensity: 6, from: [-1.5, -1.0, 2.0], distance: 4 },
        ],
        softbox: { color: '#ffd18a', intensity: 3, width: 1.6, height: 1.6, from: [-1.8, 2.6, 0.4] },
        // Neutral tone mapping keeps saturated colours saturated (ACES washes bright blue toward white).
        tone: 'neutral',
        backdropUntoned: true, soften: { blur: 0.006, inner: 0.45, outer: 0.85 },
        bloom: { threshold: 0.9, strength: 0.22, radius: 0.026 },
        rim: { intensity: 0.0275, color: '#38a8ff' }, aoIntensity: 2.4, aoRadius: 0.04,
        glow: { amount: 0.22, radius: 0.03, tint: ['#ffb070', 0.08] },
      },
      backdrop: {
        // Blurred carnival / street at dusk.
        gradient: [180, '#5a8af0', '#8a8ad0', '#e0a070', '#5a3a40'],
        // Large out-of-focus light masses: pink/orange upper left, cyan upper right (cyan, not white, at the top),
        // gold/orange lower left.
        masses: [[0.5, 0.0, 0.14, '#8fdcff'], [0.12, 0.1, 0.2, '#ff6aa8'], [0.86, 0.1, 0.22, '#30b8ff'], [0.14, 0.72, 0.24, '#ffa030'], [0.06, 0.55, 0.22, '#ffb050'], [0.12, 0.85, 0.25, '#f0a050'], [0.15, 0.25, 0.12, '#ffd090'], [0.88, 0.3, 0.2, '#5a7ae0'], [0.6, 1.0, 0.3, '#4a2a30']],
        bokeh: { n: 16, colors: ['#ffd9a0', '#ffffff', '#ffb060', '#a8c8ff'], min: 0.015, max: 0.05, seed: 5 },
        // Background colours sampled around the reference board, painted as soft spots and tuned until each
        // sample point equals the reference's (includes the bright blue-white light behind the top-right corner).
        vignette: 0, spotBlur: 0.025,
        spots: [
          [0.183, 0.023, 0.06, '#464779'],
          [0.300, 0.023, 0.06, '#3991f8'],
          [0.417, 0.023, 0.06, '#31b0fd'],
          [0.533, 0.023, 0.06, '#31b9fd'],
          [0.650, 0.023, 0.06, '#5fbcfc'],
          [0.767, 0.023, 0.06, '#2d96fb'],
          [0.883, 0.023, 0.06, '#495bce'],
          [0.023, 0.117, 0.06, '#214eb0'],
          [0.023, 0.233, 0.06, '#1a52b2'],
          [0.023, 0.350, 0.06, '#48417b'],
          [0.023, 0.467, 0.06, '#f37861'],
          [0.023, 0.583, 0.06, '#f5895a'],
          [0.023, 0.700, 0.06, '#523862'],
          [0.977, 0.117, 0.06, '#4b5dd0'],
          [0.977, 0.217, 0.06, '#001f3e'],
          [0.977, 0.317, 0.06, '#47345d'],
          [0.977, 0.417, 0.06, '#ffc972'],
          [0.883, 0.100, 0.06, '#8987f0'],
          [0.933, 0.183, 0.06, '#4f72e8'],
        ],
        // Dark dome across the bottom of the tile, warm plum-grey under the board fading to navy at the bottom: peaks ~70% down behind the board and falls away to
        // ~83% at both side edges (traced from the reference).
        ellipses: [[0.5, 1.2, 0.75, 0.5, '#0c1327', 0.012, '#22191cfc', '#2c2738fc', '#3b323ffc']],
        shapes: [[0.78, 0.08, 0.12, 0.5, '#3a5ab8'], [0.9, 0.15, 0.08, 0.4, '#5a7ad8'], [0.02, 0.35, 0.05, 0.3, '#e08a40']],
      },
    }
  },

  checkers: () => {
    // Reference: a chunky physical toy board photographed on a table: thick casing, individually
    // bevelled inset squares, sculpted pucks with slight variation, warm raking light, shallow focus.
    const N = 6
    const SQ = 0.55
    const size = N * SQ
    const surface = noiseTexture({ contrast: 0.1, seed: 43 })
    const casing = mat('#171310', { roughness: 0.2, roughnessMap: surface, bumpMap: surface, bumpScale: 0.4, clearcoat: 0.8, clearcoatRoughness: 0.12, envMapIntensity: 0.6 })
    const parts = [
      mesh(rbox(size + 0.6, 0.4, size + 0.6, 0.18, 6), casing, [0, -0.125, 0]), // casing: contains the board, doesn't dominate
    ]
    // Raised lip around a recessed playing field.
    const lipW = 0.3
    for (const [x, z, w, d] of [[0, (size + lipW) / 2, size + lipW * 2, lipW], [0, -(size + lipW) / 2, size + lipW * 2, lipW], [(size + lipW) / 2, 0, lipW, size], [-(size + lipW) / 2, 0, lipW, size]])
      parts.push(mesh(rbox(w, 0.2, d, 0.08, 5), casing, [x, 0.12, z]))
    // Individual inset tiles with soft bevels and a little tonal variation.
    const tileGeo = rbox(SQ - 0.04, 0.1, SQ - 0.04, 0.035, 4) // dark micro-gaps + bevel catch light
    // Tactile, not literally rough: faint roughness and bump noise varies the reflections.
    const grain = noiseTexture({ contrast: 0.18, seed: 41 })
    const at = (c, r) => [(c - (N - 1) / 2) * SQ, 0, (r - (N - 1) / 2) * SQ]
    for (let r = 0; r < N; r++) for (let c = 0; c < N; c++) {
      const dark = (r + c) % 2 === 1
      const [x, , z] = at(c, r)
      const tile = mesh(tileGeo, mat(dark ? '#24150c' : '#e4a85e', { roughness: dark ? 0.45 : 0.6, roughnessMap: grain, bumpMap: grain, bumpScale: 0.8, clearcoat: dark ? 0.25 : 0.05, clearcoatRoughness: 0.3 }), [x, 0.03, z])
      parts.push(vary(tile, 100 + r * N + c, { value: 0.015, rough: 0.025, height: 0.02 }))
      tile.rotation.set(0, 0, 0) // tiles stay square; tone, roughness and a hair of height vary
    }
    // Lower, heavier pucks: radius +5%, height -10%.
    const geo = toyPuckGeometry({ radius: 0.268, height: 0.202 })
    const red = mat('#d80010', { roughness: 0.24, clearcoat: 1, clearcoatRoughness: 0.08, envMapIntensity: 0.35, sheen: 0.3, sheenColor: '#ff4040' })
    const black = mat('#1c1917', { roughness: 0.35, clearcoat: 0.6, clearcoatRoughness: 0.25, envMapIntensity: 0.4 })
    const pieces = []
    const put = (c, r, m) => {
      const [x, , z] = at(c, r)
      const p = vary(mesh(geo, m, [x, 0.08, z]), pieces.length + 1, { rot: 2, rough: 0.025, value: 0.015, scale: 0.0075 })
      pieces.push(p)
      parts.push(p)
    }
    // Mixed mid-game position.
    ;[[1, 0], [3, 0], [2, 1], [4, 1], [3, 2], [5, 2], [2, 3], [4, 3], [1, 2]].forEach(([c, r]) => put(c, r, black))
    ;[[0, 1], [5, 0], [0, 3], [1, 4], [3, 4], [2, 5], [4, 5], [5, 4]].forEach(([c, r]) => put(c, r, red))
    // The table it sits on: falls out of focus toward the edges.
    // Ends just behind the board so the warm room bokeh shows past its far edge.
    const table = mesh(rbox(12, 0.3, 6.2, 0.1), mat('#ffffff', {
      map: woodTexture({ base: '#6a3414', dark: '#5a2a0e', light: '#7a3e1c', seed: 31, size: 1024 }),
      roughness: 0.4, clearcoat: 0.5, clearcoatRoughness: 0.2,
    }), [0, -0.475, 0.6])
    const board = group(parts, [0, 0, 0], [0, -0.32, 0])
    return {
      object: group([board, table]),
      // Key art, not the player camera: higher, farther, longer lens, the whole board as an object.
      // Sitting over an active game: close, higher, cropped asymmetrically on the middle pieces.
      frame: pieces.slice(3, 15),
      view: { pitch: 0.7, yaw: 0.0, fill: 1.0, shift: [0.04, -0.1], roll: -0.08, fov: 24 },
      look: {
        // Backlit: warm light rakes toward the camera across the squares.
        keyFrom: [-2.0, 3.2, -1.2],
        glow: { amount: 0.25, radius: 0.035, tint: ['#ff9a40', 0.1] },
        envIntensity: 0.12, ambient: ['#ffb070', '#3a1a08', 0.9], keyIntensity: 2.0, keyColor: '#ffd8a0',
        rim: { intensity: 0.08, color: '#ffc080' }, aoIntensity: 2.2, aoRadius: 0.016,
        softbox: { color: '#ffe6c4', intensity: 7, width: 1.8, height: 0.8, from: [-1.0, 2.0, 0.4] },
        // Showcase DOF: nearest and rear pieces soften, centre stays sharpest.
        dof: { aperture: 0.008, maxblur: 0.02 },
      },
      backdrop: {
        gradient: [180, '#3a2010', '#8a4a1a', '#2a1408'],
        masses: [[0.85, 0.1, 0.3, '#ffb050'], [0.1, 0.15, 0.25, '#d07a30'], [0.9, 0.7, 0.3, '#c86a20'], [0.1, 0.9, 0.3, '#3a1a0a']],
        bokeh: { n: 14, colors: ['#ffd090', '#ffb060', '#fff0c0'], min: 0.02, max: 0.07, seed: 9 },
        // A room beyond the focal plane: cabinet, shelves, lamp glow, window.
        shapes: [
          [0.02, 0.05, 0.22, 0.45, '#2a140a'], [0.05, 0.12, 0.16, 0.02, '#6a3a1a'], [0.05, 0.25, 0.16, 0.02, '#6a3a1a'],
          [0.62, 0.02, 0.2, 0.26, '#f0b870', 0.02], [0.63, 0.04, 0.08, 0.22, '#ffd8a0', 0.01],
          [0.86, 0.06, 0.06, 0.08, '#ffe0a0', 0.03], [0.88, 0.14, 0.02, 0.3, '#3a1a0a'],
          [0.3, 0.1, 0.12, 0.3, '#3a1c0c'],
          [0.44, 0.0, 0.12, 0.09, '#ffcf88', 0.05], [0.47, 0.09, 0.06, 0.04, '#ffeccc', 0.02], // lamp shade + bulb glow
          [0.24, 0.3, 0.05, 0.05, '#ffb860', 0.025], [0.53, 0.26, 0.04, 0.06, '#e08a40', 0.01], // table objects
          [0.92, 0.0, 0.08, 0.18, '#ffdca0', 0.01], // second window
        ],
      },
    }
  },

  'ring-toss': () => {
    // Reference: close-up of a wooden peg rising out of grass, a red ring high and tilted,
    // a blue ring below it, a yellow ring leaning on the right; blue sky bokeh behind.
    const pine = mat('#ffffff', { map: woodTexture({ base: '#c8783a', dark: '#b06a30', light: '#dc8c48', seed: 21 }), roughness: 0.55, clearcoat: 0.2, clearcoatRoughness: 0.4 })
    const peg = new THREE.LatheGeometry(
      [[0, 0], [0.24, 0], [0.22, 2.6], [0.24, 2.7], [0.2, 2.82], [0, 2.86]].map(([x, y]) => new THREE.Vector2(x, y)),
      48,
    )
    const ring = (color, emissive) => mat(color, { roughness: 0.22, clearcoat: 1, clearcoatRoughness: 0.08, emissive, emissiveIntensity: emissive ? 0.12 : 0 })
    const tube = (r, t) => new THREE.TorusGeometry(r, t, 40, 100)
    return {
      object: group([
        // Peg leans right; rings are threaded on its axis (point at length t: base + axis * t).
        mesh(peg, pine, [-0.2, -0.6, 0], [0, 0, -0.38]),
        mesh(tube(0.95, 0.24), ring('#1446ff'), [-0.2 + 0.37 * 0.9, -0.6 + 0.93 * 0.9, 0], [-1.0, 0, -0.2]),
        mesh(tube(0.82, 0.24), ring('#e8001a'), [-0.2 + 0.37 * 1.55, -0.6 + 0.93 * 1.55, 0], [-1.05, 0, -0.3]),
        mesh(tube(0.56, 0.21), ring('#f5a000', '#ff6a00'), [-0.2 + 0.37 * 1.55 + 0.8, -0.6 + 0.93 * 1.55 - 0.3, 0.45], [-0.9, 0.35, 0.25]),
      ]),
      view: { pitch: 0.35, yaw: 0, fill: 0.88, shift: [0.0, 0.02], roll: 0 },
      look: { envIntensity: 0.75 },
      backdrop: {
        gradient: [180, '#5a8ae0', '#3a6ad0', '#2a7a3a'],
        masses: [[0.15, 0.2, 0.25, '#ffc070'], [0.85, 0.15, 0.25, '#8ab0ff'], [0.5, 1.0, 0.45, '#2a7a2a'], [0.15, 0.85, 0.25, '#4aa03a'], [0.5, 0.95, 0.2, '#1a3a12']],
        bokeh: { n: 14, colors: ['#ffffff', '#ffd080', '#a0c8ff'], min: 0.015, max: 0.05, seed: 13, yMax: 0.6 },
      },
    }
  },

  'whack-a-mole': () => {
    // Reference: mole bursting from a hole lower-left, giant red mallet swinging in top-right,
    // lush grass with more holes, sunny blurred sky behind.
    // Lush lawn: saturated green with darker blade streaks so it doesn't read as flat plastic.
    const lawn = canvasTexture(512, 512, (ctx, W) => {
      ctx.fillStyle = '#2bb51a'
      ctx.fillRect(0, 0, W, W)
      let r = 5
      const rand = () => ((r = (r * 16807) % 2147483647) / 2147483647)
      for (let i = 0; i < 2600; i++) {
        ctx.strokeStyle = rand() < 0.5 ? 'rgba(10,90,5,.35)' : 'rgba(150,240,80,.35)'
        ctx.lineWidth = 1 + rand() * 1.5
        const x = rand() * W
        const y = rand() * W
        ctx.beginPath()
        ctx.moveTo(x, y)
        ctx.lineTo(x + (rand() - 0.5) * 4, y - 6 - rand() * 8)
        ctx.stroke()
      }
    })
    lawn.wrapS = lawn.wrapT = THREE.RepeatWrapping
    lawn.repeat.set(6, 6)
    const grass = mat('#ffffff', { map: lawn, roughness: 0.85, clearcoat: 0, envMapIntensity: 0.3, emissive: '#0f5a06', emissiveIntensity: 0.25 })
    const hole = matte('#1a0d06')
    const rim = mat('#6a3c1a', { roughness: 0.95, clearcoat: 0 })
    // Dug-out holes: a dark pit with a soft lip of soil, not a hard ring.
    const holeAt = (x, z, r) => group([
      mesh(cyl(r * 1.12, r * 1.18, 0.05, 48), rim, [0, 0.01, 0]),
      mesh(cyl(r, r * 0.85, 0.06, 48), hole, [0, 0.025, 0]),
    ], [x, 0, z])
    const red = mat('#e8192a', { roughness: 0.22, clearcoat: 1, clearcoatRoughness: 0.08 })
    const gold = metal('#f2b33a')
    const hammer = group([
      mesh(cyl(0.62, 0.62, 1.5, 64), red, [0, 0, 0], [0, 0, Math.PI / 2]),
      mesh(torus(0.62, 0.07, 64), gold, [-0.55, 0, 0], [0, Math.PI / 2, 0]),
      mesh(torus(0.62, 0.07, 64), gold, [0.55, 0, 0], [0, Math.PI / 2, 0]),
      mesh(cyl(0.6, 0.6, 0.06, 64), mat('#c8141f', { roughness: 0.3 }), [-0.76, 0, 0], [0, 0, Math.PI / 2]),
      mesh(cyl(0.14, 0.16, 2.6, 24), wood('#d08a4a'), [0, -1.35, 0]),
    ], [1.85, 2.85, 0.6], [0, 0, 0], 1.4)
    // Striking face points down-left toward the camera; handle runs off to the lower right.
    {
      const face = new THREE.Vector3(-0.55, -0.35, 0.75).normalize()
      const handle = new THREE.Vector3(0.85, -0.45, 0.2)
      handle.addScaledVector(face, -handle.dot(face)).normalize()
      const up = handle.clone().negate()
      hammer.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(face, up, new THREE.Vector3().crossVectors(face, up)))
    }
    const moleGroup = group([heroMole()], [-0.45, -0.25, 0.3], [0, 0.18, 0], 1.3)
    return {
      // Frame the mole and mallet; the grass runs off the tile edges.
      frame: [moleGroup],
      object: group([
        mesh(cyl(9, 9, 0.3, 64), grass, [0, -0.15, 0]),
        holeAt(-0.45, 0.3, 1.2),
        holeAt(1.9, 0.6, 0.6),
        holeAt(1.4, 2.0, 0.55),
        moleGroup,
        grassTuft([-1.6, 0, 1.2], 1.6), grassTuft([0.8, 0, 1.4], 1.3), grassTuft([2.6, 0, -0.4], 1.5), grassTuft([-2.2, 0, -0.6], 1.4),
        hammer,
      ]),
      view: { pitch: 0.24, yaw: 0, fill: 0.6, shift: [-0.13, -0.11] },
      look: { envIntensity: 0.7, keyColor: '#fff0d8', rim: { intensity: 0.04 } },
      backdrop: {
        gradient: [180, '#4a8ae8', '#9ac0f0', '#f0c070'],
        masses: [[0.15, 0.35, 0.25, '#e0a050'], [0.85, 0.3, 0.25, '#ffd080'], [0.5, 0.05, 0.25, '#6aa8ff'], [0.1, 0.1, 0.15, '#c08050']],
        bokeh: { n: 10, colors: ['#ffffff', '#ffe0a0'], min: 0.02, max: 0.05, seed: 23, yMax: 0.5 },
      },
    }
  },

  darts: () => {
    const face = canvasTexture(1024, 1024, (ctx, W) => {
      const c = W / 2
      const seg = (r0, r1, colors) => {
        for (let i = 0; i < 20; i++) {
          const a0 = ((i - 0.5) / 20) * Math.PI * 2 - Math.PI / 2
          const a1 = ((i + 0.5) / 20) * Math.PI * 2 - Math.PI / 2
          ctx.beginPath()
          ctx.arc(c, c, r1, a0, a1)
          ctx.arc(c, c, r0, a1, a0, true)
          ctx.closePath()
          ctx.fillStyle = colors[i % 2]
          ctx.fill()
        }
      }
      // Black number band with white numbers.
      ctx.fillStyle = '#121216'
      ctx.beginPath()
      ctx.arc(c, c, c, 0, Math.PI * 2)
      ctx.fill()
      // Silver studs around the rim instead of numbers, as in the reference.
      for (let i = 0; i < 20; i++) {
        const ang = (i / 20) * Math.PI * 2
        ctx.beginPath()
        ctx.arc(c + Math.cos(ang) * c * 0.9, c + Math.sin(ang) * c * 0.9, 9, 0, Math.PI * 2)
        ctx.fillStyle = '#cfd4de'
        ctx.fill()
      }
      const rr = c * 0.82
      seg(rr * 0.94, rr, ['#e3122b', '#14a34a']) // double
      seg(rr * 0.6, rr * 0.94, ['#16161a', '#f3e2bd'])
      seg(rr * 0.54, rr * 0.6, ['#e3122b', '#14a34a']) // treble
      seg(rr * 0.1, rr * 0.54, ['#16161a', '#f3e2bd'])
      ctx.beginPath()
      ctx.arc(c, c, rr * 0.1, 0, Math.PI * 2)
      ctx.fillStyle = '#14a34a'
      ctx.fill()
      ctx.beginPath()
      ctx.arc(c, c, rr * 0.045, 0, Math.PI * 2)
      ctx.fillStyle = '#e3122b'
      ctx.fill()
      // Silver spider wires.
      ctx.strokeStyle = '#cfd4de'
      ctx.lineWidth = 2.5
      for (const f of [1, 0.94, 0.6, 0.54, 0.1, 0.045]) {
        ctx.beginPath()
        ctx.arc(c, c, rr * f, 0, Math.PI * 2)
        ctx.stroke()
      }
      for (let i = 0; i < 20; i++) {
        const a = ((i - 0.5) / 20) * Math.PI * 2 - Math.PI / 2
        ctx.beginPath()
        ctx.moveTo(c + Math.cos(a) * rr * 0.1, c + Math.sin(a) * rr * 0.1)
        ctx.lineTo(c + Math.cos(a) * rr, c + Math.sin(a) * rr)
        ctx.stroke()
      }
    })
    // The cylinder cap maps the canvas rotated a quarter turn; turn it back so 20 sits on top.
    face.center.set(0.5, 0.5)
    face.rotation = Math.PI / 2
    const sisal = mat('#ffffff', { map: face, roughness: 0.85, clearcoat: 0 })
    const board = mesh(cyl(1.5, 1.5, 0.5, 128), [mat('#18181e', { roughness: 0.45 }), sisal, matte('#151519')], [0, 0, 0], [Math.PI / 2, 0, 0])
    const surround = mesh(torus(1.55, 0.16, 128), mat('#1e1e26', { roughness: 0.3, clearcoat: 0.6 }), [0, 0, 0.16])

    const flight = (color) => {
      const s = new THREE.Shape()
      s.moveTo(0, 0)
      s.lineTo(0.24, 0.12)
      s.lineTo(0.26, 0.42)
      s.lineTo(0, 0.5)
      s.closePath()
      return new THREE.ExtrudeGeometry(s, { depth: 0.012, bevelEnabled: false })
    }
    const dart = (color, pos, rot) => {
      const fm = mat(color, { roughness: 0.3, side: THREE.DoubleSide })
      const fg = flight()
      return group([
        mesh(cone(0.035, 0.32, 16), metal('#e6e9f0'), [0, -0.16, 0], [Math.PI, 0, 0]),
        mesh(cyl(0.075, 0.06, 0.5, 32), metal('#b9a06a'), [0, 0.25, 0]),
        mesh(cyl(0.05, 0.075, 0.08, 32), metal('#c9ced8'), [0, 0.54, 0]),
        mesh(cyl(0.032, 0.032, 0.6, 16), mat('#24242c', { roughness: 0.3 }), [0, 0.88, 0]),
        ...[0, 1, 2, 3].map((k) => mesh(fg, fm, [0, 1.0, 0], [0, (k * Math.PI) / 2, 0])),
      ], pos, rot)
    }
    // Chunky toy dart: red barrel, three wide fins.
    const red = mat('#ee1520', { roughness: 0.3, clearcoat: 0.8, clearcoatRoughness: 0.15 })
    const fin = new THREE.Shape()
    fin.moveTo(0, 0)
    fin.quadraticCurveTo(0.32, 0.12, 0.36, 0.55)
    fin.lineTo(0.06, 0.66)
    fin.lineTo(0, 0.45)
    fin.closePath()
    const finGeo = extrude(fin, 0.03, 0.02)
    const bigDart = group([
      mesh(cone(0.04, 0.35, 16), metal('#dfe4ec'), [0, -0.17, 0], [Math.PI, 0, 0]),
      mesh(capsule(0.1, 0.55), red, [0, 0.35, 0]),
      mesh(cyl(0.05, 0.05, 0.5, 16), red, [0, 0.85, 0]),
      ...[0, 1, 2].map((k) => group([mesh(finGeo, red, [0, 0, -0.015])], [0, 0.85, 0], [0, (k * Math.PI * 2) / 3, 0])),
    ], [0, 0, 0], [0, 0, 0], 1.2)
    // Stick the tip in the bullseye of the turned board, shaft pointing up-right toward the camera.
    const turn = 0.5
    const bull = new THREE.Vector3(Math.sin(turn) * 0.27, 0.0, Math.cos(turn) * 0.27)
    const dir = new THREE.Vector3(0.7, 0.45, 0.6).normalize()
    bigDart.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir)
    bigDart.position.copy(bull).addScaledVector(dir, 0.3 * 1.2)
    return {
      object: group([group([board, surround], [0, 0, 0], [0, turn, 0]), bigDart]),
      view: { pitch: 0.12, yaw: 0, fill: 0.95, shift: [-0.02, 0.02], roll: 0 },
      look: { envIntensity: 0.6 },
      backdrop: {
        gradient: [180, '#2a3aa8', '#3a2a88', '#1a1a5a'],
        masses: [[0.1, 0.15, 0.25, '#4a6aff'], [0.15, 0.85, 0.3, '#c0508a'], [0.85, 0.8, 0.25, '#8a3aa8'], [0.8, 0.1, 0.2, '#6a4aff']],
        bokeh: { n: 26, colors: ['#ffb060', '#ff8a4a', '#a0b0ff', '#ffd0a0'], min: 0.006, max: 0.025, seed: 17 },
      },
    }
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
