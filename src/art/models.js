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
    const WOOD_LEFT = [0.665899, 1.582582, 4.868346]
    const WOOD_FRONT = [0.482445, 0.541188, 0.839459]
    // Top-face gradient, tuned to the reference's rim samples: lighter and yellower toward the back and
    // right, a little darker at the front.
    const TOP_BACK = [1.040722, 2.212584, 3.049769]
    const TOP_RIGHT = [1.833755, 1.702495, 0.29334]
    const TOP_FRONT = [0.699675, 0.74028, 1.219953]
    // Inside wall faces and cross-bar sides are darker than the lit tops.
    const INNER_WALL = [0.045, 0.035, 0.03]
    railWood.userData.faceTint = { side: INNER_WALL }
    wood.userData.faceTint = {
      left: WOOD_LEFT, front: WOOD_FRONT, side: [0.82, 0.72, 0.68], inner: INNER_WALL, innerRight: [0.4, 0.38, 0.36], innerBack: [0.25, 0.22, 0.2], outer: [INNER / 2 + 0.05, INNER_DEPTH / 2 + 0.05], // darker reddish-brown sides
      topBack: TOP_BACK, topRight: TOP_RIGHT, topFront: TOP_FRONT, extent: [INNER / 2 + 0.25, INNER_DEPTH / 2 + 0.25],
    }
    const DIV_W = 0.10 // divider width
    const S = (INNER + DIV_W) / 3 // side-to-side cell pitch (pieces and dividers sit on it)
    const SZ = (INNER_DEPTH + DIV_W) / 3 // front-to-back cell pitch
    const RIM = 0.25 // frame wall thickness
    // The right wall is a little thicker so it looks as wide on screen as the left (it is foreshortened).
    const RIM_RIGHT = 0.25
    const OUTER = INNER + RIM + RIM_RIGHT
    // The back wall is foreshortened too, so it is a little thicker to read as wide as the others.
    const RIM_BACK = 0.22
    const OUTER_DEPTH = INNER_DEPTH + RIM + RIM_BACK
    const floorTop = 0.52 // black cell floor (deep wells); pieces rest on it
    const H = 0.88 // whole tray height: tall side walls show
    // Pieces rest on the floor; their tops sit PIECE_DROP just below the board face.
    const PIECE_DROP = 0.07
    const PIECE_HEIGHT = 0.88 - 0.6 - PIECE_DROP // fixed piece height (0.21), independent of the floor depth
    // Map the grain once across the whole frame; repeating it every unit showed up as seams on the rim.
    // Rounded corners in plan, but crisp edges: only a tight bevel where top meets sides.
    const WALL_EXTRA = 0.15 // outer walls run this much further down below the board, for a taller left side
    const frameGeo = trayFrameGeometry({ outer: OUTER, outerDepth: OUTER_DEPTH, outerCenter: [(RIM_RIGHT - RIM) / 2, -(RIM_BACK - RIM) / 2], inner: INNER, innerDepth: INNER_DEPTH, height: H + WALL_EXTRA, outerRadius: 0.2, innerRadius: 0.06, bevel: 0.025 })
    frameGeo.translate(0, -WALL_EXTRA, 0)
    // Move the board's bottom front-right corner onto the reference's (about 550, 430 at 600 px): the bottom
    // shifts right (tapering to nothing at the left wall) and slightly forward; the top face and inside walls stay put.
    const TL_DROP = 0.09 // how far the top back-left corner is lowered (the edge glow follows it)
    const BASE_SHIFT = [0.36, 0.125] // [x at the right wall, z]
    const BACK_LEFT_SHIFT = [0, -0.3] // [x, z] at the bottom back-left corner
    {
      const pos = frameGeo.attributes.position
      const bottom = -WALL_EXTRA
      const xl = -OUTER / 2, w = OUTER
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i), z = pos.getZ(i)
        // Outer faces only: the inside walls stay upright so no gap opens beside the floor.
        const d = Math.max(x - INNER / 2, -INNER / 2 - x, z - INNER_DEPTH / 2, -INNER_DEPTH / 2 - z)
        const out = Math.min(1, Math.max(0, (d - 0.03) / 0.05))
        const t = (H - pos.getY(i)) / (H - bottom) * out
        const across = Math.min(1, Math.max(0, (x - xl) / w))
        // The bottom back-left corner moves onto the reference's too (about 26, 146 at 600 px).
        const back = Math.min(1, Math.max(0, (OUTER_DEPTH / 2 - z) / OUTER_DEPTH))
        const bl = (1 - across) * back
        pos.setX(i, x + BASE_SHIFT[0] * t * across + BACK_LEFT_SHIFT[0] * t * bl)
        pos.setZ(i, z + BASE_SHIFT[1] * t + BACK_LEFT_SHIFT[1] * t * bl)
      }
      // Lower the top back-left corner: the rim, inner edge included, drops toward that corner.
      const zb = -OUTER_DEPTH / 2 - (RIM_BACK - RIM) / 2
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i)
        const u = Math.min(1, Math.max(0, (x - xl) / w))
        const v = Math.min(1, Math.max(0, (z - zb) / OUTER_DEPTH))
        const up = Math.max(0, (y - bottom) / (H - bottom))
        pos.setY(i, y - TL_DROP * (1 - u) * (1 - v) * up)
      }
      frameGeo.computeVertexNormals()
    }
    const uv = frameGeo.attributes.uv
    for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) / OUTER + 0.5, uv.getY(i) / OUTER + 0.5)
    // Black cell floor, its top-left (back-left) corner lowered by FLOOR_TL_DROP,
    // tapering to nothing toward the other corners.
    const FLOOR_TL_DROP = 0.12
    const lowerFloorTL = (geo) => {
      const pos = geo.attributes.position
      for (let i = 0; i < pos.count; i++) {
        if (pos.getY(i) < floorTop - 0.05) continue // only the top surface
        const u = Math.min(1, Math.max(0, (pos.getX(i) + INNER / 2) / INNER)), v = Math.min(1, Math.max(0, (pos.getZ(i) + INNER_DEPTH / 2) / INNER_DEPTH))
        pos.setY(i, pos.getY(i) - FLOOR_TL_DROP * (1 - u) * (1 - v))
      }
      geo.computeVertexNormals()
      return geo
    }
    const slabTop = floorTop - FLOOR_TL_DROP - 0.01 // the slab sits just under the lowest point of the floor
    const floorSlab = rbox(INNER + 0.1, slabTop, INNER_DEPTH + 0.1, 0.02, 2).translate(0, slabTop / 2, 0)
    const floorPlane = lowerFloorTL(new THREE.PlaneGeometry(INNER, INNER_DEPTH, 24, 24).rotateX(-Math.PI / 2).translate(0, floorTop + 0.002, 0))
    const parts = [
      // One-piece frame: rounded outer corners (radius 0.45), rounded inner corners, no seams.
      mesh(frameGeo, wood),
      mesh(floorSlab, wood), // floor slab, hidden under cells
      mesh(floorPlane, black),
    ]
    // Glowing top-right edge (white), as in the reference: a thin bright strip along the outer top edge from the
    // back edge, round the top-right corner and down the right edge, fading out at both ends; bloom
    // spreads it into a soft halo.
    // back: how far it runs along the back edge from the corner; right: along the right edge.
    const EDGE_GLOW = { color: '#ffffff', width: 0.016, opacity: 0.6, back: 2.9, right: 2.45, offset: 0.01 }
    // Optional blur: wider, fainter copies of the strip around it ([width multiple, brightness multiple]); none now.
    const EDGE_GLOW_BLUR = [[1, 1]]
    {
      // Hugs the board's outline just outside the outer walls and a hair below the top, following the
      // rounded corner (outer radius 0.2), so the board covers the strip and only its glow shows behind it.
      const o = EDGE_GLOW.offset
      const hx = INNER / 2 + RIM_RIGHT + o, hz = INNER_DEPTH / 2 + RIM_BACK + o, cr = 0.2 + o, y = H - 0.035 // the real outer right / back edges
      const pts = []
      for (let t = 0; t < 1; t += 0.02) pts.push(new THREE.Vector3(hx - cr - EDGE_GLOW.back * (1 - t), y, -hz))
      for (let a = 0; a <= 1; a += 0.05) pts.push(new THREE.Vector3(hx - cr + Math.sin(a * Math.PI / 2) * cr, y, -hz + cr - Math.cos(a * Math.PI / 2) * cr))
      for (let t = 0.02; t <= 1; t += 0.02) pts.push(new THREE.Vector3(hx, y, -hz + cr + EDGE_GLOW.right * t))
      // Follow the lowered top back-left corner so the strip stays tucked under the edge.
      for (const p of pts) p.y -= TL_DROP * (1 - Math.min(1, Math.max(0, (p.x + OUTER / 2) / OUTER))) * (1 - Math.min(1, Math.max(0, (p.z + OUTER_DEPTH / 2) / OUTER_DEPTH)))
      const curve = new THREE.CatmullRomCurve3(pts)
      const segs = 480, radial = 8
      for (const [wm, sm] of EDGE_GLOW_BLUR) {
      const geo = new THREE.TubeGeometry(curve, segs, EDGE_GLOW.width * wm, radial, false)
      // Taper: full width and brightness at the corner, thinning and fading to nothing at both ends.
      const total = EDGE_GLOW.back + (Math.PI / 2) * cr + EDGE_GLOW.right
      const uc = (EDGE_GLOW.back + (Math.PI / 4) * cr) / total // corner, as a fraction of the length
      const taper = (u) => Math.max(0, u < uc ? u / uc : (1 - u) / (1 - uc))
      const pos = geo.attributes.position
      const cols = []
      const centre = new THREE.Vector3()
      const v = new THREE.Vector3()
      for (let i = 0; i <= segs; i++) {
        const f = taper(i / segs)
        curve.getPointAt(i / segs, centre)
        const c = new THREE.Color(EDGE_GLOW.color)
        const a = f ** 1.2 * EDGE_GLOW.opacity * sm // 60% opaque at the corner, fading to clear at both ends
        for (let j = 0; j <= radial; j++) {
          const k = i * (radial + 1) + j
          v.fromBufferAttribute(pos, k).sub(centre).multiplyScalar(f).add(centre)
          pos.setXYZ(k, v.x, v.y, v.z)
          cols.push(c.r, c.g, c.b, a)
        }
      }
      geo.setAttribute('color', new THREE.Float32BufferAttribute(cols, 4))
      parts.push(new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, toneMapped: false })))
      }
    }
    // Dividers are flush with the frame top (a hair under, to avoid z-fighting where they run into the walls);
    // their ends run into the walls so no rounded stub shows.
    const DIV_DROP = 0.05 // dividers sit a little below the frame's top edge
    const DIV_H = H - floorTop - DIV_DROP
    const RAIL_GAP = -0.05 // bars run into the frame walls: flush, no gap
    // The lowered top back-left corner slopes the wall top down toward that corner; the left vertical bar (and
    // its joints) follow that slope so they sit as far below the wall top as the other bars do, and the right
    // vertical bar (and its joints) is given the same height.
    const followTL = (geo, cx, cy, cz) => {
      const pos = geo.attributes.position
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i) + cx, y = pos.getY(i) + cy, z = pos.getZ(i) + cz
        const u = Math.min(1, Math.max(0, (x + OUTER / 2) / OUTER)), v = Math.min(1, Math.max(0, (z + OUTER_DEPTH / 2) / OUTER_DEPTH))
        pos.setY(i, pos.getY(i) - TL_DROP * (1 - u) * (1 - v) * Math.max(0, (y - floorTop) / (H - floorTop)))
      }
      geo.computeVertexNormals()
      return geo
    }
    for (const o of [-0.5, 0.5]) {
      const hBar = rbox(INNER - 2 * RAIL_GAP, DIV_H, DIV_W, 0.025, 4)
      if (o < 0) followTL(hBar, 0, floorTop + DIV_H / 2, o * SZ) // the top (back) bar follows the corner's slope too
      parts.push(mesh(hBar, railWood, [0, floorTop + DIV_H / 2, o * SZ]))
      const vBar = rbox(DIV_W, DIV_H, INNER_DEPTH - 2 * RAIL_GAP, 0.025, 4)
      followTL(vBar, -0.5 * S, floorTop + DIV_H / 2, 0) // both vertical bars take the left bar's height profile
      parts.push(mesh(vBar, railWood, [o * S, floorTop + DIV_H / 2, 0]))
    }
    // Raised square blocks where the bars cross: flush with the bars' sides (a hair inside, to avoid
    // z-fighting), standing proud of them only in height.
    const JOINT_W = DIV_W - 0.002
    const JOINT_H = DIV_H + 0.06
    for (const ox of [-0.5, 0.5]) for (const oz of [-0.5, 0.5])
      parts.push(mesh(followTL(rbox(JOINT_W, JOINT_H, JOINT_W, 0.03, 4), -0.5 * S, floorTop + JOINT_H / 2, oz * SZ), railWood, [ox * S, floorTop + JOINT_H / 2, oz * SZ]))
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
    red.userData.faceTint = { side: [0.3, 0.25, 0.25], hole: [0.2, 0.15, 0.15] } // darker sides; the hole walls darker still
    const ringGeo = toyOGeometry({ radius: PIECE_SPAN / 2 - O_BAND / 2, width: O_BAND, height: PIECE_HEIGHT, round: PIECE_ROUND }) // flat top, slightly rounded edges
    const O_SHIFT_Z = -0.04 // O pieces sit slightly toward the top (back) wall of their cells
    const O_RIGHT_SHIFT_X = 0.04 // the right column's O pieces sit slightly toward the right wall
    const layout = ['X', 'X', 'O', 'O', 'O', 'O', 'X', 'X', 'O']
    layout.forEach((p, i) => {
      // Both pieces are centred on their origin and PIECE_HEIGHT tall: rest on the floor, tops flush with the board face.
      const piece = p === 'X' ? X() : mesh(ringGeo, red)
      piece.position.set(((i % 3) - 1) * S + (p === 'O' && i % 3 === 2 ? O_RIGHT_SHIFT_X : 0), floorTop + PIECE_HEIGHT / 2, (Math.floor(i / 3) - 1) * SZ + (p === 'O' ? O_SHIFT_Z : 0))
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
        // The background proper: a 48 x 48 colour field sampled from the reference around the board, stretched
        // smoothly over the frame and tuned until the render matches the reference cell by cell.
        vignette: 0,
        grid: { cols: 48, blur: 0.01, colors: [
          '8b79c5 8c77c1 8177c5 997bbe 5a4f94 895983 a76b88 c57f7e e29a7f bb7080 8c6a8d 875f96 5a5a9c 5b5eb3 3f8af8 3facff 3eb4fc 3fb8fd 40bffe 40c4fd 41c8fd 3fc6fe 3bc6fa 3dc6fd 3ac8fb 3fccfb 45d1fb 4bd0fa 50d0f9 4ccef5 39bffe ffe7db fff5c3 84b8cd a2cbc2 c5c295 e8d58f 38acff 2f94fd 278bf3 2272e7 255ed0 7d63ed ead9e4 ffffdf d2afe1 ead6e3 e5cce4',
          '8274c3 8072bd 7970c3 9a78b9 6e5790 9d6f80 b68589 d0987e e9af81 c8877f 9f7d89 9d7194 7465a1 6a64b5 4787f9 41a8ff 3eb1fc 3cb5fc 3ebeff 42c3fc 42c9fc 41c4fc 3dc7f8 45c9fc 50ccf8 5accfa 61d2f7 57d0fa 52cefb 50cff6 3ec1ff ffe6dc fff6c4 90bdd4 b6d3ce daca9e eccd7c 239dff 2b94fc 2c88f1 236fe6 295acf 8463ec ebdbe2 ffffd9 c8a4de e0c9e1 ddc0e3',
          '967dc0 967ab8 8e79be 8d78b4 193888 441c7a 761f7b a53d66 ca5a69 943171 522b81 412c88 0e2f7e 2a459d 3281f4 40a8ff 42aafa 47adfa 4ab2ff 40b4f9 3ebcfc 3cb8ff 39b5fd 2eb1ff 17b2ff 08b8ff 00bbff 2fc5ff 43c4f2 2fbfef 12adf9 ffdfd2 fff2b6 529ab6 579fa5 7f964a c0ceb2 62b6f0 0a7cfa 297eee 1f6ddf 1057c8 5d51e3 d9c7e3 ffffe1 dbbbdf f0e1e1 ecdae3',
          '6d6bc9 7068c1 555fc9 997aba f0639b ffcb7e ffff83 ffff9c ffff94 fff888 ffd58d ffbe98 ff91d6 ae7ed6 4481fc 38a4ff 32b7ff 28bbff 21ccff 45d8ff 49d8fb 43d4f5 3bdcf1 58e0ea 81e5d7 98e2c5 f2ee83 52d7fb 54d6fa 53d3f9 77d4f7 e4e8e4 f5ebd4 b3d2d1 b4cfc3 cdd0a8 cdd0a8 35a0fa 35a0fa 2f91f6 226def 4057d1 c178ff ffffd9 ffffd4 b47fde d9b1dd d1a5e1',
          '5d6dc8 6269c2 556fc5 565bc3 ff8d91 ffffb3 e8f1a8 eae59e eaeaa3 e9eaaf ede3ae eefd94 ffe2ae ffb5cc 7d8dee 4ba9f3 96dde8 9eddce c2eab2 53dafc 51dbfb 4fdbf9 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 54d6fa 53d3f9 77d4f7 e4e8e4 f5ebd4 b3d2d1 b4cfc3 cdd0a8 cdd0a8 4ba6f9 4ba6f9 0591ff 076ce5 535fe0 ae7dff f49cd6 ffc8db 4a6beb 8a93e7 7f8de9',
          '5160c1 565fbb 3a59b8 3143bb f081a2 ffe899 f4dca6 f4e497 eceba9 efe8c4 fcf5ba fcf0b7 fad7c5 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce 51dbfb 4fdbf9 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 53d3f9 77d4f7 e4e8e4 f5ebd4 b3d2d1 b4cfc3 cdd0a8 a1c0f2 a1c0f2 a1c0f2 e7e1e0 3659f5 484fd4 5b9fee 3596f9 609ffa 3268df 4980e7 457ee8',
          '5366c2 485db8 5d6ac9 8072c4 d9a3b5 fbdeb0 fceeb2 fcf3b1 fcf8bb fdf6c3 fdf6c3 fcf0b7 fad7c5 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce 4fdbf9 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 53d3f9 77d4f7 e4e8e4 f5ebd4 b3d2d1 b4cfc3 cdd0a8 a1c0f2 a1c0f2 a1c0f2 a1c0f2 ad90eb 4d61e4 49a0ec 2092e3 3894e4 6665e4 557be6 5b7ae8',
          '5463c7 4856bb 5567cc 8072c4 d9a3b5 fbdeb0 fceeb2 fcf3b1 fcf8bb fdf6c3 fdf6c3 fdf6c3 fad7c5 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce 4fdbf9 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 53d3f9 77d4f7 e4e8e4 f5ebd4 b3d2d1 b4cfc3 a1c0f2 a1c0f2 a1c0f2 aeb5f6 aeb5f6 cac6f7 547cfa 387ee8 2796e0 449aef 6968e3 587de6 607be8',
          '425fcf 2e52cf 5168c9 5168c9 d9a3b5 fbdeb0 fceeb2 fcf3b1 fcf8bb fdf6c3 fdf6c3 fdf6c3 fad7c5 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce 4fdbf9 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 77d4f7 e4e8e4 f5ebd4 b3d2d1 a1c0f2 a1c0f2 a6b2f3 a6b2f3 a6b2f3 a6b2f3 9db4e5 606df7 233792 243c95 5c81fd 5f75de 5b77e7 5d7ae8',
          '586eaa 51638e 6372b4 6372b4 6372b4 fbdeb0 fceeb2 fcf3b1 fcf8bb fdf6c3 fdf6c3 fdf6c3 fad7c5 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 77d4f7 e4e8e4 f5ebd4 b3d2d1 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 6161ea 2a5dc1 19347f 4651e2 4481db 4471dc 4478df',
          '787397 6372b4 6372b4 6372b4 6372b4 6372b4 fceeb2 fcf3b1 fcf8bb fdf6c3 fdf6c3 fdf6c3 fdf6c3 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 77d4f7 e4e8e4 f5ebd4 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 6775dc 6775dc 6867da 476dd4 2753b1 3f65ce 3268b4 3965c0 3767c2',
          'a77886 a07e97 a07e97 6372b4 6372b4 6372b4 6372b4 fcf3b1 fcf8bb fdf6c3 fdf6c3 fdf6c3 fdf6c3 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce 51def7 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 b7e7d0 e4e8e4 a6b2f3 a6b2f3 a6b2f3 a6b2f3 a6b2f3 586cd1 586cd1 586cd1 586cd1 3352c3 3c55b4 3f5bb8 6e55d6 1d4884 384da7 304da2',
          '927286 997a96 997a96 997a96 997a96 6372b4 6372b4 6372b4 fcf8bb fdf6c3 fdf6c3 fdf6c3 fdf6c3 e0bada 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 b7e7d0 a6b2f3 a6b2f3 a6b2f3 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 4052b2 525cbf 444dbd 063456 22417f 173f7c',
          '6e6a93 7f719e 7f719e 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 fdf6c3 fdf6c3 fdf6c3 fdf6c3 fdf6c3 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 b7e7d0 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 5e68c6 5e68c6 5555b7 4464b2 015bb5 26416b 13478d 194788',
          '595da0 6a65a5 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 fdf6c3 fdf6c3 fdf6c3 fdf6c3 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 5e68c6 5e68c6 5e68c6 5e68c6 5e68c6 4457b1 0036ac ac7287 6e639f 83689c',
          '57569f 3d4c93 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 605aa0 c67d7b c67d7b fdf6c3 fdf6c3 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 b7e7d0 586cd1 586cd1 586cd1 586cd1 586cd1 586cd1 5e68c6 5e68c6 5e68c6 5e68c6 5e68c6 5d60bd 5d60bd 6264ba 223eb4 8e6160 635280 72597d',
          '594d85 4d467a 625392 625392 625392 625392 625392 c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b 96acee 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce 62e1f3 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 586cd1 586cd1 586cd1 586cd1 586cd1 5e68c6 5e68c6 5e68c6 5e68c6 5e68c6 6a5eaf 6a5eaf 6a5eaf 6a5eaf 544b9a 3545a1 4c4059 414379 494475',
          '65518a 4d487e 6a538c 6a538c c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b 73bcf4 89d6ee 9fdedd acdfce acdfce acdfce acdfce acdfce acdfce 7ce5eb 9ce8df b7e7d0 b7e7d0 b7e7d0 586cd1 586cd1 586cd1 5e68c6 5e68c6 5e68c6 5e68c6 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 665073 5c3d68 5f4271 674572',
          '604d7d 4b4375 835b83 c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b fce089 fce089 9fdedd acdfce acdfce acdfce acdfce acdfce acdfce 7ce5eb 9ce8df b7e7d0 b7e7d0 586cd1 586cd1 5e68c6 5e68c6 5e68c6 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf e18c7b e18c7b f95f5e ff9c70 ff8368 ff906f',
          'c96a73 c96c71 a64f67 c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b c67d7b fce089 fce089 fce089 fce089 fce089 fce089 fce089 acdfce acdfce acdfce acdfce acdfce acdfce 7ce5eb 9ce8df b7e7d0 b7e7d0 5e68c6 5e68c6 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf fcbf82 fcbf82 fcbf82 fcbf82 ffa56d fcff91 f9e384 fdf08c',
          'ffb376 ffb174 ffb16f f6af7c f6af7c f6af7c f6af7c f6af7c fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 acdfce acdfce acdfce acdfce acdfce 7ce5eb 9ce8df b7e7d0 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 f1ff99 f1e888 f4f493',
          'fec881 fdc57f fdc47b fcc681 fcc681 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fccb79 acdfce acdfce 3c170a 3c170a 3c170a 3c170a 5b1f08 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf 6a5eaf fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fdf195 fdf195 f8f18b f6ea8b f8ef90',
          'fab97a fbb779 f9aa72 fbc37e fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fccb79 fccb79 fccb79 fccb79 fccb79 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 6a5eaf fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fde68c fde68c fde68c fde68c f9cf74 fcd880 feda7f',
          'fdba77 fcc179 fb9e69 fbfc8f fce089 fce089 fce089 fce089 fce089 fce089 fce089 fce089 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 5b1f08 581f0a fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fcbf82 fde68c fde68c fde68c fde68c fde68c fde68c fde68c fde68c f7e681 fcea88',
          'feb66d fdbf71 fc965e ffea85 fddb84 fddb84 fddb84 fddb84 fddb84 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 d87d51 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a fcbf82 fde68c fde68c fde68c fde68c fde68c fde68c fde68c fde68c fde68c fdf292 fdf292 f4eb84 f7ee8c',
          'fb9964 fba36b f87353 ffe283 fdd07e fdd07e fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 d87d51 d87d51 d87d51 d87d51 24171c 351a17 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 581f0a 5e2510 fde68c fde68c fde68c fde68c fde68c fde68c fdee8f fdee8f fdee8f fdee8f f3da7a faf18e',
          'ef845f f09163 e85b4e ffd17b fcc77a fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 fccb79 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 fde68c fdee8f fdee8f fdee8f fdee8f fdee8f fdee8f fdee8f fdee8f fed67d',
          'f59965 f8a56b ef7955 ffdd87 fdd27d fccb79 fccb79 fccb79 fccb79 fccb79 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 5e2510 682a13 fdee8f fdee8f fdee8f fdee8f fdee8f f9b867 f9b867 f79455',
          'ffa061 ffa361 ff8757 ffc571 ff8748 f5a05f f5a05f d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 24171c 24171c 24171c 24171c 351a17 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 682a13 682a13 682a13 fdee8f f9b867 f9b867 f9b867 f9b867 f9b867',
          'a04b62 9f4c5f 8e4064 c95f5a ac3d40 d77450 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 682a13 682a13 682a13 682a13 cb884e cb884e f9b867 f9b867 f9b867',
          'c36c53 bf6b50 be6850 b76756 e27a49 e78651 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 d87d51 824335 824335 824335 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 5e2510 682a13 682a13 682a13 cb884e cb884e cb884e cb884e cb884e',
          'ae5b45 a55844 af5e45 924c41 a45544 a05d4b a9614e a9614e a9614e a9614e a9614e 824335 824335 824335 824335 824335 824335 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 682a13 682a13 682a13 6f2e1a cb884e cb884e cb884e cb884e',
          '442f46 3d2d44 493245 29283e 2e2b41 29264b 6f404d 6f404d 824335 824335 824335 824335 824335 824335 824335 824335 824335 24171c 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e2510 5e2510 5e2510 61260f 682a13 682a13 6f2e1a cb884e ffa87d aa4b32 b66141',
          '623c5f 5e3c5e 5b3d5d 5e3a5e 653b57 623b48 754048 824335 824335 824335 824335 824335 824335 824335 824335 824335 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 581f0a 581f0a 581f0a 5e210c 5e2510 5e2510 61260f 682a13 cb4859 fc5096 ffffff fff86d ffe38b ffdf82',
          '785070 77516f 6c4c67 8e5e88 7f493f 824931 71402b 824335 824335 824335 824335 824335 824335 824335 824335 331f2c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 351a17 3c170a 3c170a 3c170a 5b1f08 5b1f08 5b1f08 5d2007 581f0a 581f0a 5e210c 5e2510 833748 9c4339 ab3b48 7e2248 e21e50 ffffef ffc763 ffbe7e ffa25f',
          '5f2f4c 5f304a 592d4d 562c3f 562d2d 663832 542f2e 6f3831 6f3831 6f3831 6f3831 6f3831 331f2c 331f2c 331f2c 331f2c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 3c170a 3c170a 3c170a 4b2011 5b1f08 5b1f08 5d2007 581f0a 5c120a 96271a 7a0704 850000 eb4004 ff7634 ff9a51 ffff98 ffff9f ffa97c ffb17d ff9166',
          'a66a3a 9d6435 a26c40 b16f37 b57141 623433 372333 552c31 552c31 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 351a17 351a17 351a17 351d1b 3c170a 3c170a 4b2011 5b1f08 831600 880000 870600 e04e17 ffa74f ffff82 ffffbe fffffc ffffd4 ffff96 ffff96 ffee90 ff966d ff9f6c e4865b',
          'cb8853 be7c4d e69d5c 743f2a 392230 231c2f 221a29 3d222d 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 351a17 351a17 351d1b 3c170a 150800 8c3c13 f88248 ffc678 ffff9b ffffa3 ffff9c ffff91 ffd379 ff955f dd654c 9e3e3a 943e47 df7a63 f19868 d28767 c38960 b67f59',
          '4b2f31 412830 5d3d35 221834 2b1e2d 31202b 35202c 12162d 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 331f2c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 351a17 6e423c 995c50 995f68 a66b7a a27a82 a0717f 9a5e6f 99545c 9c5454 dc7c6f c9735a a4654e 865f4f 7a5948 5f4845 4e3e3a 473439 3e3033 604443 573c3e 5c3d3d',
          '3c2d3a 372b3a 3b2e3a 3f2936 261d2b 291b2a 211828 1b1b2f 2a1c29 2a1c29 2a1c29 2a1c29 2a1c29 111323 111323 111323 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 5c495d 5d4554 6a5374 685477 4b3859 342f45 3f3452 41304a 332836 2d2137 1a1326 151527 3f3653 383048 533d3b 5c403a 523137 452f30 452b31 443233 433136 593e41 4b383b 513a3b',
          '3c2b36 3c2b36 402e38 312635 251c2c 261d2c 201826 1e1d36 221927 221927 111323 111323 111323 111323 111323 111323 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 29181e 2d1c21 3d282d 432c2d 4b302a 3a272c 3f2e2c 362528 3a2a2f 372a33 3f2c39 403335 3e3136 403137',
          '332b36 322b36 362e37 342634 2c2231 282337 211d2b 1a203c 1a1726 111323 111323 111323 111323 111323 111323 111323 111323 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 29181e 2d1c21 3d282d 432c2d 372628 2d252a 37282b 37282e 362b34 392b33 362f36 3c2f3b 3a2f39 3a2f39',
          '292433 2a2533 2b2534 202130 1e202e 1e1e30 15192f 151c2f 11244a 111323 111323 111323 111323 111323 111323 111323 111323 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 29181e 2d1c21 3d282d 32242a 32242a 2e202a 35292d 322830 322732 302836 332737 352d3a 342c39 342c39',
          '202133 212232 222233 232031 201d30 161b2c 15182b 15192b 121b35 101323 101323 101323 101323 101323 101323 101323 101323 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 29181e 2d1c21 2b232d 2b232d 2b232d 26222f 28212f 292230 2b2432 292532 2b2736 312939 302838 302838',
          '1e1f32 202032 1f2032 1d2031 181c2e 15182b 16192a 15172c 11192e 121525 121525 121525 121525 121525 121525 121525 121525 24171c 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 29181e 23202d 23202d 23202d 23202d 1d1e2e 20202f 22202f 222432 212132 282435 282a39 272838 272838',
          '1a1d31 1b1d31 1c1d31 1a1c30 171c30 171a2c 16172c 18192c 15192d 161728 161728 161728 161728 161728 161728 161728 161728 161728 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 2d1b22 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 191d2e 1c1f2e 1c1f31 212232 232332 202335 232635 232535 222535',
          '1b1d31 1c1d30 1d1e31 1a1d30 171c30 171a2b 16182b 16172a 131729 161728 161728 161728 161728 161728 161728 161728 161728 161728 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 2e1e24 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1a1e2e 1d1f2e 1d1f31 222232 222332 232335 252735 242535 242635',
          '1a1d31 1b1d31 1b1d31 191c30 171c30 16192b 15172b 15172a 111629 161728 161728 161728 161728 161728 161728 161728 161728 161728 24171c 24171c 24171c 24171c 24171c 24171c 24171c 26181d 291b22 291c23 24171e 21161c 271a22 2d1e25 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 1f1f2e 191e2e 1d1f2e 1d1f31 212232 222332 232335 232635 232535 232635',
        ] },
        ellipses: [
          // Soft shadow just under the board's bottom edge (a row of blurred dark ellipses following the edge).
          [0.3, 0.875, 0.16, 0.05, '#00000099', 0.03], [0.5, 0.83, 0.16, 0.05, '#00000099', 0.03],
          [0.7, 0.785, 0.16, 0.05, '#00000099', 0.03], [0.88, 0.74, 0.12, 0.045, '#00000080', 0.03]],
        shapes: [[0.78, 0.08, 0.12, 0.5, '#3a5ab8'], [0.9, 0.15, 0.08, 0.4, '#5a7ad8'], [0.02, 0.35, 0.05, 0.3, '#e08a40']],
      },
    }
  },

  checkers: () => {
    // Reference: a chunky physical toy board photographed on a table: thick casing, individually
    // bevelled inset squares, sculpted pucks with slight variation, warm raking light, shallow focus.
    const N = 6
    const ROWS = 5 // the reference's board is a row short at the front and a column short at the right: 5 x 5
    const COLS = 5
    const SQ = 0.53 // square size (pieces keep their size)
    const SQ_A1 = 0.47 // the size the A1 corner was placed for: the board grows away from that corner
    const size = N * SQ
    const depth = ROWS * SQ
    const width = COLS * SQ
    const zc = -(size - depth) / 2 // the field keeps its back edge; the front edge moves in by the missing row
    const xc = -(size - width) / 2 // and its left edge; the right edge moves in by the missing column
    const surface = noiseTexture({ contrast: 0.1, seed: 43 })
    const LIP_W = 0.28 // outer wall thickness (base matches it)
    const casing = mat('#171310', { roughness: 0.12, bumpMap: surface, bumpScale: 0.15, clearcoat: 1, clearcoatRoughness: 0.05, envMapIntensity: 1.2 }) // reflective, polished
    const parts = [
    ]
    // Casing: one smooth solid from the table (y -0.325) up to the squares (y 0.22), swept round the board as
    // a profile: a small flat strip round the squares, the wall sloping down, a rounded roll into the sides,
    // the sides, the base. Every ring of the sweep follows the same rounded outline, so the slope curves round
    // each corner as one smooth surface.
    const lipW = LIP_W
    const STRIP = 0.06 // flat strip around the squares
    const SLANT_DROP = 0.13 // how far the slope falls below the flat strip
    const BASE_Y = -0.325, TOP_Y = 0.22
    const ROLL = 0.07 // radius of the rounded edge where the slope meets the sides
    const CORNER = 0.14 // the slope's contours round a rectangle this far inside the squares' edge: broad corners
    const profile = [] // [offset from the squares' edge, y]
    profile.push([-0.12, TOP_Y - 0.006], [-0.001, TOP_Y - 0.006]) // tucked just under the squares
    for (let k = 0; k <= 4; k++) profile.push([STRIP * k / 4, TOP_Y]) // flat strip
    {
      // Slope: eased off the strip (y = drop * t^2 (2 - t)), then a roll of radius ROLL down to vertical.
      const slopeEnd = lipW - ROLL * 0.75 // leaves room for the roll inside the wall width
      const run = slopeEnd - STRIP
      for (let k = 1; k <= 24; k++) { const t = k / 24; profile.push([STRIP + run * t, TOP_Y - SLANT_DROP * t * t * (2 - t)]) }
      let [o, y] = profile[profile.length - 1]
      const a0 = Math.atan2(-SLANT_DROP / run, 1) // tangent angle at the end of the slope
      const steps = 16
      for (let k = 1; k <= steps; k++) {
        const a = a0 + (-Math.PI / 2 - a0) * (k - 0.5) / steps
        const da = (-Math.PI / 2 - a0) / steps
        o += ROLL * Math.abs(da) * Math.cos(a)
        y += ROLL * Math.abs(da) * Math.sin(a)
        profile.push([o, y])
      }
      for (let k = 1; k <= 6; k++) profile.push([o, y + (BASE_Y + 0.02 - y) * k / 6]) // side
      profile.push([o - 0.02, BASE_Y], [-0.12, BASE_Y]) // base
    }
    const casePos = [], caseUv = [], caseIdx = []
    const ARC = 16 // segments per corner
    const corners = [[1, 1, 0], [-1, 1, Math.PI / 2], [-1, -1, Math.PI], [1, -1, Math.PI * 1.5]]
    const ring = []
    for (const [sx, sz, a0] of corners) for (let k = 0; k <= ARC; k++) ring.push([sx * (width / 2 - CORNER), sz * (depth / 2 - CORNER), a0 + (Math.PI / 2) * k / ARC])
    for (const [o, y] of profile) for (const [cx0, cz0, a] of ring) {
      const r = o + CORNER
      const x = cx0 + r * Math.cos(a), z = cz0 + r * Math.sin(a)
      casePos.push(x, y, z)
      caseUv.push(x / 3 + 0.5, z / 3 + 0.5)
    }
    const RN = ring.length
    for (let p = 0; p < profile.length - 1; p++) for (let k = 0; k < RN; k++) {
      const a = p * RN + k, b = p * RN + (k + 1) % RN, c = (p + 1) * RN + k, d = (p + 1) * RN + (k + 1) % RN
      caseIdx.push(a, b, c, b, d, c)
    }
    const caseGeo = new THREE.BufferGeometry()
    caseGeo.setAttribute('position', new THREE.Float32BufferAttribute(casePos, 3))
    caseGeo.setAttribute('uv', new THREE.Float32BufferAttribute(caseUv, 2))
    caseGeo.setIndex(caseIdx)
    caseGeo.computeVertexNormals()
    parts.push(mesh(caseGeo, casing, [xc, 0, zc]))
    // Individual inset tiles with soft bevels and a little tonal variation.
    const TILE_TOP = 0.22 // flush with the top of the border wall (lip: 0.2 tall at y 0.12)
    const tileGeo = rbox(SQ, 0.24, SQ, 0.004, 2) // squares butt straight against each other: no gaps or bevels between them
    // Every square gets a slight chamfer along all four top edges.
    const CHAMFER = SQ / 32
    const chamferGeo = (() => {
      const g = new THREE.BoxGeometry(SQ, 0.24, SQ, 32, 1, 32)
      const pos = g.attributes.position
      for (let i = 0; i < pos.count; i++) {
        if (pos.getY(i) < 0.119) continue
        if (Math.abs(Math.abs(pos.getX(i)) - SQ / 2) < 1e-6 || Math.abs(Math.abs(pos.getZ(i)) - SQ / 2) < 1e-6) pos.setY(i, pos.getY(i) - CHAMFER)
      }
      g.computeVertexNormals()
      return g
    })()
    const tileGeoFor = () => chamferGeo
    // Tactile, not literally rough: faint roughness and bump noise varies the reflections.
    const grain = noiseTexture({ contrast: 0.18, seed: 41 })
    const at = (c, r) => [(c - (N - 1) / 2) * SQ, 0, (r - (N - 1) / 2) * SQ]
    for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) {
      const dark = (r + c) % 2 === 0 // the reference's colouring: its dark squares carry the pieces
      const [x, , z] = at(c, r)
      const tile = mesh(tileGeoFor(c, r), mat(dark ? '#24150c' : '#b07d4a', { roughness: dark ? 0.45 : 0.6, roughnessMap: grain, bumpMap: grain, bumpScale: 0.8, clearcoat: dark ? 0.25 : 0.05, clearcoatRoughness: 0.3 }), [x, TILE_TOP - 0.12, z])
      parts.push(vary(tile, 100 + r * N + c, { value: 0.015, rough: 0.025, height: 0.02 }))
      tile.rotation.set(0, 0, 0) // tiles stay square; tone, roughness and a hair of height vary
    }
    // Lower, heavier pucks: radius +5%, height -10%.
    const geo = toyPuckGeometry({ radius: 0.185, height: 0.174, flat: true }) // smaller pieces, flat tops
    const red = mat('#a8000c', { roughness: 0.24, clearcoat: 1, clearcoatRoughness: 0.08, envMapIntensity: 0.35, sheen: 0.3, sheenColor: '#c02a2a' }) // darker red
    const black = mat('#1c1917', { roughness: 0.35, clearcoat: 0.6, clearcoatRoughness: 0.25, envMapIntensity: 0.4 })
    const pieces = []
    // c, r may be fractional to sit a piece off-centre (in squares; +c right, -r back). Up to 0.1 square keeps
    // a piece inside its square (piece radius 0.185, half square 0.235).
    const put = (c, r, m) => {
      const [x, , z] = at(c, r)
      const p = vary(mesh(geo, m, [x, TILE_TOP, z]), pieces.length + 1, { rot: 2, rough: 0.025, value: 0.015, scale: 0.0075 })
      pieces.push(p)
      parts.push(p)
    }
    // The reference's position (each piece placed on the square it sits on there).
    ;[[1.9, 0], [3.1, 0], [1, 1], [2, 2], [4.1, 0.9], [4, 2], [3, 3]].forEach(([c, r]) => put(c, r, black))
    ;[[0, 2], [1, 3.1], [2.1, 4], [4, 4], [3, 1], [0, 4]].forEach(([c, r]) => put(c, r, red))
    // The table it sits on: falls out of focus toward the edges.
    // Ends just behind the board so the warm room bokeh shows past its far edge.
    const table = mesh(rbox(12, 0.3, 6.2, 0.1), mat('#4a4a4a', { // darker table (the colour multiplies the wood texture)
      map: woodTexture({ base: '#6a3414', dark: '#5a2a0e', light: '#7a3e1c', seed: 31, size: 1024 }),
      roughness: 0.4, clearcoat: 0.5, clearcoatRoughness: 0.2,
    }), [0, -0.475, 0.6])
    // Square to the axes (the exact camera sets the angle); shifted so the A1 corner of the field (-3 SQ, -3 SQ)
    // stays where it was when the board was smaller, and the board grows toward E5.
    const board = group(parts, [3 * (SQ - SQ_A1), 0, 3 * (SQ - SQ_A1)])
    return {
      object: group([board, table]),
      // Key art, not the player camera: higher, farther, longer lens, the whole board as an object.
      // Sitting over an active game: close, higher, cropped asymmetrically on the middle pieces.
      frame: pieces.slice(3, 15),
      // Exact camera solved so the board's squares land on the reference's (light-square centres, ~6 px rms
      // at 600 px).
      view: { camera: { position: [1.95466, 7.52586, 6.3444], target: [-0.08661, -0.10143, 0.04202], roll: 0.03284, fov: 17.77753 } },
      look: {
        // Backlit: warm light rakes toward the camera across the squares.
        keyFrom: [-2.0, 3.2, -1.2],
        glow: { amount: 0.25, radius: 0.035, tint: ['#ff9a40', 0.1] },
        envIntensity: 0.3, ambient: ['#ffb070', '#3a1a08', 1.3], keyIntensity: 2.3, keyColor: '#ffd8a0', exposure: 1.1,
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
