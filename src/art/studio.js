// The "photo studio" used to bake each 3D model into a transparent image (see scripts/bake-art.mjs).
// HDR environment for reflections, a key light casting soft shadows onto an invisible floor,
// a fresnel rim glow layered onto the models' own materials, and a bloom pass that keeps alpha.
import * as THREE from 'three'
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js'
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
  }
  material.customProgramCacheKey = () => `rim-${color}-${intensity}-${power}`
}

/**
 * Points the camera from the requested angle and frames the model's actual projected outline
 * (not its bounding sphere), so every asset fills the frame by `fill` without clipping.
 */
function fitCamera(object, aspect, { pitch = 0.5, yaw = -0.3, fill = 0.9 }) {
  const points = []
  object.updateMatrixWorld(true)
  object.traverse((o) => {
    if (!o.isMesh) return
    const pos = o.geometry.attributes.position
    const step = Math.max(1, Math.floor(pos.count / 400))
    for (let i = 0; i < pos.count; i += step) points.push(new THREE.Vector3().fromBufferAttribute(pos, i).applyMatrix4(o.matrixWorld))
  })
  const sphere = new THREE.Box3().setFromPoints(points).getBoundingSphere(new THREE.Sphere())
  const dirV = new THREE.Vector3(Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch))
  const camera = new THREE.PerspectiveCamera(FOV, aspect, 0.01, sphere.radius * 100)
  const target = sphere.center.clone()
  let dist = sphere.radius / Math.sin(THREE.MathUtils.degToRad(FOV / 2))
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
    const halfH = Math.tan(THREE.MathUtils.degToRad(FOV / 2)) * dist
    target.addScaledVector(right, ((minX + maxX) / 2) * halfH * aspect).addScaledVector(up, ((minY + maxY) / 2) * halfH)
    dist *= Math.max((maxX - minX) / 2, (maxY - minY) / 2) / fill
  }
  camera.position.copy(target).addScaledVector(dirV, dist)
  camera.lookAt(target)
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

/** Renders one model at width x height pixels and returns the canvas. */
export function renderModel(name, { width, height, tone = 'aces', exposure = 1, rim = {} }) {
  const build = MODELS[name]
  if (!build) throw new Error(`No model named ${name}`)
  const { object, view = {}, shadow = true, look = {} } = build()

  renderer.toneMapping = TONE[look.tone ?? tone]
  renderer.toneMappingExposure = look.exposure ?? exposure

  const scene = new THREE.Scene()
  scene.environment = envMap
  scene.environmentIntensity = look.envIntensity ?? 1

  object.traverse((o) => {
    if (!o.isMesh) return
    o.castShadow = shadow
    o.receiveShadow = true
    ;[].concat(o.material).forEach((m) => m.isMeshStandardMaterial && addRim(m, { ...rim, ...look.rim }))
  })
  scene.add(object)

  const box = new THREE.Box3().setFromObject(object)
  const sphere = box.getBoundingSphere(new THREE.Sphere())
  const r = sphere.radius

  // Key light matches the HDR's main softbox (front-left, above) and casts the soft shadow.
  const key = new THREE.DirectionalLight('#fff3e6', look.keyIntensity ?? 1.4)
  key.position.set(sphere.center.x - r * 2.2, sphere.center.y + r * 3, sphere.center.z + r * 2.4)
  key.target.position.copy(sphere.center)
  key.castShadow = shadow
  key.shadow.mapSize.set(2048, 2048)
  key.shadow.radius = 6
  key.shadow.blurSamples = 16
  key.shadow.bias = -0.0004
  key.shadow.normalBias = 0.02
  Object.assign(key.shadow.camera, { left: -r * 1.6, right: r * 1.6, top: r * 1.6, bottom: -r * 1.6, near: 0.1, far: r * 10 })
  scene.add(key, key.target)

  if (shadow) {
    const floorY = box.min.y - 0.002
    const catcher = new THREE.Mesh(new THREE.PlaneGeometry(r * 6, r * 6), new THREE.ShadowMaterial({ opacity: 0.32 }))
    catcher.rotation.x = -Math.PI / 2
    catcher.position.set(sphere.center.x, floorY, sphere.center.z)
    catcher.receiveShadow = true
    const ao = aoBlob(r * 0.95)
    ao.position.set(sphere.center.x, floorY + 0.001, sphere.center.z)
    scene.add(catcher, ao)
  }

  const camera = fitCamera(object, width / height, view)
  renderer.setPixelRatio(1)
  renderer.setSize(width, height, false)
  renderer.render(scene, camera)

  const out = document.createElement('canvas')
  out.width = width
  out.height = height
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
