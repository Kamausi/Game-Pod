// The "photo studio" used to bake each 3D model into a transparent image (see scripts/bake-art.mjs).
// HDR environment for reflections, a key light casting soft shadows onto an invisible floor,
// a fresnel rim glow layered onto the models' own materials, and a bloom pass that keeps alpha.
import * as THREE from 'three'
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js'
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js'
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js'
import { GTAOPass } from 'three/examples/jsm/postprocessing/GTAOPass.js'
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js'
import { BokehPass } from 'three/examples/jsm/postprocessing/BokehPass.js'
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js'
import { MODELS } from './models.js'

const FOV = 30
let renderer = null
let envMap = null

export async function setupStudio(hdrUrl) {
  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true })
  renderer.setClearColor(0x000000, 0)
  renderer.shadowMap.enabled = true
  renderer.shadowMap.type = THREE.PCFSoftShadowMap
  const hdr = await new HDRLoader().loadAsync(hdrUrl)
  hdr.mapping = THREE.EquirectangularReflectionMapping
  const pmrem = new THREE.PMREMGenerator(renderer)
  envMap = pmrem.fromEquirectangular(hdr).texture
  pmrem.dispose()
  hdr.dispose()
}

/**
 * Adds a view-dependent rim glow (fresnel) on top of a standard/physical material, so silhouettes
 * pick up a colored edge light without replacing the material's own shading.
 */
export function addRim(material, { color = '#9fd4ff', intensity = 0.35, power = 3 } = {}) {
  if (material.userData.rim) return
  material.userData.rim = true
  // Optional painted shading (material.userData.faceTint): multiplies the final colour of faces by the
  // direction they face in the mesh's own space, for matching hand-lit key art face by face.
  // { left, front, side, inner, innerRight, innerBack: [r, g, b] multipliers, outer: [halfX, halfZ] } - left/front apply only outside
  // the outer half-extents (outer walls), inner only inside them (inside walls; innerRight / innerBack for the
  // inside faces of the right / back walls), side to every vertical face.
  const tint = material.userData.faceTint
  const v3 = (a) => new THREE.Vector3(...(a ?? [1, 1, 1]))
  material.onBeforeCompile = (shader) => {
    shader.uniforms.uRimColor = { value: new THREE.Color(color) }
    shader.uniforms.uRimIntensity = { value: intensity }
    shader.uniforms.uRimPower = { value: power }
    shader.fragmentShader = shader.fragmentShader
      .replace('void main() {', 'uniform vec3 uRimColor;\nuniform float uRimIntensity;\nuniform float uRimPower;\nvoid main() {')
      .replace(
        '#include <emissivemap_fragment>',
        `#include <emissivemap_fragment>
        float rimFresnel = pow(1.0 - clamp(dot(normalize(normal), normalize(vViewPosition)), 0.0, 1.0), uRimPower);
        totalEmissiveRadiance += uRimColor * rimFresnel * uRimIntensity;`,
      )
    if (!tint) return
    Object.assign(shader.uniforms, {
      uTintLeft: { value: v3(tint.left) }, uTintFront: { value: v3(tint.front) }, uTintSide: { value: v3(tint.side) }, uTintInner: { value: v3(tint.inner) }, uTintInnerRight: { value: v3(tint.innerRight ?? tint.inner) }, uTintInnerBack: { value: v3(tint.innerBack ?? tint.inner) }, uTintHole: { value: v3(tint.hole) },
      uOuter: { value: new THREE.Vector2(...(tint.outer ?? [0, 0])) },
      uTopBack: { value: v3(tint.topBack) }, uTopRight: { value: v3(tint.topRight) }, uTopFront: { value: v3(tint.topFront) },
      uExtent: { value: new THREE.Vector2(...(tint.extent ?? [1, 1])) },
    })
    shader.vertexShader = shader.vertexShader
      .replace('void main() {', 'varying vec3 vObjN;\nvarying vec3 vObjP;\nvoid main() {')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvObjN = objectNormal;\nvObjP = position;')
    shader.fragmentShader = shader.fragmentShader
      .replace('void main() {', 'varying vec3 vObjN;\nvarying vec3 vObjP;\nuniform vec3 uTintLeft;\nuniform vec3 uTintFront;\nuniform vec3 uTintSide;\nuniform vec3 uTintInner;\nuniform vec3 uTintInnerRight;\nuniform vec3 uTintInnerBack;\nuniform vec3 uTintHole;\nuniform vec2 uOuter;\nuniform vec3 uTopBack;\nuniform vec3 uTopRight;\nuniform vec3 uTopFront;\nuniform vec2 uExtent;\nvoid main() {')
      .replace(
        '#include <opaque_fragment>',
        `vec3 tn = normalize(vObjN);
        float outerX = step(uOuter.x, abs(vObjP.x)), outerZ = step(uOuter.y, abs(vObjP.z));
        float wl = pow(max(0.0, -tn.x), 2.0) * outerX, wf = pow(max(0.0, tn.z), 2.0) * outerZ;
        float ws = 1.0 - abs(tn.y), wi = ws * (1.0 - outerX) * (1.0 - outerZ);
        outgoingLight *= vec3(1.0) + wl * (uTintLeft - 1.0) + wf * (uTintFront - 1.0) + ws * (uTintSide - 1.0) + wi * (mix(mix(uTintInner, uTintInnerRight, pow(max(0.0, -tn.x), 2.0)), uTintInnerBack, pow(max(0.0, tn.z), 2.0)) - 1.0);
        // hole: extra multiplier on faces that lean toward the object's centre (the inside of a ring, its rounded
        // inner lip included), fading out toward flat tops.
        float wh = step(dot(tn.xz, vObjP.xz), -1e-4) * clamp((1.0 - tn.y) * 2.0, 0.0, 1.0);
        outgoingLight *= vec3(1.0) + wh * (uTintHole - 1.0);
        // Top faces: linear ramps toward the back (-z), right (+x) and front (+z) of the object's extent.
        float wt = max(0.0, tn.y);
        float zb = clamp(-vObjP.z / uExtent.y, 0.0, 1.0), zf = clamp(vObjP.z / uExtent.y, 0.0, 1.0), xr = clamp(vObjP.x / uExtent.x, 0.0, 1.0);
        outgoingLight *= vec3(1.0) + wt * (zb * (uTopBack - 1.0) + xr * (uTopRight - 1.0) + zf * (uTopFront - 1.0));
        #include <opaque_fragment>`,
      )
  }
  material.customProgramCacheKey = () => `rim-${color}-${intensity}-${power}-${JSON.stringify(tint ?? null)}`
}

