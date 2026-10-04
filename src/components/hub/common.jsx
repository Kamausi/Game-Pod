import { useEffect } from 'react'
import Avatar from '../../art/Avatar.jsx'
import Badge from '../../art/Badge.jsx'
import HeaderArt from '../../art/HeaderArt.jsx'
import { GameArt } from '../../art/Sprite.jsx'
import { sfx } from '../../audio.js'
import { useStore } from '../../store.jsx'
import { Icon } from './icons.jsx'

export const AchievementBadge = ({ a, className = '' }) => <Badge icon={a.icon} tone={a.tone} locked={!a.done} className={className} />

/** Live header: floating 3D toys, the Game Pod logo, and the player's avatar, level and settings. */
export function Header({ variant = 'home', title, subtitle, meta, settingsActive, onProfile, onSettings }) {
  const { profile, level } = useStore()
  return (
    <header className={`hs-header hs-header-${variant}`}>
      <HeaderArt variant={variant} />
      {variant === 'home' && <h1 className="sr-only">Game Pod</h1>}
      <div className="hs-me-wrap">
        <button type="button" className="hs-me" onClick={onProfile} aria-label={`Your profile, level ${level.level}`}>
          <Avatar id={profile.avatar} />
        </button>
        <span className="hs-me-level" aria-hidden="true">
          Lv. {level.level}
        </span>
      </div>
      <button type="button" className={`hs-gear ${settingsActive ? 'on' : ''}`} onClick={onSettings} aria-label="Settings">
        {Icon.gear}
      </button>
      {title && (
        <div className="hs-page-title">
          <div>
            <h1>{title}</h1>
            {subtitle && <p>{subtitle}</p>}
          </div>
          {meta && <span className="hs-page-meta">{meta}</span>}
        </div>
      )}
    </header>
  )
}

export function Section({ title, icon, onSeeAll, action, children, className = '' }) {
  return (
    <section className={`hs-section ${className}`}>
      <div className="hs-section-head">
        <h2>
          {icon && <span className="hs-section-icon">{icon}</span>}
          {title}
        </h2>
        {onSeeAll && (
          <button type="button" className="hs-see-all" onClick={onSeeAll}>
            See All {Icon.chevron}
          </button>
        )}
        {action}
      </div>
      {children}
    </section>
  )
}

// Home-row tile: the game's 3D art with its name below.
export function GameTile({ game, size = 'large', onOpen }) {
  return (
    <button type="button" className={`hs-tile hs-tile-${size}`} onClick={() => onOpen(game)}>
      <GameArt game={game} />
      {!game.playable && <span className="hs-soon">Soon</span>}
      <span className="hs-tile-label">{game.name}</span>
    </button>
  )
}

// All Games tile: the game's 3D art with a working favorite heart.
export function GridTile({ game, onOpen }) {
  const { favorites, toggleFavorite } = useStore()
  const fav = favorites.includes(game.id)
  return (
    <div className="hs-gtile">
      <button type="button" className="hs-gtile-main" onClick={() => onOpen(game)}>
        <GameArt game={game} />
        {!game.playable && <span className="hs-soon hs-soon-left">Soon</span>}
        <span className="hs-gtile-label">{game.name}</span>
      </button>
      <button
        type="button"
        className={`hs-heart ${fav ? 'on' : ''}`}
        aria-pressed={fav}
        aria-label={fav ? `Remove ${game.name} from favorites` : `Add ${game.name} to favorites`}
        onClick={() => {
          sfx('tap')
          toggleFavorite(game.id)
        }}
      >
        {fav ? Icon.heartFill : Icon.heart}
      </button>
    </div>
  )
}

export function ProgressBar({ value, max, color = 'gold' }) {
  const pct = max ? Math.min(100, (value / max) * 100) : 0
  return (
    <span className={`hs-bar hs-bar-${color}`} role="progressbar" aria-valuemin={0} aria-valuemax={max} aria-valuenow={value}>
      <span style={{ width: `${pct}%` }} />
    </span>
  )
}

export function Toggle({ checked, onChange, label }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      className={`hs-toggle ${checked ? 'on' : ''}`}
      onClick={() => {
        sfx('tap')
        onChange(!checked)
      }}
    >
      <span />
    </button>
  )
}

export function Sheet({ title, onClose, children }) {
  useEffect(() => {
    const onKey = (e) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])
  return (
    <div className="hs-sheet-backdrop" onClick={onClose}>
      <div className="hs-sheet" role="dialog" aria-modal="true" aria-label={title} onClick={(e) => e.stopPropagation()}>
        <div className="hs-sheet-head">
          <h2>{title}</h2>
          <button type="button" className="hs-sheet-x" onClick={onClose} aria-label="Close">
            {Icon.close}
          </button>
        </div>
        {children}
      </div>
    </div>
  )
}

export function EmptyState({ icon, title, text, children }) {
  return (
    <div className="hs-empty">
      <div className="hs-empty-icon">{icon}</div>
      <h2>{title}</h2>
      <p>{text}</p>
      {children}
    </div>
  )
}

export const formatNumber = (n) => n.toLocaleString('en-US')

export function formatDate(iso) {
  return new Date(iso).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' })
}
