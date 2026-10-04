import { useId } from 'react'

// Cartoon avatars drawn in SVG from a few parts. Each preset picks skin, hair, outfit and extras.
export const AVATAR_PRESETS = {
  you: { skin: '#f2c29b', hair: 'short', hairColor: '#5a3520', shirt: '#2b2f45', bg: '#3d8bff', crown: true },
  gameking: { skin: '#f0b98d', hair: 'curly', hairColor: '#6b3a1f', shirt: '#f2a91f', bg: '#ffb21f', glasses: 'shades' },
  pixelplayz: { skin: '#f7d0b0', hair: 'long', hairColor: '#f5c95a', shirt: '#ff6fa8', bg: '#7c8cff', headphones: '#ff5fa2' },
  ninjanate: { skin: '#d9a07a', hair: 'none', shirt: '#1d1d26', bg: '#ff7a3d', cap: '#1b1c24', mask: true },
  lunastars: { skin: '#e9b48f', hair: 'long', hairColor: '#1fb5c9', shirt: '#3a6bff', bg: '#22c8ff' },
  playrocket: { skin: '#e2a378', hair: 'none', shirt: '#ff3b30', bg: '#ffc51f', cap: '#e8232f', glasses: 'shades' },
  tetratom: { skin: '#e8b089', hair: 'short', hairColor: '#3b2416', shirt: '#ff9a1f', bg: '#ff9a3c' },
  joystickjen: { skin: '#f4c7a5', hair: 'long', hairColor: '#9b4dff', shirt: '#ff4fd8', bg: '#b06bff' },
  blockboss: { creature: 'frog', bg: '#2fe06a' },
  cardshark: { creature: 'shark', bg: '#22a8ff' },
  diceduel: { skin: '#f2c09a', hair: 'bob', hairColor: '#e8402a', shirt: '#ffd21f', bg: '#ff4d6d' },
}

export const AVATAR_IDS = Object.keys(AVATAR_PRESETS)

function shade(hex, amt) {
  const n = parseInt(hex.slice(1), 16)
  const f = (v) => Math.max(0, Math.min(255, Math.round(v + amt * 255)))
  return `rgb(${f(n >> 16)},${f((n >> 8) & 255)},${f(n & 255)})`
}

function Eyes({ y = 52, dx = 8.5 }) {
  return (
    <g>
      {[-1, 1].map((s) => (
        <g key={s}>
          <ellipse cx={50 + s * dx} cy={y} rx="5.4" ry="6.2" fill="#fff" />
          <circle cx={50 + s * dx + 0.6} cy={y + 0.8} r="3.4" fill="#2a1a12" />
          <circle cx={50 + s * dx + 1.8} cy={y - 0.8} r="1.3" fill="#fff" />
        </g>
      ))}
    </g>
  )
}

function Face({ p }) {
  const dark = shade(p.skin, -0.12)
  return (
    <g>
      <ellipse cx="27.5" cy="53" rx="5" ry="7" fill={dark} />
      <ellipse cx="72.5" cy="53" rx="5" ry="7" fill={dark} />
      <ellipse cx="50" cy="50" rx="23" ry="25" fill={p.skin} />
      <ellipse cx="38" cy="60" rx="4.5" ry="2.6" fill="#ff7a8a" opacity="0.35" />
      <ellipse cx="62" cy="60" rx="4.5" ry="2.6" fill="#ff7a8a" opacity="0.35" />
      {!p.glasses && <Eyes />}
      {!p.glasses && (
        <g stroke={shade(p.hairColor ?? '#3b2416', -0.1)} strokeWidth="2.2" strokeLinecap="round" fill="none">
          <path d="M37 42.5 Q41.5 40 46 42" />
          <path d="M54 42 Q58.5 40 63 42.5" />
        </g>
      )}
      <ellipse cx="50" cy="58.5" rx="2.4" ry="1.7" fill={dark} />
      {!p.mask && <path d="M42.5 64 Q50 71 57.5 64 Q50 67.5 42.5 64 Z" fill="#7a2a1e" stroke="#7a2a1e" strokeWidth="1.6" strokeLinejoin="round" />}
    </g>
  )
}