/**
 * Points the camera from the requested angle and frames the projected outline of `targets`
 * (the whole model, or the parts a model lists in `frame`)
 * (not its bounding sphere), so every asset fills the frame by `fill` without clipping.
 */
function fitCamera(targets, aspect, { pitch = 0.5, yaw = -0.3, fill = 0.9, shift = null, roll = 0, fov = FOV, camera: exact = null }) {
  // An exact camera (position, look-at target, roll, fov), e.g. solved to line a model up with its reference art.
  if (exact) {
    // Near/far hug the subject: a far camera with a huge depth range makes close surfaces z-fight.
    const dist = new THREE.Vector3(...exact.position).distanceTo(new THREE.Vector3(...exact.target))
    const camera = new THREE.PerspectiveCamera(exact.fov, aspect, dist * 0.3, dist * 4)
    camera.position.set(...exact.position)
    camera.lookAt(...exact.target)
    if (exact.roll) camera.rotateZ(exact.roll)
    return camera
  }
  const points = []
  ;[].concat(targets).forEach((t) => t.updateMatrixWorld(true))
  ;[].concat(targets).forEach((t) => t.traverse((o) => {
    if (!o.isMesh) return
    const pos = o.geometry.attributes.position
    const step = Math.max(1, Math.floor(pos.count / 400))
    for (let i = 0; i < pos.count; i += step) points.push(new THREE.Vector3().fromBufferAttribute(pos, i).applyMatrix4(o.matrixWorld))
  }))
  const sphere = new THREE.Box3().setFromPoints(points).getBoundingSphere(new THREE.Sphere())
  const dirV = new THREE.Vector3(Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch))
  const camera = new THREE.PerspectiveCamera(fov, aspect, 0.01, sphere.radius * 100)
  const target = sphere.center.clone()
  let dist = sphere.radius / Math.sin(THREE.MathUtils.degToRad(fov / 2))
  for (let i = 0; i < 6; i++) {
    camera.position.copy(target).addScaledVector(dirV, dist)
    camera.lookAt(target)
    camera.updateMatrixWorld()
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity
    for (const p of points) {
      const v = p.clone().project(camera)
      minX = Math.min(minX, v.x); maxX = Math.max(maxX, v.x); minY = Math.min(minY, v.y); maxY = Math.max(maxY, v.y)
    }
    // Recenter on the outline, then scale distance so the larger extent spans `fill` of the frame.
    const right = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 0)
    const up = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 1)
    const halfH = Math.tan(THREE.MathUtils.degToRad(fov / 2)) * dist
    target.addScaledVector(right, ((minX + maxX) / 2) * halfH * aspect).addScaledVector(up, ((minY + maxY) / 2) * halfH)
    dist *= Math.max((maxX - minX) / 2, (maxY - minY) / 2) / fill
    if (globalThis.__BAKE_DEBUG) console.log('fit', i, points.length, minX.toFixed(2), maxX.toFixed(2), minY.toFixed(2), maxY.toFixed(2))
  }
  // Optional placement: shift the subject (in fractions of the frame) and roll the camera.
  camera.position.copy(target).addScaledVector(dirV, dist)
  camera.lookAt(target)
  if (shift || roll) {
    camera.updateMatrixWorld()
    const right = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 0)
    const up = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 1)
    const halfH = Math.tan(THREE.MathUtils.degToRad(fov / 2)) * dist
    const [sx = 0, sy = 0] = shift ?? []
    const offset = right.multiplyScalar(-sx * 2 * halfH * aspect).add(up.multiplyScalar(-sy * 2 * halfH))
    camera.position.add(offset)
    target.add(offset)
    camera.lookAt(target)
    if (roll) camera.rotateZ(roll)
  }
  return camera
}

