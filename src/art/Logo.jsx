import { useId } from 'react'

// "GAME POD" logo: chunky Fredoka lettering with a dark outline, a stacked 3D extrusion
// and per-letter gradients, set inside a glowing pod ring.
const LETTERS = [
  { ch: 'P', x: 62, top: '#7fd0ff', bottom: '#1f5fff' },
  { ch: 'O', x: 128, top: '#fff07a', bottom: '#ff9a00' },
  { ch: 'D', x: 194, top: '#ff8a7a', bottom: '#e01a2a' },
]

function Chunky({ children, x, y, size, fill, depth = 7 }) {
  const common = { x, y, fontFamily: 'Fredoka, system-ui, sans-serif', fontWeight: 700, fontSize: size, textAnchor: 'middle' }
  return (
    <g>
      {Array.from({ length: depth }, (_, i) => (
        <text key={i} {...common} y={y + depth - i} fill="#0b1240" stroke="#0b1240" strokeWidth="14" strokeLinejoin="round">
          {children}
        </text>
      ))}
      <text {...common} fill="#0b1240" stroke="#0b1240" strokeWidth="14" strokeLinejoin="round">
        {children}
      </text>
      <text {...common} fill={fill}>
        {children}
      </text>
    </g>
  )
}

export default function Logo({ className = '' }) {
  const uid = useId().replace(/:/g, '')
  return (
    <svg className={`logo ${className}`} viewBox="0 0 256 190" role="img" aria-label="Game Pod">
      <defs>
        <radialGradient id={`pod${uid}`} cx="50%" cy="45%" r="55%">
          <stop offset="0" stopColor="#1d3aa8" stopOpacity="0.95" />
          <stop offset="0.7" stopColor="#0c1450" stopOpacity="0.95" />
          <stop offset="1" stopColor="#060a2e" stopOpacity="0.9" />
        </radialGradient>
        <linearGradient id={`ring${uid}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#9fe2ff" />
          <stop offset="0.5" stopColor="#3a7bff" />
          <stop offset="1" stopColor="#8a3dff" />
        </linearGradient>
        <linearGradient id={`white${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#ffffff" />
          <stop offset="1" stopColor="#b9cbff" />
        </linearGradient>
        {LETTERS.map((l) => (
          <linearGradient key={l.ch} id={`l${l.ch}${uid}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor={l.top} />
            <stop offset="1" stopColor={l.bottom} />
          </linearGradient>
        ))}
        <filter id={`glow${uid}`} x="-30%" y="-30%" width="160%" height="160%">
          <feGaussianBlur stdDeviation="5" />
        </filter>
      </defs>
      <ellipse cx="128" cy="98" rx="118" ry="86" fill="none" stroke="#3a8bff" strokeWidth="10" opacity="0.55" filter={`url(#glow${uid})`} />
      <ellipse cx="128" cy="98" rx="116" ry="84" fill={`url(#pod${uid})`} stroke={`url(#ring${uid})`} strokeWidth="5" />
      <rect x="98" y="8" width="60" height="8" rx="4" fill="#7fe0ff" />
      <rect x="98" y="180" width="60" height="7" rx="3.5" fill="#7fe0ff" />
      <Chunky x={128} y={84} size={66} fill={`url(#white${uid})`}>
        GAME
      </Chunky>
      {LETTERS.map((l) => (
        <Chunky key={l.ch} x={l.x} y={160} size={84} fill={`url(#l${l.ch}${uid})`} depth={9}>
          {l.ch}
        </Chunky>
      ))}
    </svg>
  )
}