function Hair({ p, behind }) {
  if (p.hair === 'none') return null
  const c = p.hairColor
  const hi = shade(c, 0.12)
  if (behind) {
    if (p.hair === 'long') return <path d="M24 48 C 22 26, 36 18, 50 18 C 64 18, 78 26, 76 48 L 80 84 C 70 90, 30 90, 20 84 Z" fill={shade(c, -0.08)} />
    if (p.hair === 'bob') return <path d="M24 48 C 22 26, 36 18, 50 18 C 64 18, 78 26, 76 48 L 78 68 C 70 72, 30 72, 22 68 Z" fill={shade(c, -0.08)} />
    return null
  }
  if (p.hair === 'short')
    return (
      <g>
        <path d="M26 48 C 23 27, 37 19, 52 20 C 67 20, 78 30, 74 48 C 71 38, 63 33, 54 34 C 46 31, 35 35, 26 48 Z" fill={c} />
        <path d="M40 22 C 44 14, 52 14, 56 21 C 60 15, 68 18, 66 25" fill={c} />
        <path d="M36 30 C 42 26, 50 26, 56 29" stroke={hi} strokeWidth="2" fill="none" strokeLinecap="round" />
      </g>
    )
  if (p.hair === 'curly')
    return (
      <g fill={c}>
        <path d="M25 50 C 21 28, 36 18, 51 19 C 67 19, 80 30, 75 50 C 72 40, 64 35, 55 36 C 46 33, 34 38, 25 50 Z" />
        {[[30, 32], [38, 24], [48, 21], [58, 22], [67, 27], [73, 36]].map(([x, y]) => (
          <circle key={x} cx={x} cy={y} r="7.5" />
        ))}
        <path d="M38 28 C 44 25, 52 25, 58 27" stroke={hi} strokeWidth="2" fill="none" strokeLinecap="round" />
      </g>
    )
  // long / bob: bangs in front
  return (
    <g>
      <path d="M26 46 C 25 28, 38 21, 52 21 C 66 21, 76 30, 74 46 C 68 36, 60 32, 50 33 C 44 38, 34 40, 26 46 Z" fill={c} />
      <path d="M36 28 C 42 24, 52 24, 60 27" stroke={hi} strokeWidth="2" fill="none" strokeLinecap="round" />
    </g>
  )
}

function Extras({ p }) {
  return (
    <g>
      {p.cap && (
        <g>
          <path d="M25 44 C 24 24, 38 16, 50 16 C 62 16, 76 24, 75 44 Z" fill={p.cap} />
          <path d="M50 44 L 84 44 C 86 47, 80 50, 74 49 L 50 48 Z" fill={shade(p.cap, -0.1)} />
          <path d="M34 26 C 40 21, 52 20, 60 22" stroke={shade(p.cap, 0.2)} strokeWidth="2" fill="none" strokeLinecap="round" />
          {p.mask && <circle cx="50" cy="30" r="4" fill="#22c8ff" />}
        </g>
      )}
      {p.mask && (
        <g>
          <path d="M26 56 C 30 75, 42 76, 50 76 C 58 76, 70 75, 74 56 C 66 58, 34 58, 26 56 Z" fill="#15161d" />
          <path d="M34 62 C 42 64, 58 64, 66 62" stroke="#2a2b36" strokeWidth="1.5" fill="none" />
        </g>
      )}
      {p.glasses && (
        <g>
          <rect x="35" y="45" width="13" height="10" rx="4.5" fill="#101320" />
          <rect x="52" y="45" width="13" height="10" rx="4.5" fill="#101320" />
          <path d="M48 49 L 52 49" stroke="#101320" strokeWidth="2.4" />
          <path d="M38 47.5 L 42 47.5 M 55 47.5 L 59 47.5" stroke="#5fa8ff" strokeWidth="1.6" strokeLinecap="round" />
        </g>
      )}
      {p.headphones && (
        <g>
          <path d="M23 50 C 21 22, 79 22, 77 50" stroke={p.headphones} strokeWidth="5" fill="none" strokeLinecap="round" />
          <rect x="18" y="45" width="11" height="17" rx="5" fill={p.headphones} />
          <rect x="71" y="45" width="11" height="17" rx="5" fill={p.headphones} />
          <rect x="21" y="48" width="5" height="11" rx="2.5" fill="#fff" opacity="0.8" />
          <rect x="74" y="48" width="5" height="11" rx="2.5" fill="#fff" opacity="0.8" />
        </g>
      )}
    </g>
  )
}