const TONE = {
  aces: THREE.ACESFilmicToneMapping,
  agx: THREE.AgXToneMapping,
  neutral: THREE.NeutralToneMapping,
}

function aoBlob(radius) {
  const c = document.createElement('canvas')
  c.width = c.height = 256
  const ctx = c.getContext('2d')
  const g = ctx.createRadialGradient(128, 128, 0, 128, 128, 128)
  g.addColorStop(0, 'rgba(0,0,0,0.5)')
  g.addColorStop(0.5, 'rgba(0,0,0,0.22)')
  g.addColorStop(1, 'rgba(0,0,0,0)')
  ctx.fillStyle = g
  ctx.fillRect(0, 0, 256, 256)
  const m = new THREE.Mesh(
    new THREE.PlaneGeometry(radius * 2, radius * 1.6),
    new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(c), transparent: true, depthWrite: false }),
  )
  m.rotation.x = -Math.PI / 2
  return m
}

const boxOf = (list) => {
  const box = new THREE.Box3()
  ;[].concat(list).forEach((o) => box.expandByObject(o))
  return box
}

/** Renders one model at width x height pixels and returns the canvas. */
export function renderModel(name, { width, height, tone = 'aces', exposure = 1, rim = {} }) {
  const build = MODELS[name]
  if (!build) throw new Error(`No model named ${name}`)
  const { object, view = {}, shadow = true, look = {}, backdrop = null, frame = null } = build()

  renderer.toneMapping = TONE[look.tone ?? tone]
  renderer.toneMappingExposure = look.exposure ?? exposure

  const scene = new THREE.Scene()
  scene.environment = envMap
  scene.environmentIntensity = look.envIntensity ?? 1
  // Warm-lit scenes (tavern, sunset) swap the cool studio fill for a coloured ambient bounce.
  if (look.ambient) scene.add(new THREE.HemisphereLight(look.ambient[0], look.ambient[1], look.ambient[2] ?? 1))

  object.traverse((o) => {
    if (!o.isMesh) return
    o.castShadow = shadow
    o.receiveShadow = true
    ;[].concat(o.material).forEach((m) => m.isMeshStandardMaterial && addRim(m, { ...rim, ...look.rim }))
    if (look.envTint) [].concat(o.material).forEach((m) => m.isMeshStandardMaterial && (m.envMapIntensity *= look.envTint))
  })
  scene.add(object)

  const box = new THREE.Box3().setFromObject(object)
  const sphere = box.getBoundingSphere(new THREE.Sphere())
  const r = sphere.radius

  // Key light matches the HDR's main softbox (front-left, above) and casts the soft shadow.
  const key = new THREE.DirectionalLight(look.keyColor ?? '#fff3e6', look.keyIntensity ?? 1.4)
  const [kx, ky, kz] = look.keyFrom ?? [-2.2, 3, 2.4] // in scene radii; negative z = backlight
  key.position.set(sphere.center.x + r * kx, sphere.center.y + r * ky, sphere.center.z + r * kz)
  key.target.position.copy(sphere.center)
  key.castShadow = shadow
  key.shadow.mapSize.set(2048, 2048)
  key.shadow.radius = look.shadowSoftness ?? 6
  key.shadow.blurSamples = 16
  key.shadow.bias = -0.0004
  key.shadow.normalBias = 0.02
  Object.assign(key.shadow.camera, { left: -r * 1.6, right: r * 1.6, top: r * 1.6, bottom: -r * 1.6, near: 0.1, far: r * 10 })
  scene.add(key, key.target)

  // Extra lights for a multi-light rig, positions in scene radii from the subject's centre:
  // { type: 'hemi', sky, ground, intensity } | { type: 'dir' | 'point', color, intensity, from, distance? }
  for (const l of look.lights ?? []) {
    if (l.type === 'hemi') { scene.add(new THREE.HemisphereLight(l.sky, l.ground, l.intensity)); continue }
    const light = l.type === 'point'
      ? new THREE.PointLight(l.color, l.intensity, (l.distance ?? 0) * r, 1.6)
      : new THREE.DirectionalLight(l.color, l.intensity)
    light.position.set(sphere.center.x + r * l.from[0], sphere.center.y + r * l.from[1], sphere.center.z + r * l.from[2])
    if (light.target) { light.target.position.copy(sphere.center); scene.add(light.target) }
    scene.add(light)
  }

  // Softbox: a large rectangular area light above and to one side, so glossy pieces get a broad
  // gradient highlight that describes their curvature instead of a single specular dot.
  if (look.softbox) {
    RectAreaLightUniformsLib.init()
    const { color = '#fff1dc', intensity = 6, width: sw = 1.6, height: sh = 0.9, from = [-1.2, 2.2, 0.8] } = look.softbox
    const box = new THREE.RectAreaLight(color, intensity, r * sw, r * sh)
    box.position.set(sphere.center.x + r * from[0], sphere.center.y + r * from[1], sphere.center.z + r * from[2])
    box.lookAt(sphere.center)
    scene.add(box)
  }

  // Scene tiles (with a painted backdrop) float in front of it, so no floor shadow there.
  if (shadow && !backdrop) {
    const floorY = box.min.y - 0.002
    const catcher = new THREE.Mesh(new THREE.PlaneGeometry(r * 6, r * 6), new THREE.ShadowMaterial({ opacity: 0.32 }))
    catcher.rotation.x = -Math.PI / 2
    catcher.position.set(sphere.center.x, floorY, sphere.center.z)
    catcher.receiveShadow = true
    const ao = aoBlob(r * 0.95)
    ao.position.set(sphere.center.x, floorY + 0.001, sphere.center.z)
    scene.add(catcher, ao)
  }

  const camera = fitCamera(frame ?? object, width / height, view)
  let backdropBehind = null
  renderer.setPixelRatio(1)
  renderer.setSize(width, height, false)

  if (backdrop) {
    // Scene tiles: the painted backdrop becomes the background, and the frame goes through
    // ambient occlusion so pieces sit *in* boards and grass instead of floating on them.
    // backdropUntoned: render the subject over a transparent background and lay the painted backdrop
    // behind it afterwards, so its colours come through exactly instead of being tone mapped.
    const backCanvas = paintBackdrop(width, height, backdrop)
    const bg = new THREE.CanvasTexture(backCanvas)
    bg.colorSpace = THREE.SRGBColorSpace
    if (!look.backdropUntoned) {
      scene.background = bg
      scene.backgroundIntensity = look.backgroundIntensity ?? 1.15
    } else {
      backdropBehind = backCanvas
    }
    const target = new THREE.WebGLRenderTarget(width, height, { type: THREE.HalfFloatType, samples: 4 })
    const composer = new EffectComposer(renderer, target)
    composer.setPixelRatio(1)
    composer.setSize(width, height)
    composer.addPass(new RenderPass(scene, camera))
    const gtao = new GTAOPass(scene, camera, width, height)
    // Small radius: darken contact creases and pockets, not whole flat faces near tall neighbours.
    gtao.updateGtaoMaterial({ radius: r * (look.aoRadius ?? 0.05), distanceExponent: 2, thickness: r * 0.03, scale: 1 })
    gtao.blendIntensity = look.aoIntensity ?? 1.1
    composer.addPass(gtao)
    if (look.dof) {
      // Real depth of field: focus on the framed subject, blur the table/scenery that falls away.
      const focusPoint = boxOf(frame ?? object).getCenter(new THREE.Vector3())
      const bokeh = new BokehPass(scene, camera, {
        focus: camera.position.distanceTo(focusPoint),
        aperture: look.dof.aperture ?? 0.004,
        maxblur: look.dof.maxblur ?? 0.012,
      })
      composer.addPass(bokeh)
    }
    composer.addPass(new OutputPass())
    composer.render()
    composer.dispose()
    gtao.dispose()
    target.dispose()
    bg.dispose()
  } else {
    renderer.render(scene, camera)
  }

  const out = document.createElement('canvas')
  out.width = width
  out.height = height
  if (backdropBehind) out.getContext('2d').drawImage(backdropBehind, 0, 0)
  out.getContext('2d').drawImage(renderer.domElement, 0, 0)

  scene.traverse((o) => {
    if (!o.isMesh) return
    o.geometry.dispose()
    ;[].concat(o.material).forEach((m) => {
      m.map?.dispose()
      m.dispose()
    })
  })
  return out
}

