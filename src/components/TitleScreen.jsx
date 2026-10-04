import { useEffect, useRef, useState } from 'react'
import titleArt from '../assets/title.webp'
import './TitleScreen.css'

// Key points in the artwork, as fractions of its width/height.
const POD = { x: 0.5, y: 0.645 }

const prefersReducedMotion = () =>
  typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches

const SPARK_COLORS = ['255,214,120', '120,200,255', '255,255,255', '190,140,255']

function makeParticle(w, h, kind, initial) {
  const color = SPARK_COLORS[Math.floor(Math.random() * SPARK_COLORS.length)]
  if (kind === 'spark') {
    // Rises out of the pod's mouth and fans outward.
    const spread = (Math.random() - 0.5) * 0.5
    return {
      kind,
      color,
      x: (POD.x + spread * 0.9) * w,
      y: POD.y * h + Math.random() * h * 0.03,
      vx: spread * w * 0.08,
      vy: -(0.05 + Math.random() * 0.12) * h,
      size: (0.6 + Math.random() * 1.6) * (w / 400),
      life: initial ? Math.random() * 4 : 0,
      maxLife: 2.5 + Math.random() * 3,
      twinkle: Math.random() * Math.PI * 2,
    }
  }
  // Slow ambient dust/stars drifting across the whole frame.
  return {
    kind,
    color,
    x: Math.random() * w,
    y: Math.random() * h,
    vx: (Math.random() - 0.5) * w * 0.01,
    vy: -(0.005 + Math.random() * 0.02) * h,
    size: (0.4 + Math.random() * 1.2) * (w / 400),
    life: initial ? Math.random() * 8 : 0,
    maxLife: 6 + Math.random() * 6,
    twinkle: Math.random() * Math.PI * 2,
    star: Math.random() < 0.25,
  }
}

function drawStar(ctx, x, y, r) {
  ctx.beginPath()
  ctx.moveTo(x, y - r * 2.4)
  ctx.quadraticCurveTo(x, y, x + r * 2.4, y)
  ctx.quadraticCurveTo(x, y, x, y + r * 2.4)
  ctx.quadraticCurveTo(x, y, x - r * 2.4, y)
  ctx.quadraticCurveTo(x, y, x, y - r * 2.4)
  ctx.fill()
}

function useParticles(canvasRef, enabled) {
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    let w = 0
    let h = 0
    let particles = []
    let raf = 0
    let last = performance.now()

    const resize = () => {
      const dpr = Math.min(window.devicePixelRatio || 1, 2)
      w = canvas.clientWidth
      h = canvas.clientHeight
      canvas.width = Math.round(w * dpr)
      canvas.height = Math.round(h * dpr)
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      const sparks = Math.round(70 * (w / 500))
      const dust = Math.round(50 * (w / 500))
      particles = [
        ...Array.from({ length: sparks }, () => makeParticle(w, h, 'spark', true)),
        ...Array.from({ length: dust }, () => makeParticle(w, h, 'dust', true)),
      ]
    }

    const draw = (dt) => {
      ctx.clearRect(0, 0, w, h)
      ctx.globalCompositeOperation = 'lighter'
      for (let i = 0; i < particles.length; i++) {
        let p = particles[i]
        p.life += dt
        if (p.life > p.maxLife) p = particles[i] = makeParticle(w, h, p.kind, false)
        p.x += p.vx * dt
        p.y += p.vy * dt
        p.twinkle += dt * 4
        const t = p.life / p.maxLife
        const fade = Math.min(1, t * 5) * (1 - t)
        const alpha = fade * (0.55 + 0.45 * Math.sin(p.twinkle))
        if (alpha <= 0.01) continue

        const glow = p.size * 4
        const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, glow)
        g.addColorStop(0, `rgba(${p.color},${alpha})`)
        g.addColorStop(1, `rgba(${p.color},0)`)
        ctx.fillStyle = g
        ctx.fillRect(p.x - glow, p.y - glow, glow * 2, glow * 2)

        ctx.fillStyle = `rgba(255,255,255,${alpha})`
        if (p.star) drawStar(ctx, p.x, p.y, p.size)
        else {
          ctx.beginPath()
          ctx.arc(p.x, p.y, p.size * 0.6, 0, Math.PI * 2)
          ctx.fill()
        }
      }
    }

    const loop = (now) => {
      const dt = Math.min(0.05, (now - last) / 1000)
      last = now
      draw(dt)
      raf = requestAnimationFrame(loop)
    }

    const onVisibility = () => {
      cancelAnimationFrame(raf)
      if (!document.hidden && enabled) {
        last = performance.now()
        raf = requestAnimationFrame(loop)
      }
    }

    resize()
    if (enabled) raf = requestAnimationFrame(loop)
    else draw(0) // Reduced motion: one still frame of sparkles.

    const observer = new ResizeObserver(() => {
      resize()
      if (!enabled) draw(0)
    })
    observer.observe(canvas)
    document.addEventListener('visibilitychange', onVisibility)
    return () => {
      cancelAnimationFrame(raf)
      observer.disconnect()
      document.removeEventListener('visibilitychange', onVisibility)
    }
  }, [canvasRef, enabled])
}

