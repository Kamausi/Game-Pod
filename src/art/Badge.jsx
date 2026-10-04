import { useId } from 'react'

// Winged hexagon medals for achievements, drawn in SVG.
const TONES = {
  gold: { rim: ['#fff1a8', '#f5b81c', '#a86a00'], inner: ['#5a2a8a', '#2a1048'], glow: '#ffc533' },
  blue: { rim: ['#ffffff', '#b9c6e0', '#6a7896'], inner: ['#1b3c8f', '#0c1a4a'], glow: '#4fa8ff' },
  violet: { rim: ['#fff1a8', '#f5b81c', '#a86a00'], inner: ['#5a1f8a', '#260b48'], glow: '#b06bff' },
  green: { rim: ['#ffffff', '#b9c6e0', '#6a7896'], inner: ['#0e5a6a', '#06283a'], glow: '#2fe0a0' },
  red: { rim: ['#fff1a8', '#f5b81c', '#a86a00'], inner: ['#8a1f2a', '#3e0a12'], glow: '#ff5a6a' },
  dark: { rim: ['#e8ecff', '#8f9bbd', '#4a5578'], inner: ['#222a5a', '#10143a'], glow: '#6a8cff' },
}

const hex = (r, cx = 60, cy = 50) =>
  Array.from({ length: 6 }, (_, i) => {
    const a = (Math.PI / 3) * i - Math.PI / 2
    return `${(cx + Math.cos(a) * r).toFixed(2)},${(cy + Math.sin(a) * r).toFixed(2)}`
  }).join(' ')

// Icons on a 40x40 grid centered at (20,20).
function Icon({ name, uid }) {
  const gold = `url(#gold${uid})`
  switch (name) {
    case 'star':
      return <path d="M20 3 L25 14 L37 15.5 L28 23.5 L30.5 35.5 L20 29.5 L9.5 35.5 L12 23.5 L3 15.5 L15 14 Z" fill={gold} stroke="#a86a00" strokeWidth="1.2" strokeLinejoin="round" />
    case 'gamepad':
      return (
        <g>
          <path d="M8 12 H32 C38 12 40 18 39 26 C38 33 33 34 30 30 L27 26 H13 L10 30 C7 34 2 33 1 26 C0 18 2 12 8 12 Z" fill="#3a8bff" stroke="#0e2a7a" strokeWidth="1.4" />
          <path d="M9 17 V25 M5 21 H13" stroke="#0e1a4a" strokeWidth="3" strokeLinecap="round" />
          <circle cx="28" cy="18.5" r="2.2" fill="#ffc533" />
          <circle cx="32" cy="22" r="2.2" fill="#ff4d6d" />
          <circle cx="24" cy="22" r="2.2" fill="#2fe06a" />
          <circle cx="28" cy="25.5" r="2.2" fill="#22c8ff" />
        </g>
      )
    case 'trophy':
      return (
        <g fill={gold} stroke="#a86a00" strokeWidth="1.2" strokeLinejoin="round">
          <path d="M11 5 H29 V14 C29 21 25 25 20 25 C15 25 11 21 11 14 Z" />
          <path d="M11 8 H5 C5 15 8 18 12 18 M29 8 H35 C35 15 32 18 28 18" fill="none" strokeWidth="2.4" />
          <path d="M17 25 H23 V30 H17 Z" />
          <path d="M11 31 H29 V36 H11 Z" />
        </g>
      )
    case 'diamond':
      return (
        <g stroke="#0b3a8a" strokeWidth="1" strokeLinejoin="round">
          <path d="M8 13 L14 5 H26 L32 13 L20 36 Z" fill="#3fb7ff" />
          <path d="M8 13 H32 L20 36 Z" fill="#1f7ae0" />
          <path d="M14 5 L17 13 L20 5 L23 13 L26 5" fill="none" stroke="#bfe8ff" />
          <path d="M17 13 L20 36 L23 13" fill="#5cc8ff" stroke="none" />
        </g>
      )
    case 'crown':
      return (
        <g>
          <path d="M5 30 L3 10 L12 18 L20 6 L28 18 L37 10 L35 30 Z" fill={gold} stroke="#a86a00" strokeWidth="1.4" strokeLinejoin="round" />
          <rect x="5" y="30" width="30" height="5" rx="1.5" fill={gold} stroke="#a86a00" strokeWidth="1.2" />
          <circle cx="20" cy="23" r="3" fill="#ff3b4e" />
          <circle cx="11" cy="25" r="2" fill="#22c8ff" />
          <circle cx="29" cy="25" r="2" fill="#2fe06a" />
        </g>
      )
    case 'calendar':
      return (
        <g>
          <rect x="5" y="7" width="30" height="29" rx="5" fill="#fff" stroke="#1d2a6a" strokeWidth="1.4" />
          <path d="M5 12 C5 9 7 7 10 7 H30 C33 7 35 9 35 12 V16 H5 Z" fill="#ff3b4e" />
          <path d="M12 4 V10 M28 4 V10" stroke="#1d2a6a" strokeWidth="2.6" strokeLinecap="round" />
          <text x="20" y="32" textAnchor="middle" fontFamily="Fredoka, sans-serif" fontWeight="700" fontSize="16" fill="#1d2a6a">7</text>
        </g>
      )
    case 'joystick':
      return (
        <g>
          <rect x="6" y="26" width="28" height="9" rx="3" fill="#2a2b3a" stroke="#0d0e18" strokeWidth="1.2" />
          <rect x="18.5" y="13" width="3" height="15" fill="#c9d0de" />
          <circle cx="20" cy="11" r="7" fill="#ff2436" stroke="#8a0010" strokeWidth="1.2" />
          <circle cx="17.5" cy="8.5" r="2" fill="#fff" opacity="0.7" />
          <circle cx="29" cy="30" r="2" fill="#ffc533" />
        </g>
      )
    case 'target':
      return (
        <g>
          {[16, 12.5, 9, 5.5, 2.5].map((r, i) => (
            <circle key={r} cx="18" cy="22" r={r} fill={i % 2 ? '#fff' : '#ff2f45'} />
          ))}
          <path d="M18 22 L34 6" stroke="#c9d0de" strokeWidth="2.4" strokeLinecap="round" />
          <path d="M34 6 L36 2 L38 4 L34 6 L39 6 L36 9 Z" fill="#ff2f45" />
        </g>
      )
    case 'xo':
      return (
        <g strokeLinecap="round" strokeWidth="5">
          <path d="M5 7 L17 19 M17 7 L5 19" stroke="#3a8bff" />
          <circle cx="28" cy="27" r="7" fill="none" stroke="#ff3b4e" />
        </g>
      )
    case 'users':
      return (
        <g fill="#c58bff" stroke="#4a1a8a" strokeWidth="1.2">
          <circle cx="15" cy="13" r="6" />
          <path d="M3 34 C3 25 9 21 15 21 C21 21 27 25 27 34 Z" />
          <circle cx="28" cy="15" r="5" fill="#9b5cff" />
          <path d="M24 22 C30 21 37 24 37 33 H29 C29 28 27 24 24 22 Z" fill="#9b5cff" />
        </g>
      )
    default:
      return null
  }
}

