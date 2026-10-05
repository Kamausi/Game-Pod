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
          '8b79c5 8d77c1 8177c5 9a7bbe 574f94 885883 a66a88 c47d7d e39a7e ba6f7f 8b698d 875e95 57599c 5b5eb3 3f8af8 3facff 3eb4fc 3fb8fd 40bffe 40c4fd 41c8fd 3fc6fe 3ac6fa 3dc6fd 39c8fb 3eccfb 43d1fb 4bd0fa 50d0f9 4ccef5 38bffe ffe6db fff6c3 82b7cd a3cbc1 c1c093 ead58f 36abff 3094fd 268bf3 2272e7 255fd0 7d62ed ead9e4 ffffdf d2afe1 ead6e3 e5cce4',
          '8274c3 8072bd 766fc3 9b78b9 705790 a1727f b88988 d09a7f e9b282 c9897f a0818a a17395 7766a3 6d64b5 4787f9 41a8ff 3eb1fc 3cb5fc 3cbeff 42c3fc 42c9fc 41c4fc 3dc7f8 45c9fc 51ccf7 5dccfa 64d2f7 59d0f9 52cefa 53cff4 3fc0ff ffe4da fff4c2 90bcd3 bad6cf ddcea4 fbd681 29a3ff 3096fd 2b88f0 236ee6 2a5acf 8563ec ebdce2 ffffd9 c8a1de e0c9e1 ddc0e3',
          '967dc0 977ab8 8f79be 8e78b5 123887 42197a 741b7c a43965 ca5969 932c70 502881 422a89 0a2f7b 2a459b 3281f4 40a8ff 42aafa 48adfa 4cb2ff 41b4f9 40bcfc 3eb7ff 3bb4fc 2db0ff 16b1ff 05b8ff 00baff 23c7ff 3ac7fc 2ec4f6 15b2ff ffe7d8 fffbbc 5d9ebb 62a6aa 8b9d4a 88a792 4297d6 0c83ff 287fee 1f6de0 1058c8 5a4fe3 d9c7e3 ffffe1 dbbddf f0e4e1 ecdae3',
          '6d6bc9 7068c1 525dc9 997db9 f6619c ffcf7d ffff82 ffff9d ffff91 fffb86 ffd88d ffc098 ff92da b180d7 4483fc 39a4ff 31b6ff 27baff 1bcaff 3cd8ff 41d9ff 3dd6fa 35dff5 54e3f1 84e9de 99e5cc f5f18a 85d2c7 68c7d8 5cc1dd 64bddc a8c9cd aec9be 7eafb5 72a6a8 77a193 72a2ac 549bce 3792e9 3295fa 226cf0 4057d0 c47aff ffffd9 ffffd4 b47cde d9b2dd d1a5e1',
          '5d6dc8 6069c2 5870c5 535ac4 ff8c8f ffffb5 e6efa7 e8e19b ebe79e eceaad f6efb7 faff9c ffe7b5 ffb7d2 7e8df5 45aaf9 97e0ed 9edfd2 c4edb6 7ddbd9 67d6e3 5ed4e4 5dd6e1 6bd8db 80dacf 93d9c2 a9d9b0 8dcec0 7bc6cb 74c1cf 7abece 8fbfc7 93bbbf 85b1b8 7daab2 7aa6ae 74a4bb 66a2cf 4e9fe4 0092ff 086de4 5360e0 ac7dff f799d5 ffcbdb 4768eb 8d95e7 7e8de9',
          '5160c1 575fbd 3e5cbc 3547c1 ff87a6 fff29c fde5ab feef9b f5f6af fbfad1 e5dfb4 e1e0ac dfd2b6 cebdc6 9eafd6 85b8db 93cbd7 9ad2cd 9ed7c5 88d4cf 78d1d6 70d0d7 70d0d5 77d1d1 82d1ca 8cd0c2 93cebc 8bc8c0 83c3c4 7fbec7 82bbc6 88b9c3 8ab5be 86afbb 82abb9 80a8ba 7fa8c2 82abcf 95b7db ede8e7 3057f7 4a4fd3 5ba2ee 3296fa 629ffa 3168df 4980e7 457de8',
          '5366c3 4a5fbd 5261b5 6a69b1 b18ba7 cfb7a2 d8c5a4 dccfa2 dcd5aa dcd6b3 d8d3b2 d3d0b1 ccc9b7 bebfc0 a8b9c8 99bccd 98c4cc 97c9c9 95ccc6 8bccca 82cbcc 7ccbcd 7bcacc 7ecac9 83cac5 89c8c1 8cc6be 89c2bf 86bec0 84bac1 85b7c1 87b4bf 87b1bd 86adbc 84a9bc 84a8bf 86a8c5 8daacf 9db0d8 b7b5e2 aa93ee 4d60e4 4ba1ec 2090e2 3895e4 6865e4 557be6 5b7ae8',
          '5463c5 4858c0 5a64b4 7574ae 9b8ba8 b4a4a4 c2b3a4 c9bea5 ccc4a8 cdc7ad cbc7af c6c5b1 c0c2b5 b6bdba aabbc0 a0bcc3 9bbfc4 97c2c3 93c5c3 8dc5c4 87c5c5 83c5c6 82c4c5 83c4c3 85c3c1 88c1be 89bfbc 88bcbc 87b9bd 86b6bd 86b3bd 87b0bc 87adbc 86a9bc 86a7bd 86a5c1 89a5c6 8fa7ce 9babd7 adb3e3 ccc8fc 527af9 387ee9 2797e1 449aee 6968e3 577ee6 607be8',
          '435fce 2b52d7 5967b3 7677aa 9088a5 a49aa3 b2a7a3 bab1a4 bfb7a6 c0bba9 bfbdac bdbdae b8bbb1 b1bab5 a9b9b9 a2b9bb 9cbbbd 98bdbd 94bebe 8fbfbe 8bbfbf 88bfbf 87bebf 86bebe 87bdbc 88bbbb 89b9b9 88b7b9 88b4b9 87b1b9 87aeb9 87abb9 87a9ba 86a6bb 86a3bc 87a1c0 88a0c4 8ca0cb 91a2d3 98a7dc a9bfee 6370fa 23368f 243a93 5c83ff 5f74dc 5b77e8 5d7ae8',
          '576fac 4c6499 666e9f 7a79a1 8c86a1 9c93a1 a79ea1 afa7a2 b4ada4 b6b2a6 b6b4a8 b5b5ab b1b5ae acb5b0 a7b5b3 a1b6b5 9db7b7 99b8b7 95b9b8 91b9b9 8eb9b9 8bb9b9 8ab9b9 89b8b8 89b7b7 89b5b7 89b4b6 89b2b6 88afb5 88adb6 88aab6 87a7b6 87a5b7 86a2b8 869fba 869dbd 869bc1 8698c6 8696cd 8492d4 7c88dd 6766ed 295ec3 19347c 4650e5 4481db 4471dd 4478df',
          '77749d 716f93 757398 7f7b9b 8b849d 968e9d a0979e a79fa0 aca5a1 afaaa3 afada5 afafa7 acb0a9 a9b0ac a5b1ae a1b2b0 9db2b1 99b3b2 96b4b3 93b4b3 90b4b4 8eb4b4 8cb3b4 8bb3b3 8bb2b3 8ab0b2 8aafb2 8aadb2 89aab2 89a8b2 88a6b2 88a3b3 87a0b4 879eb5 869bb7 8598ba 8395bd 8191c1 7e8cc6 7784cc 6e77d2 6a69dc 486cd5 2753b1 3f65ce 3268b4 3965c0 3767c2',
          'ac7b8a 86738f 7f7594 837a97 8a8299 938a9a 9b929b a1999c a69f9e a9a3a0 aaa7a1 aaa9a3 a8aba5 a6aca7 a3aca9 a0adaa 9caeac 99aead 96afad 94afae 91afae 8fafaf 8eaeaf 8dadae 8cacae 8babae 8baaae 8aa8ae 8aa6ae 89a4ae 89a1ae 889faf 889cb0 879ab1 8597b3 8493b5 818fb8 7e8abb 7884bf 6e7ac2 5b6bc4 3256ca 3f56b5 3d5bb6 7155d9 1a4882 384da7 2f4da2',
          '98758a 82708e 7e7392 827894 887f96 908797 978e98 9d9499 a29a9b a59e9c a6a29e a6a49f a5a6a1 a4a7a3 a1a8a4 9fa9a6 9caaa7 99aaa8 97aaa8 94aaa9 92aaa9 90aaaa 8fa9aa 8ea9aa 8da8aa 8ca6a9 8ca5a9 8ba3a9 8ba2aa 8aa0aa 8a9daa 899bab 8899ac 8796ad 8693ae 848fb0 818bb3 7d86b5 767fb7 6c76b9 5e6aba 4b5bb9 4355b6 515bbd 454dbf 063453 234180 173f7b',
          '726d97 706a92 766e92 7d7492 867c92 8e8393 958a95 9b9196 9f9697 a29a99 a39d9a a4a09c a3a29d a2a39f a0a4a0 9ea5a1 9ca5a2 99a6a3 97a6a4 95a6a4 93a6a5 91a5a5 90a5a5 8fa4a5 8ea3a5 8da2a5 8ca1a5 8c9fa5 8b9da5 8b9ca6 8a9aa6 8a97a7 8995a7 8892a8 878faa 858cab 8288ad 7d83af 787cb0 6f74b2 656bb3 5a60b4 5659c0 4565b1 005cb6 26406a 11478d 194688',
          '5c5ea3 5c5e95 6b6691 786f8f 84788f 8d8190 948891 9a8e92 9e9394 a19795 a29a96 a29c98 a29e99 a19f9a 9fa09b 9da19d 9ba19d 99a29e 97a29f 95a29f 93a1a0 92a1a0 90a0a0 8f9fa0 8e9fa0 8e9da0 8d9ca1 8d9ba1 8c99a1 8c98a1 8b96a2 8b94a2 8a92a3 898fa4 888da5 8689a6 8486a7 8081a8 7b7baa 7575ab 6d6dac 6465ae 595db0 4459b5 0033ac af7587 6d629f 85689c',
          '5957a0 3e4a9c 605d8c 756b8b 84768b 8e7f8c 95868e 9b8c8f 9e9190 a09591 a19793 a19994 a19b95 a09c96 9e9d97 9d9d98 9b9e99 999e9a 979e9a 959d9b 939d9b 929d9b 919c9c 909b9c 8f9a9c 8e999c 8e989c 8d979c 8d959c 8d949d 8c929d 8c919d 8b8f9e 8b8d9f 8a8a9f 8987a0 8784a1 8480a2 817ca3 7c76a4 7670a5 6f69a6 6663ab 6365bf 1e3eb5 8f615d 605281 72597c',
          '584d84 4d447e 655982 796885 887586 927f88 99868a 9d8c8b a0908d a1938e a2958f a19790 a19891 9f9992 9e9a93 9c9a94 9a9a95 989a95 979a96 959996 949997 929897 919897 909797 8f9697 8f9597 8e9498 8e9398 8e9298 8d9098 8d8f98 8d8d99 8d8c99 8d8a9a 8c889a 8c869b 8b839b 89809c 877d9c 85789c 81739c 7b6c9c 70619c 4c48a6 3445a5 4d4057 424379 494474',
          '65518b 4e4683 6c587d 82697f 907782 998184 9e8886 a18d88 a39089 a3928a a3948c a2958d a1968d 9f968e 9e978f 9c9790 9a9690 989691 969691 959592 939592 929492 919493 909393 8f9293 8f9193 8f9093 8e8f93 8e8e93 8e8d93 8e8c94 8e8a94 8f8994 8f8895 8f8695 8f8595 8f8396 8f8196 8f7e96 8e7b95 8e7894 8c7392 896b8d 805e85 635379 5c3b68 5f4271 674572',
          '604d7c 494379 7c5675 936d7a 9e7c7e a38681 a68b83 a78f85 a79186 a69387 a59388 a39489 a1948a 9f948a 9d948b 9b938c 99938c 98938d 96928d 94928d 93918e 92908e 91908e 908f8e 8f8e8e 8f8d8e 8f8c8e 8f8b8e 8f8a8f 8f898f 8f888f 90878f 90868f 91858f 928490 928390 938390 948290 968190 987f8f 9b7e8d a07c8a a97984 be7278 ff6562 ff9d70 ff8168 ff906f',
          'c96a73 cc6d72 ae516b b17974 b2887b b18e7e af9181 ad9382 ab9383 a99384 a79385 a49385 a29286 9f9187 9d9187 9b9088 999088 978f89 958e89 948e89 938d89 918c8a 908c8a 908b8a 8f8a8a 8f898a 8f898a 8f888a 8f878a 8f868a 90858a 91848a 92848a 93838a 94838a 96828b 97828b 9a828a 9d838a a18489 a68587 af8785 bd8b80 d49379 ffae70 fdff92 f9e484 fdf18d',
          'ffb376 ffb073 ffb671 d79f76 c89c7b bf9a7d b9997f b49880 b09680 ac9481 a99381 a59182 a29082 9f8f83 9d8e83 9a8d84 988c84 968c84 958b85 938a85 928985 918985 908885 8f8785 8f8685 8f8685 8f8585 8f8485 8f8385 908385 918285 928185 938185 958185 968185 998185 9b8285 9e8385 a38585 a88784 af8b83 b99181 c69a7f d8a97e edc681 f4ff9b efe689 f4f394',
          'fec881 fdc57f ffc87e e6b67b d6ae7d cba77d c2a17d ba9c7e b4987e af957e aa927e a6907e a38e7f 9f8d7f 9c8b7f 9a8a80 978980 958880 948781 928681 918681 908581 8f8481 8f8381 8e8281 8e8281 8e8181 8f8080 8f8080 907f80 917f80 927e80 947e80 967e80 987f80 9b8080 9e8180 a28380 a78680 ad8a80 b58f7f bf977e cba27d dab27e eacb81 fcf991 f5ea8c f8f091',
          'fab97a fab779 fcaa72 efc67f e0be7f d3b27e c9a87c c0a07b b89a7b b1957b ac927b a78e7b a28c7b 9f8a7b 9c887c 99877c 96867c 94847c 92847d 91837d 90827d 8f817d 8e807d 8e7f7d 8d7f7d 8e7e7c 8e7d7c 8e7c7c 8f7c7c 907b7b 917b7b 937b7b 957b7b 977b7b 9a7c7b 9d7e7b a17f7b a5827b ab867b b18a7b b9917b c3997b cea57b dbb37b e9c37a ffdc7e fdd982 fed97f',
          'fdba77 fcc279 fc9b68 ffff92 e9d283 dabc7e cead7b c3a379 ba9b78 b39577 ac9077 a78c77 a28977 9e8777 9a8578 978378 958278 938178 918079 8f7f79 8e7e79 8d7d79 8d7c79 8d7b78 8c7b78 8d7a78 8d7977 8e7977 8f7877 907876 917776 937776 957876 987876 9b7976 9e7b76 a27d76 a78076 ad8477 b48a77 bc9177 c59978 cfa478 dab179 e5c179 f0d17a fbec87 fce988',
          'feb56d fdc071 fc955d ffed87 eed280 debe7c d1ae78 c5a276 bb9974 b39374 ac8d73 a68973 a08673 9c8374 998174 958074 937e74 917d75 8f7c75 8e7b75 8d7a75 8c7975 8b7875 8b7774 8b7774 8b7673 8c7573 8d7572 8e7472 8f7471 917371 937371 957471 987570 9b7671 9f7871 a37a71 a87e72 ae8272 b58773 bd8e74 c59774 cfa175 d9ae76 e3bd78 eccf7b faf68e f6eb8b',
          'fb9864 fba56b f87053 ffe585 f0ce7d e1bb78 d3ab74 c69f72 bb9670 b28f70 aa896f a4856f 9e826f 9a7f70 967d70 937c70 917a71 8f7971 8d7871 8c7771 8b7671 8a7571 8a7470 897370 8a7370 8a726f 8b716e 8c706e 8d706d 8e6f6c 906f6c 926f6b 95706b 98706b 9b726b 9f746b a4766c a97a6d af7e6e b6846f bd8b70 c59371 ce9c72 d7a773 e1b575 eac577 fcef8b faf18e',
          'ef845f f09263 e8584e ffd17b f4ca7a e3b573 d3a56f c5996d b9906b af8a6b a7846b a1816b 9b7d6b 977b6c 93796c 90776c 8e766d 8c756d 8a746d 89736d 88726d 88716d 88706d 886f6c 886f6b 886e6b 896d6a 8a6c69 8b6c68 8d6b67 8f6b67 916b66 946b66 976c65 9a6d65 9e6f66 a37267 a87568 af7969 b57f6a bd866c c58d6e cd966f d69f70 deaa71 e7b571 f1c273 ffde82',
          'f59865 f7a56c ef7654 ffdd88 ffd981 e4ab6c d09a68 c19066 b58866 ab8366 a37e66 9c7b67 977867 937668 907468 8d7369 8b7269 897169 87706a 866f6a 866e69 856d69 856c69 856b68 866b67 866a67 876965 886864 8a6763 8c6762 8d6661 906660 926660 96665f 996860 9d6960 a26c61 a76f62 ae7464 b47966 bc7f68 c4876a cc8e6c d4966d dc9e6d e4a36b eca465 fea559',
          'ffa061 ffa661 ff8756 ffc871 ff8c49 de8b58 ca885d bb835f ae7f60 a57b61 9d7762 977562 927363 8e7164 8b6f64 896e65 876d66 856c66 846c66 836b66 836a66 836966 836865 836764 836763 846662 856561 866460 88635e 8a625d 8c615c 8e615a 91605a 946159 976159 9b635a a0655b a5695d ac6d5f b37261 bb7864 c37f67 cb876a d38e6b da946b df9768 e49662 e8915c',
          'a04a62 9f4c5f 8b3d64 cc5f5a aa3c40 cc7250 c07956 b17759 a5745a 9c725c 96705d 906e5e 8c6c5f 896b60 866a61 846961 836962 816862 816763 806763 806663 806562 806462 806461 816360 82625e 83605d 845f5b 865e59 885d58 8a5c56 8c5b54 8f5a53 925a52 955b52 995c53 9d5e54 a36156 a96559 b16a5d b97161 c27865 cb7f69 d3876c d98d6b dc8e67 dd8c60 dd895c',
          'c36c53 bf6b50 bf694f b56757 e37c49 f08951 b77252 a46c53 996a55 926857 8d6758 89675a 85665b 83655c 81655d 7f645e 7e645f 7d635f 7d6360 7c6260 7c6260 7c615f 7d615f 7d605e 7e5f5c 7f5e5b 805c59 825b57 835954 855852 875650 8a554e 8c544c 8f534b 92534b 96544b 9a564d 9f594f a65d53 ad6257 b6685d c16f63 cb7869 d5826f db8a70 db8a68 d5825d d37e58',
          'ae5b45 a55744 b25e45 924c41 a55444 a65f4b 935c4d 8e5d4e 8a5e50 865f52 835f54 805f55 7e5f57 7c5f59 7b5f5a 7a5f5b 795f5c 795f5c 785f5d 785e5d 795e5d 795d5c 7a5d5c 7a5c5b 7b5b59 7c5a57 7e5855 7f5752 815550 83534d 85514a 874f47 8a4d45 8c4c43 8f4b42 924b42 954c44 9a4f47 a1534c a95852 b35e58 bf6560 cd6f6a da7e77 e2907d e28d6d ca7353 c6714e',
          '442f46 3b2d44 493245 26283e 2e2b40 2a244e 674548 774f48 7a534a 79564d 78574f 775851 765953 755a55 755a57 745a58 745b59 745b59 745b5a 745b5a 755a5a 755a5a 765959 775858 785756 795654 7b5452 7c534e 7e504b 804e47 824b43 854940 87463c 89433a 8b4138 8d4138 90413a 94443e 9a4744 a24d4b ae5351 bc595b ce6269 e67785 f1aba0 ffa875 a74b2c b96740',
          '623c5f 5e3c5e 5b3d5d 5e395d 653b57 663b4b 6a423f 6d4641 6e4a45 6e4d48 6e4f4b 6e514e 6e5350 6e5452 6e5554 6e5655 6f5656 6f5657 705758 705758 715758 725658 735657 745556 755454 765352 77514f 794f4b 7a4c46 7c4941 7f463c 824338 863f34 873a30 87372d 87352d 88352e 8e3833 913a3a 974245 a64848 b64951 e04e68 ff56ae ffffff fff564 ffe790 ffe182',
          '785170 77526f 6b4c65 91618b 7f493d 824932 7a402b 643f38 62423f 624544 634847 644b4a 654d4d 664e4f 675051 685152 695254 6a5255 6b5355 6c5356 6d5356 6e5356 6f5355 705254 725152 735050 744e4c 764b48 774842 78443c 7a4035 813e30 86392b 863124 842a20 80271f 7b261f 892c26 86292b 893a54 a84942 ba3e54 811f51 f11553 fffff7 ffc45d ffbe80 ff9f5e',
          '5f2f4c 5f304a 592d4d 532a3e 532b2c 653833 5c2f2b 523435 53393b 563d40 584144 5b4447 5d474a 5f494c 614b4e 624c50 644e51 654f53 664f54 685054 695055 6a5055 6b5054 6d4f53 6e4e52 704d4f 714b4b 734946 73453f 713f37 723a2d 803a28 8c3723 862616 831911 79160e 5c0f0a 9f271d 7d0101 860000 f53d00 ff762e ff9b4b ffff97 ffff9c ffa97c ffb480 ff9166',
          'a46a3a 9d6435 a06c40 b47137 b97442 613433 3f2332 3e2a34 443139 49363e 4e3b42 523f45 554248 58444a 5a474c 5d484e 5e4a50 604b51 624c52 634d53 654d53 664d54 674d54 694d53 6b4c52 6d4b4f 6f4a4b 714745 71443e 693b33 5d2d20 83391f a24222 861000 8b0000 840000 e74b11 ffa94d ffff85 ffffc4 ffffff ffffda ffff99 ffff96 fff192 ff936c ff9f6c e4865a',
          'ce8953 be7c4d eaa05f 723e29 362130 211c2f 261a28 2a2130 342936 3d303c 443540 493a43 4e3d46 514048 54434a 57454c 59464e 5b484f 5d4951 5f4a52 604a52 624b53 634b54 654b54 674a53 694a51 6d494d 714744 784842 643b37 070200 89390d fb8245 ffcc7b ffff9f ffffa9 ffffa1 ffff94 ffd67a ff955c e26547 9e3b34 933b44 e27a63 f49968 d28767 c58960 b67f59',
          '4a2f31 3e252f 5d3d34 1f1834 2e1f2d 31202b 37212e 161329 282335 332b3b 3b313f 423642 473944 4b3c47 4f3f49 52414b 55434c 57454e 59464f 5b4751 5c4852 5e4853 5f4954 604955 624956 654854 6a4a54 673c34 98594d 9c5e67 ac6e7d a57d87 a07482 9a5b6f 9c515b 9d5151 e27c6f cc7059 a4634e 835f50 78594a 5f4a48 4e413b 443439 3a2e33 604443 543b3e 5c3d3d',
          '3c2d3a 382c3a 3b2f3a 402936 251d2b 291b2a 1f1828 1e192b 242236 2d293b 352e3e 3b3241 413643 463946 4a3c48 4d3e49 50404b 53424d 55434e 574450 594551 5a4652 5b4754 5c4757 47384b 4d3744 5f4969 625071 443253 2d293d 392e4c 3e2c43 302530 2c1f32 171222 121322 3f3650 363045 563f38 5e4039 523137 452e2e 452a31 463333 443336 5b3e42 4b383b 513a3b',
          '3b2b36 3c2b36 402e38 302635 251c2c 261d2c 201826 1f1a31 22223a 28273d 2f2c3f 363040 3c3342 413644 453946 493b48 4c3d4a 4f3f4c 51414d 53424f 554350 564452 574454 584556 594559 5a455a 5c4862 584662 4b3b57 40354d 3e324c 3b2f46 35293d 2e2438 271f32 272134 332b3f 372c3d 3d2e35 3c2729 3f2e2c 362528 3a2a2f 362a33 3f2b39 3e3335 3e3136 403137',
          '332b36 322b36 362e37 342634 2c2231 282337 211d2b 1a1d38 1c2340 23263f 2a2a3f 312d40 373142 3c3443 413745 453947 483b49 4b3d4b 4e3f4c 50404e 52414f 534251 544252 554354 554356 554258 54425a 504059 493a53 42364e 3e324a 3a2f45 352b3f 31273b 2d2438 2c2537 302739 322837 322832 2e2527 37282c 37282e 362b34 392b33 362f36 3c2f3c 3a2f3a 3a2f3a',
          '292433 2a2533 2b2534 202130 1e202e 1e1e30 151931 161b2d 0e1f43 1d2540 26283e 2d2b3f 332f40 393242 3e3544 423746 453948 483b4a 4b3d4b 4d3e4d 4f3f4e 504050 514051 514052 514054 514054 4f3f55 4c3c54 473950 42354c 3e3248 3a2f44 362c40 32293c 2f273a 2e2638 2e2637 2e2635 2e2430 2f2028 35292d 322830 322732 302836 332737 352d3b 342c3a 342c3a',
          '202133 212232 222233 232031 201d30 161b2c 15182b 16192a 0f1830 1b2139 23253b 2a293d 312d3f 363041 3b3343 3f3545 433847 463949 483b4a 4b3c4c 4c3d4d 4e3e4f 4e3e50 4f3f51 4e3e52 4d3e52 4c3c52 493a50 45384e 42354b 3e3248 3a2f44 362c41 332a3d 30283b 2e2738 2c2636 2a2434 282331 26232e 28212f 292230 2b2432 292532 2b2736 31293a 302839 302839',
          '1e1f32 202032 1f2032 1d2031 181c2e 15182b 16192a 15172c 0e162a 1a1f34 222338 28273b 2e2b3e 342e40 393142 3d3444 413646 443848 463a4a 493b4b 4a3c4d 4c3d4e 4c3d4f 4c3d4f 4c3d50 4b3c50 493b4f 47394e 44374c 41344a 3d3247 3a2f44 362d41 332a3e 30283b 2d2738 2a2536 272334 232131 1d1d2b 20202f 22202f 222432 212132 282435 282a39 272838 272838',
          '1a1d31 1b1d31 1c1d31 1a1c30 171c30 171a2c 16172c 1a1a2c 14162a 1a1d32 212236 272639 2d2a3c 322d3f 373041 3b3344 3f3546 423747 453949 473a4b 493b4c 4a3c4d 4b3c4e 4b3c4e 4a3c4f 493b4f 483a4e 46384d 43364b 403449 3d3246 392f43 362d41 332b3e 30293b 2c2739 292536 252333 202031 181c2e 1c1f2e 1c1f31 212232 242332 202335 232635 232535 222535',
          '1b1d31 1c1d30 1d1e31 1a1d30 171c30 171a2b 16172b 17172a 111426 191c2f 202134 262538 2c293b 312c3e 362f41 3a3243 3e3445 413647 443849 46394a 483a4b 493b4c 4a3b4d 4a3b4e 493b4e 483a4e 47394d 45374c 42364a 403448 3c3146 392f43 362d41 332b3e 2f293b 2c2739 282536 242333 1f2031 1b1d2e 1d1f2e 1d1f31 222232 232232 232335 252735 242535 242635',
          '1a1d30 1b1d30 1b1d31 191c30 171c30 16192b 15172b 17182a 0f1326 191c2f 1f2033 262537 2b293b 312c3e 362f41 3a3243 3e3445 413647 433849 46394a 473a4b 483b4c 493b4d 493b4d 493b4e 483a4d 46394d 44374b 42354a 3f3348 3c3146 392f43 362d40 332b3e 2f293b 2c2739 282536 232233 1e2031 191d2e 1d1f2e 1d1f31 212232 222232 232335 232635 232635 232635',
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
