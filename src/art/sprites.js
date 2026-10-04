// Renders the 3D models in models.js to transparent PNG images once, on demand, and caches them.
// One offscreen renderer is shared, and renders are queued one per frame so the UI never stalls.
import { useEffect, useSyncExternalStore } from 'react'
import * as THREE from 'three'
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js'
import { MODELS } from './models.js'

const FOV = 30
const cache = new Map() // key -> data URL ('' when rendering failed)
const queued = new Set()
const listeners = new Set()
let renderer = null
let envMap = null
let pumping = false

function setup() {
  if (renderer !== null) return renderer
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true })
    // Neutral keeps the toy colors saturated (ACES washes them out).
    renderer.toneMapping = THREE.NeutralToneMapping
    renderer.toneMappingExposure = 0.95
    renderer.setClearColor(0x000000, 0)
    const pmrem = new THREE.PMREMGenerator(renderer)
    envMap = pmrem.fromScene(new RoomEnvironment(), 0.04).texture
    pmrem.dispose()
  } catch {
    renderer = false // No WebGL: callers fall back to plain gradients.
  }
  return renderer
}

const shadowTexture = (() => {
  let tex = null
  return () => {
    if (tex) return tex
    const c = document.createElement('canvas')
    c.width = c.height = 128
    const ctx = c.getContext('2d')
    const g = ctx.createRadialGradient(64, 64, 0, 64, 64, 64)
    g.addColorStop(0, 'rgba(0,0,0,0.55)')
    g.addColorStop(1, 'rgba(0,0,0,0)')
    ctx.fillStyle = g
    ctx.fillRect(0, 0, 128, 128)
    tex = new THREE.CanvasTexture(c)
    return tex
  }
})()

function dispose(root) {
  root.traverse((o) => {
    if (!o.isMesh) return
    o.geometry.dispose()
    ;[].concat(o.material).forEach((m) => {
      if (m.map && m.map !== shadowTexture()) m.map.dispose()
      m.dispose()
    })
  })
}

function renderModel(name, size, aspect) {
  const r = setup()
  const build = MODELS[name]
  if (!r || !build) return ''
  const { object, view = {}, shadow = true } = build()
  const { pitch = 0.5, yaw = -0.3, fill = 1 } = view

  const scene = new THREE.Scene()
  scene.environment = envMap
  scene.environmentIntensity = 0.4
  scene.add(new THREE.HemisphereLight('#e4ecff', '#2a1f4a', 0.55))
  const key = new THREE.DirectionalLight('#fff4e6', 2.1)
  key.position.set(3, 6, 5)
  const fillLight = new THREE.DirectionalLight('#a9c6ff', 0.45)
  fillLight.position.set(-5, 2, 3)
  const rim = new THREE.DirectionalLight('#bcd6ff', 1.3)
  rim.position.set(-2, 4, -6)
  scene.add(key, fillLight, rim, object)

  const box = new THREE.Box3().setFromObject(object)
  const sphere = box.getBoundingSphere(new THREE.Sphere())
  if (shadow) {
    const s = sphere.radius * 1.7
    const plane = new THREE.Mesh(
      new THREE.PlaneGeometry(s, s * 0.75),
      new THREE.MeshBasicMaterial({ map: shadowTexture(), transparent: true, depthWrite: false }),
    )
    plane.rotation.x = -Math.PI / 2
    plane.position.set(sphere.center.x, box.min.y - 0.01, sphere.center.z)
    scene.add(plane)
  }

  const camera = new THREE.PerspectiveCamera(FOV, aspect, 0.1, 200)
  const dist = sphere.radius / Math.sin(THREE.MathUtils.degToRad(FOV / 2)) / fill
  camera.position.set(
    sphere.center.x + Math.sin(yaw) * Math.cos(pitch) * dist,
    sphere.center.y + Math.sin(pitch) * dist,
    sphere.center.z + Math.cos(yaw) * Math.cos(pitch) * dist,
  )
  camera.lookAt(sphere.center)

  r.setPixelRatio(1)
  r.setSize(Math.round(size * aspect), size, false)
  r.render(scene, camera)
  const url = r.domElement.toDataURL('image/png')
  dispose(scene)
  return url
}

const keyOf = (name, size, aspect) => `${name}|${size}|${aspect}`

async function pump() {
  if (pumping) return
  pumping = true
  // Canvas textures draw text in Fredoka, so wait for the font first.
  try {
    await Promise.all([document.fonts.load('700 64px Fredoka'), document.fonts.load('600 64px Fredoka')])
  } catch {
    // Fall back to the system font.
  }
  while (queued.size) {
    const k = queued.values().next().value
    queued.delete(k)
    const [name, size, aspect] = k.split('|')
    let url = ''
    try {
      url = renderModel(name, Number(size), Number(aspect))
    } catch (err) {
      console.warn(`Could not render ${name}`, err)
    }
    cache.set(k, url)
    listeners.forEach((l) => l())
    await new Promise((res) => requestAnimationFrame(() => res()))
  }
  pumping = false
}

export function requestSprite(name, size = 320, aspect = 1) {
  const k = keyOf(name, size, aspect)
  if (cache.has(k) || queued.has(k)) return
  queued.add(k)
  pump()
}

const subscribe = (l) => {
  listeners.add(l)
  return () => listeners.delete(l)
}

/** Returns the rendered image URL for a model (undefined while rendering, '' if WebGL is unavailable). */
export function useSprite(name, size = 320, aspect = 1, enabled = true) {
  const k = keyOf(name, size, aspect)
  const url = useSyncExternalStore(subscribe, () => cache.get(k))
  useEffect(() => {
    if (enabled) requestSprite(name, size, aspect)
  }, [name, size, aspect, enabled])
  return url
}