function Frog() {
  return (
    <g>
      <path d="M18 100 C 20 82, 34 76, 50 76 C 66 76, 80 82, 82 100 Z" fill="#2fbf4f" />
      <ellipse cx="50" cy="58" rx="31" ry="23" fill="#4fd65a" />
      <circle cx="35" cy="38" r="12" fill="#4fd65a" />
      <circle cx="65" cy="38" r="12" fill="#4fd65a" />
      <circle cx="35" cy="38" r="8" fill="#fff" />
      <circle cx="65" cy="38" r="8" fill="#fff" />
      <circle cx="36" cy="39.5" r="4.5" fill="#1a1a1a" />
      <circle cx="64" cy="39.5" r="4.5" fill="#1a1a1a" />
      <circle cx="37.5" cy="37.5" r="1.6" fill="#fff" />
      <circle cx="65.5" cy="37.5" r="1.6" fill="#fff" />
      <path d="M30 62 Q50 78 70 62" stroke="#1e7a30" strokeWidth="3" fill="none" strokeLinecap="round" />
      <ellipse cx="30" cy="64" rx="5" ry="3" fill="#ff7a8a" opacity="0.4" />
      <ellipse cx="70" cy="64" rx="5" ry="3" fill="#ff7a8a" opacity="0.4" />
    </g>
  )
}

function Shark() {
  return (
    <g>
      <path d="M18 100 C 20 82, 34 76, 50 76 C 66 76, 80 82, 82 100 Z" fill="#2a6bd6" />
      <path d="M50 10 L 60 30 L 42 30 Z" fill="#3a8be6" />
      <ellipse cx="50" cy="52" rx="28" ry="27" fill="#4aa0f0" />
      <path d="M27 58 C 32 76, 68 76, 73 58 C 64 64, 36 64, 27 58 Z" fill="#eef6ff" />
      <path d="M33 62 L 37 67 L 41 63 L 45 68 L 50 63 L 55 68 L 59 63 L 63 67 L 67 62" fill="#fff" stroke="#c0d4ea" strokeWidth="0.8" />
      <Eyes y={46} dx={11} />
      <path d="M31 37 L 43 41 M 69 37 L 57 41" stroke="#1d4f99" strokeWidth="2.4" strokeLinecap="round" />
    </g>
  )
}

export default function Avatar({ id = 'you', className = '', title }) {
  const p = AVATAR_PRESETS[id] ?? AVATAR_PRESETS.you
  const uid = useId().replace(/:/g, '')
  return (
    <svg className={`avatar ${className}`} viewBox="0 0 100 100" role={title ? 'img' : undefined} aria-label={title} aria-hidden={title ? undefined : true}>
      <defs>
        <radialGradient id={`bg${uid}`} cx="50%" cy="35%" r="70%">
          <stop offset="0" stopColor={shade(p.bg, 0.18)} />
          <stop offset="1" stopColor={shade(p.bg, -0.22)} />
        </radialGradient>
        <clipPath id={`clip${uid}`}>
          <circle cx="50" cy="50" r="50" />
        </clipPath>
      </defs>
      <circle cx="50" cy="50" r="50" fill={`url(#bg${uid})`} />
      <g clipPath={`url(#clip${uid})`}>
        {p.creature === 'frog' && <Frog />}
        {p.creature === 'shark' && <Shark />}
        {!p.creature && (
          <g>
            <Hair p={p} behind />
            <path d="M16 100 C 18 82, 33 75, 50 75 C 67 75, 82 82, 84 100 Z" fill={p.shirt} />
            <path d="M38 76 Q50 86 62 76" stroke={shade(p.shirt, 0.15)} strokeWidth="3" fill="none" />
            <rect x="43" y="68" width="14" height="10" rx="4" fill={shade(p.skin, -0.1)} />
            <Face p={p} />
            <Hair p={p} />
            <Extras p={p} />
          </g>
        )}
      </g>
      {p.crown && (
        <g transform="translate(66 70) scale(0.9)">
          <path d="M2 18 L 0 4 L 8 10 L 14 0 L 20 10 L 28 4 L 26 18 Z" fill="#ffc533" stroke="#b97a00" strokeWidth="1.5" strokeLinejoin="round" />
          <circle cx="14" cy="13" r="2.2" fill="#ff3b4e" />
        </g>
      )}
    </svg>
  )
}