/**
 * Paints a soft, out-of-focus backdrop (the depth-of-field background behind a tile's subject):
 * a base gradient, big blurred color masses (sky, grass, glow) and scattered bokeh discs.
 *   spec = { gradient: [angleDeg, ...colors], masses: [[x, y, r, color]], bokeh: { n, colors, min, max, seed } }
 * Coordinates are fractions of the canvas.
 */
export function paintBackdrop(width, height, spec) {
  const c = document.createElement('canvas')
  c.width = width
  c.height = height
  const ctx = c.getContext('2d')
  const [angle = 160, ...stops] = spec.gradient ?? [160, '#3a2a7a', '#141a4a']
  const a = (angle * Math.PI) / 180
  const g = ctx.createLinearGradient(
    width / 2 - (Math.sin(a) * width) / 2, height / 2 + (Math.cos(a) * height) / 2,
    width / 2 + (Math.sin(a) * width) / 2, height / 2 - (Math.cos(a) * height) / 2,
  )
  stops.forEach((col, i) => g.addColorStop(i / Math.max(1, stops.length - 1), col))
  ctx.fillStyle = g
  ctx.fillRect(0, 0, width, height)

  // Low-detail room forms (cabinets, shelves, lamps, windows) painted then blurred: richer
  // background structure than colour blobs alone. shapes: [[x, y, w, h, color, radius?]] in fractions.
  if (spec.shapes) {
    ctx.filter = `blur(${Math.round(width * (spec.shapeBlur ?? 0.025))}px)`
    for (const [x, y, w, h, color, rad = 0.01] of spec.shapes) {
      ctx.fillStyle = color
      ctx.beginPath()
      ctx.roundRect(x * width, y * height, w * width, h * height, rad * width)
      ctx.fill()
    }
  }
  ctx.filter = `blur(${Math.round(width * 0.06)}px)`
  for (const [x, y, r, color] of spec.masses ?? []) {
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.ellipse(x * width, y * height, r * width, r * height * 0.8, 0, 0, Math.PI * 2)
    ctx.fill()
  }
  const b = spec.bokeh
  if (b) {
    let seed = b.seed ?? 3
    const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647)
    for (let i = 0; i < (b.n ?? 18); i++) {
      const r = (b.min ?? 0.015) + rand() * ((b.max ?? 0.06) - (b.min ?? 0.015))
      const x = rand() * width
      const y = rand() * height * (b.yMax ?? 1)
      ctx.filter = `blur(${Math.round(width * r * 0.35)}px)`
      ctx.globalAlpha = 0.35 + rand() * 0.5
      ctx.fillStyle = b.colors[i % b.colors.length]
      ctx.beginPath()
      ctx.arc(x, y, r * width, 0, Math.PI * 2)
      ctx.fill()
    }
    ctx.globalAlpha = 1
  }
  // Soft colour spots (e.g. tuned to sampled colours of a reference background): [[x, y, r, color]] in
  // fractions of the frame, blurred by spotBlur.
  ctx.filter = `blur(${Math.round(width * (spec.spotBlur ?? 0.05))}px)`
  for (const [x, y, rad, color] of spec.spots ?? []) {
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.arc(x * width, y * height, rad * width, 0, Math.PI * 2)
    ctx.fill()
  }
  // grid: { cols, rows, rows: ['rrggbb rrggbb …', …] } - a low-resolution colour field (e.g. tuned to a reference
  // background) stretched smoothly over the frame and blurred by grid.blur.
  if (spec.grid) {
    const { cols, colors, blur = 0.02 } = spec.grid
    const rows = colors.length
    const g = document.createElement('canvas')
    g.width = cols + 2
    g.height = rows + 2
    const gx = g.getContext('2d')
    const at = (cx, cy) => colors[Math.min(rows - 1, Math.max(0, cy))].split(' ')[Math.min(cols - 1, Math.max(0, cx))]
    for (let y = -1; y <= rows; y++) for (let x = -1; x <= cols; x++) { // one replicated cell of padding all round
      gx.fillStyle = `#${at(x, y)}`
      gx.fillRect(x + 1, y + 1, 1, 1)
    }
    const cw = width / cols, ch = height / rows
    ctx.filter = `blur(${Math.round(width * blur)}px)`
    ctx.imageSmoothingEnabled = true
    ctx.imageSmoothingQuality = 'high'
    ctx.drawImage(g, -cw, -ch, width + 2 * cw, height + 2 * ch)
    ctx.filter = 'none'
  }
  // Defined forms painted over everything else (e.g. a dark stage under the subject):
  // ellipses: [[cx, cy, rx, ry, color, blur?, topColor?]] in fractions of the frame; with topColor the
  // fill shades from topColor near the top of the frame's lower part (y 0.8) down to color (y 0.96).
  // leftColor / rightColor, if given, tint the ellipse toward the frame's left and right edges (fading out
  // over the outer 30% of the width).
  for (const [x, y, rx, ry, color, blur = 0.01, topColor, leftColor, rightColor] of spec.ellipses ?? []) {
    ctx.filter = `blur(${Math.round(width * blur)}px)`
    if (topColor) {
      const g = ctx.createLinearGradient(0, height * 0.8, 0, height * 0.96)
      g.addColorStop(0, topColor)
      g.addColorStop(1, color)
      ctx.fillStyle = g
    } else ctx.fillStyle = color
    ctx.beginPath()
    ctx.ellipse(x * width, y * height, rx * width, ry * height, 0, 0, Math.PI * 2)
    ctx.fill()
    if (leftColor || rightColor) {
      const clear = (hex) => `${hex.slice(0, 7)}00`
      const g = ctx.createLinearGradient(0, 0, width, 0)
      if (leftColor) { g.addColorStop(0, leftColor); g.addColorStop(0.3, clear(leftColor)) }
      if (rightColor) { g.addColorStop(0.7, clear(rightColor)); g.addColorStop(1, rightColor) }
      ctx.fillStyle = g
      ctx.fill()
    }
  }
  // rects: [[x, y, w, h, radius, color, blur?]] in fractions of the frame (radius in widths).
  for (const [x, y, w, h, r, color, blur = 0.01] of spec.rects ?? []) {
    ctx.filter = `blur(${Math.round(width * blur)}px)`
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.roundRect(x * width, y * height, w * width, h * height, r * width)
    ctx.fill()
  }
  // spotsOver: like spots, but painted over the ellipses and rects (e.g. to tune colours inside a dark dome).
  ctx.filter = `blur(${Math.round(width * (spec.spotBlur ?? 0.05))}px)`
  for (const [x, y, rad, color] of spec.spotsOver ?? []) {
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.arc(x * width, y * height, rad * width, 0, Math.PI * 2)
    ctx.fill()
  }
  ctx.filter = 'none'
  // Gentle vignette pulls focus to the subject.
  const v = ctx.createRadialGradient(width / 2, height * 0.45, width * 0.25, width / 2, height * 0.5, width * 0.8)
  v.addColorStop(0, 'rgba(0,0,0,0)')
  v.addColorStop(1, `rgba(0,0,0,${spec.vignette ?? 0.35})`)
  ctx.fillStyle = v
  ctx.fillRect(0, 0, width, height)
  return c
}

