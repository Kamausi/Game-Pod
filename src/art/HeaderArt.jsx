import Logo from './Logo.jsx'
import { Sprite } from './Sprite.jsx'

// Floating 3D toys behind the logo: [model, left %, top %, width (cqw), tilt deg, float delay s].
const PROPS = {
  home: [
    ['star', 1, 0, 9, -12, 0],
    ['gem-purple', -1, 54, 8, 10, 1.4],
    ['gem-blue', 36, 4, 7, -8, 2.2],
    ['rings', 42, -6, 17, 6, 0.6],
    ['cards', 56, -4, 16, 8, 1.8],
    ['mole', 37, 36, 25, 0, 1.1],
    ['ball', 61, 42, 14, -6, 2.6],
    ['gem-green', 56, 66, 7, 12, 0.3],
    ['dice', 70, 46, 12, 14, 1.9],
  ],
  page: [
    ['star', 1, 0, 8, -12, 0],
    ['gem-purple', -1, 58, 7, 10, 1.4],
    ['rings', 33, -8, 14, 6, 0.6],
    ['cards', 46, -8, 14, 8, 1.8],
    ['mole', 29, 30, 21, 0, 1.1],
    ['ball', 51, 38, 13, -6, 2.6],
    ['dice', 63, 4, 10, 14, 1.9],
    ['gem-green', 64, 56, 7, 12, 0.3],
  ],
}

export default function HeaderArt({ variant = 'home' }) {
  return (
    <div className={`hs-scene hs-scene-${variant}`} aria-hidden="true">
      <div className="hs-stars" />
      {PROPS[variant].map(([name, left, top, w, tilt, delay]) => (
        <div
          key={name}
          className="hs-prop"
          style={{ left: `${left}%`, top: `${top}%`, width: `${w}cqw`, '--tilt': `${tilt}deg`, animationDelay: `-${delay}s` }}
        >
          <Sprite name={name} size={192} />
        </div>
      ))}
      <Logo className="hs-logo" />
    </div>
  )
}