function Wing({ fill, stroke }) {
  return (
    <g fill={fill} stroke={stroke} strokeWidth="1" strokeLinejoin="round">
      <path d="M34 40 C 24 34, 12 26, 4 14 C 14 18, 26 22, 36 30 Z" />
      <path d="M33 50 C 22 47, 10 42, 2 32 C 12 34, 24 37, 35 41 Z" />
      <path d="M34 60 C 24 60, 13 57, 6 50 C 15 50, 26 51, 36 53 Z" />
    </g>
  )
}

export default function Badge({ icon = 'star', tone = 'gold', wings = true, locked = false, className = '', title }) {
  const t = TONES[tone] ?? TONES.gold
  const uid = useId().replace(/:/g, '')
  const silver = t.rim[0] === '#ffffff' || tone === 'dark'
  return (
    <svg className={`badge ${locked ? 'locked' : ''} ${className}`} viewBox="0 0 120 100" role={title ? 'img' : undefined} aria-label={title} aria-hidden={title ? undefined : true}>
      <defs>
        <linearGradient id={`rim${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor={t.rim[0]} />
          <stop offset="0.5" stopColor={t.rim[1]} />
          <stop offset="1" stopColor={t.rim[2]} />
        </linearGradient>
        <linearGradient id={`in${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor={t.inner[0]} />
          <stop offset="1" stopColor={t.inner[1]} />
        </linearGradient>
        <linearGradient id={`gold${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#fff3a0" />
          <stop offset="0.55" stopColor="#ffc21f" />
          <stop offset="1" stopColor="#e08a00" />
        </linearGradient>
        <linearGradient id={`wing${uid}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor={silver ? '#ffffff' : '#fff1a8'} />
          <stop offset="1" stopColor={silver ? '#8f9bbd' : '#d08a10'} />
        </linearGradient>
        <radialGradient id={`glow${uid}`}>
          <stop offset="0" stopColor={t.glow} stopOpacity="0.55" />
          <stop offset="1" stopColor={t.glow} stopOpacity="0" />
        </radialGradient>
      </defs>
      <circle cx="60" cy="50" r="48" fill={`url(#glow${uid})`} />
      {wings && (
        <>
          <Wing fill={`url(#wing${uid})`} stroke={silver ? '#5a6688' : '#9a6200'} />
          <g transform="translate(120 0) scale(-1 1)">
            <Wing fill={`url(#wing${uid})`} stroke={silver ? '#5a6688' : '#9a6200'} />
          </g>
        </>
      )}
      <polygon points={hex(36)} fill={`url(#rim${uid})`} stroke={t.rim[2]} strokeWidth="1.5" strokeLinejoin="round" />
      <polygon points={hex(28)} fill={`url(#in${uid})`} stroke="rgba(0,0,0,.35)" strokeWidth="1.5" strokeLinejoin="round" />
      <g transform="translate(41 31) scale(0.95)">
        <Icon name={icon} uid={uid} />
      </g>
    </svg>
  )
}

export function Trophy({ className = '' }) {
  const uid = useId().replace(/:/g, '')
  return (
    <svg className={className} viewBox="0 0 40 40" aria-hidden="true">
      <defs>
        <linearGradient id={`gold${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#fff3a0" />
          <stop offset="0.55" stopColor="#ffc21f" />
          <stop offset="1" stopColor="#e08a00" />
        </linearGradient>
      </defs>
      <Icon name="trophy" uid={uid} />
    </svg>
  )
}