// Eases pointer / device tilt into --px and --py (each -1..1) for layered parallax.
function useParallax(ref, enabled) {
  useEffect(() => {
    const el = ref.current
    if (!el || !enabled) return
    const target = { x: 0, y: 0 }
    const current = { x: 0, y: 0 }
    let raf = 0

    const onPointer = (e) => {
      target.x = (e.clientX / window.innerWidth) * 2 - 1
      target.y = (e.clientY / window.innerHeight) * 2 - 1
    }
    const onTilt = (e) => {
      if (e.gamma == null || e.beta == null) return
      target.x = Math.max(-1, Math.min(1, e.gamma / 25))
      target.y = Math.max(-1, Math.min(1, (e.beta - 45) / 25))
    }
    const loop = () => {
      current.x += (target.x - current.x) * 0.06
      current.y += (target.y - current.y) * 0.06
      el.style.setProperty('--px', current.x.toFixed(4))
      el.style.setProperty('--py', current.y.toFixed(4))
      raf = requestAnimationFrame(loop)
    }

    window.addEventListener('pointermove', onPointer)
    window.addEventListener('deviceorientation', onTilt)
    raf = requestAnimationFrame(loop)
    return () => {
      cancelAnimationFrame(raf)
      window.removeEventListener('pointermove', onPointer)
      window.removeEventListener('deviceorientation', onTilt)
    }
  }, [ref, enabled])
}

export default function TitleScreen({ onStart, reducedMotion: reducedSetting }) {
  const rootRef = useRef(null)
  const canvasRef = useRef(null)
  const reducedMotion = reducedSetting ?? prefersReducedMotion()
  const [launching, setLaunching] = useState(false)
  const launched = useRef(false)

  useParticles(canvasRef, !reducedMotion)
  useParallax(rootRef, !reducedMotion)

  const start = () => {
    if (launched.current) return
    launched.current = true
    setLaunching(true)
    setTimeout(onStart, reducedMotion ? 250 : 950)
  }

  useEffect(() => {
    const onKey = (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault()
        start()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  })

  return (
    <div
      ref={rootRef}
      className={`title-screen ${launching ? 'launching' : ''} ${reducedMotion ? 'still' : ''}`}
      onClick={start}
      role="button"
      tabIndex={0}
      aria-label="Game Pod. Tap anywhere to start"
      style={{ '--art': `url(${titleArt})` }}
    >
      <div className="ts-backdrop" />

      <div className="ts-poster">
        <div className="ts-layer ts-depth-1">
          <div className="ts-camera">
            <img className="ts-art" src={titleArt} alt="Game Pod" draggable="false" />
            <div className="ts-rays ts-rays-gold" />
            <div className="ts-rays ts-rays-blue" />
            <div className="ts-pod-glow" />
            <div className="ts-led ts-led-top" />
            <div className="ts-led ts-led-front" />
            <div className="ts-floor-ring" />
            <div className="ts-logo-glow" />
            <img className="ts-logo" src={titleArt} alt="" draggable="false" />
            <div className="ts-shine" />
          </div>
        </div>
        <canvas ref={canvasRef} className="ts-layer ts-depth-2 ts-particles" />
      </div>

      <div className="ts-vignette" />
      <div className="ts-flash" />

      <p className="ts-hint">Tap anywhere to start</p>
    </div>
  )
}