export function composite(back, front) {
  const out = document.createElement('canvas')
  out.width = back.width
  out.height = back.height
  const ctx = out.getContext('2d')
  ctx.drawImage(back, 0, 0)
  ctx.drawImage(front, 0, 0)
  return out
}

/** Renders the model and, when it defines a backdrop, composites it into a full-bleed scene. */
export function renderAsset(name, opts) {
  return renderModel(name, opts)
}

/**
 * Bloom that keeps transparency: bright pixels are blurred into a glow layer and added back,
 * and the glow also extends the alpha so highlights halo softly past the silhouette.
 */
export function bloom(canvas, { threshold = 0.82, strength = 0.55, radius = 0.018 } = {}) {
  const { width: w, height: h } = canvas
  const src = canvas.getContext('2d').getImageData(0, 0, w, h)
  const bright = new ImageData(w, h)
  const s = src.data
  const b = bright.data
  for (let i = 0; i < s.length; i += 4) {
    const a = s[i + 3] / 255
    const lum = ((0.2126 * s[i] + 0.7152 * s[i + 1] + 0.0722 * s[i + 2]) / 255) * a
    const k = Math.max(0, (lum - threshold) / (1 - threshold))
    b[i] = s[i]
    b[i + 1] = s[i + 1]
    b[i + 2] = s[i + 2]
    b[i + 3] = Math.round(255 * Math.min(1, k * 1.5))
  }
  const brightCanvas = document.createElement('canvas')
  brightCanvas.width = w
  brightCanvas.height = h
  brightCanvas.getContext('2d').putImageData(bright, 0, 0)

  const out = document.createElement('canvas')
  out.width = w
  out.height = h
  const ctx = out.getContext('2d')
  ctx.drawImage(canvas, 0, 0)
  ctx.globalCompositeOperation = 'lighter'
  ctx.globalAlpha = strength
  for (const f of [0.5, 1, 2.2]) {
    ctx.filter = `blur(${Math.round(w * radius * f)}px)`
    ctx.drawImage(brightCanvas, 0, 0)
  }
  ctx.filter = 'none'
  ctx.globalAlpha = 1
  ctx.globalCompositeOperation = 'source-over'
  return out
}

/**
 * Painterly glow (Orton effect): a heavily blurred, brightened copy screened over the image, plus a
 * warm tint lift. This is the soft luminous haze over the key art references.
 */
export function glow(canvas, { amount = 0.4, radius = 0.025, tint = null } = {}) {
  const { width: w, height: h } = canvas
  const out = document.createElement('canvas')
  out.width = w
  out.height = h
  const ctx = out.getContext('2d')
  ctx.drawImage(canvas, 0, 0)
  ctx.globalCompositeOperation = 'screen'
  ctx.globalAlpha = amount
  // Crush darks before blurring so only highlights bloom; shadows and saturation stay intact.
  ctx.filter = `contrast(1.8) brightness(0.85) blur(${Math.round(w * radius)}px) saturate(1.3)`
  ctx.drawImage(canvas, 0, 0)
  ctx.filter = 'none'
  if (tint) {
    ctx.globalCompositeOperation = 'soft-light'
    ctx.globalAlpha = tint[1]
    ctx.fillStyle = tint[0]
    ctx.fillRect(0, 0, w, h)
  }
  ctx.globalAlpha = 1
  ctx.globalCompositeOperation = 'source-over'
  return out
}
