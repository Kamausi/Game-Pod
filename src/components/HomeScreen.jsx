import { useEffect, useRef, useState } from 'react'
import headerArt from '../assets/home/header.webp'
import { CATEGORIES, FEATURED, GAMES, gameById, searchGames } from '../games.js'
import './HomeScreen.css'

const prefersReducedMotion = () =>
  typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches

const Icon = {
  search: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5" /><path d="M15.5 15.5 21 21" /></svg>
  ),
  filter: (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M3 6h18M3 12h18M3 18h18" />
      <circle cx="15" cy="6" r="2.2" className="fill" /><circle cx="8" cy="12" r="2.2" className="fill" /><circle cx="16" cy="18" r="2.2" className="fill" />
    </svg>
  ),
  crown: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid"><path d="M3 7l4.5 4L12 4l4.5 7L21 7l-2 12H5L3 7z" /></svg>
  ),
  play: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid"><path d="M7 4.5v15l13-7.5z" /></svg>
  ),
  clock: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8.5" /><path d="M12 7.5V12l3 2" /></svg>
  ),
  chevron: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5l7 7-7 7" /></svg>
  ),
  home: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid"><path d="M12 3 2.5 11h2.5v9.5h5.5v-6h3v6H19V11h2.5z" /></svg>
  ),
  grid: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid">
      <rect x="3" y="3" width="8" height="8" rx="2" /><rect x="13" y="3" width="8" height="8" rx="2" />
      <rect x="3" y="13" width="8" height="8" rx="2" /><rect x="13" y="13" width="8" height="8" rx="2" />
    </svg>
  ),
  trophy: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid">
      <path d="M6 3h12v2h3v3a4 4 0 0 1-4 4h-.3A6 6 0 0 1 13 15.9V18h3v3H8v-3h3v-2.1A6 6 0 0 1 7.3 12H7a4 4 0 0 1-4-4V5h3V3zm0 4H5v1a2 2 0 0 0 1 1.7V7zm12 0v2.7A2 2 0 0 0 19 8V7h-1z" />
    </svg>
  ),
  chart: (
    <svg viewBox="0 0 24 24" aria-hidden="true" className="solid">
      <rect x="3" y="12" width="5" height="9" rx="1.5" /><rect x="9.5" y="4" width="5" height="17" rx="1.5" /><rect x="16" y="9" width="5" height="12" rx="1.5" />
    </svg>
  ),
}

const TABS = [
  { id: 'home', label: 'Home', icon: Icon.home },
  { id: 'all', label: 'All Games', icon: Icon.grid },
  { id: 'achievements', label: 'Achievements', icon: Icon.trophy },
  { id: 'leaderboards', label: 'Leaderboards', icon: Icon.chart },
]

function GameTile({ game, size = 'large', onOpen }) {
  return (
    <button type="button" className={`hs-tile hs-tile-${size}`} onClick={() => onOpen(game)}>
      <img src={game.art} alt="" draggable="false" />
      {!game.playable && <span className="hs-soon">Soon</span>}
      <span className="hs-tile-label">{game.name}</span>
    </button>
  )
}

function Section({ title, onSeeAll, children }) {
  return (
    <section className="hs-section">
      <div className="hs-section-head">
        <h2>{title}</h2>
        {onSeeAll && (
          <button type="button" className="hs-see-all" onClick={onSeeAll}>
            See All {Icon.chevron}
          </button>
        )}
      </div>
      {children}
    </section>
  )
}

function FeaturedCarousel({ onOpen }) {
  const trackRef = useRef(null)
  const [index, setIndex] = useState(0)
  const indexRef = useRef(0)
  const pausedUntil = useRef(0)
  const slides = FEATURED.map(gameById)

  const goTo = (i) => {
    const track = trackRef.current
    const slide = track?.children[i]
    if (slide) track.scrollTo({ left: slide.offsetLeft - (track.clientWidth - slide.clientWidth) / 2, behavior: 'smooth' })
  }

  const onScroll = () => {
    const track = trackRef.current
    const slideWidth = track.children[0].clientWidth
    const gap = track.children[1].offsetLeft - track.children[0].offsetLeft - slideWidth
    indexRef.current = Math.round(track.scrollLeft / (slideWidth + gap))
    setIndex(indexRef.current)
  }

  useEffect(() => {
    if (prefersReducedMotion()) return
    const id = setInterval(() => {
      if (Date.now() < pausedUntil.current || document.hidden) return
      goTo((indexRef.current + 1) % slides.length)
    }, 5000)
    return () => clearInterval(id)
  }, [slides.length])

  return (
    <div className="hs-featured" onPointerDown={() => (pausedUntil.current = Date.now() + 10000)}>
      <div className="hs-featured-track" ref={trackRef} onScroll={onScroll}>
        {slides.map((game) => (
          <article key={game.id} className="hs-slide" style={{ '--art': `url(${game.art})` }}>
            <div className="hs-slide-bg" />
            <img className="hs-slide-art" src={game.art} alt="" draggable="false" />
            <div className="hs-slide-copy">
              <span className="hs-badge">{Icon.crown} Featured</span>
              <h3>{game.name}</h3>
              <p>{game.tagline}</p>
              <button type="button" className="hs-play-now" onClick={() => onOpen(game)}>
                {game.playable ? Icon.play : Icon.clock}
                {game.playable ? 'Play Now' : 'Coming Soon'}
              </button>
            </div>
          </article>
        ))}
      </div>
      <div className="hs-dots" role="tablist" aria-label="Featured games">
        {slides.map((game, i) => (
          <button
            key={game.id}
            type="button"
            role="tab"
            aria-selected={i === index}
            aria-label={game.name}
            className={i === index ? 'on' : ''}
            onClick={() => {
              pausedUntil.current = Date.now() + 10000
              goTo(i)
            }}
          />
        ))}
      </div>
    </div>
  )
}

function CategoryChip({ category, active, onClick }) {
  return (
    <button
      type="button"
      className={`hs-chip ${active ? 'active' : ''}`}
      style={{ '--chip': category.color, '--chip-border': category.border }}
      onClick={onClick}
    >
      <img src={category.icon} alt="" draggable="false" />
      <span>{category.name}</span>
    </button>
  )
}

function ComingSoonPanel({ icon, title, text }) {
  return (
    <div className="hs-empty">
      <div className="hs-empty-icon">{icon}</div>
      <h2>{title}</h2>
      <p>{text}</p>
    </div>
  )
}

export default function HomeScreen({ onPlay, onTitle }) {
  const [tab, setTab] = useState('home')
  const [query, setQuery] = useState('')
  const [category, setCategory] = useState(null) // a category id, 'new', or null for everything
  const [toast, setToast] = useState(null)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const scrollRef = useRef(null)

  useEffect(() => {
    if (!toast) return
    const id = setTimeout(() => setToast(null), 2200)
    return () => clearTimeout(id)
  }, [toast])

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: 0 })
  }, [tab])

  useEffect(() => {
    if (!settingsOpen) return
    const onKey = (e) => e.key === 'Escape' && setSettingsOpen(false)
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [settingsOpen])

  const open = (game) => {
    if (game.playable) onPlay(game.id)
    else setToast(`${game.name} is coming soon`)
  }

  const showAll = (cat = null) => {
    setCategory(cat)
    setTab('all')
  }

  const popular = GAMES.filter((g) => g.categories.includes('popular'))
  const fresh = GAMES.filter((g) => g.isNew)
  const listed =
    category === 'new' ? searchGames(query, null).filter((g) => g.isNew) : searchGames(query, category)
  const searching = query.trim() !== ''

  const searchBar = (
    <div className="hs-search">
      {Icon.search}
      <input
        type="search"
        placeholder="Search games..."
        aria-label="Search games"
        value={query}
        onChange={(e) => setQuery(e.target.value)}
      />
      <button type="button" className="hs-icon-btn" aria-label="Browse by category" onClick={() => showAll(category)}>
        {Icon.filter}
      </button>
    </div>
  )

  const grid = (games) =>
    games.length ? (
      <div className="hs-grid">
        {games.map((g) => (
          <GameTile key={g.id} game={g} size="small" onOpen={open} />
        ))}
      </div>
    ) : (
      <p className="hs-none">No games here yet. More are on the way!</p>
    )

  return (
    <div className="home-screen">
      <div className="hs-column" ref={scrollRef}>
        <header className="hs-header" style={{ backgroundImage: `url(${headerArt})` }}>
          <h1 className="sr-only">Game Pod</h1>
          <button
            type="button"
            className="hs-hit hs-avatar"
            aria-label="Your profile"
            onClick={() => setToast('Player profiles are coming soon')}
          />
          <button type="button" className="hs-hit hs-gear" aria-label="Settings" onClick={() => setSettingsOpen(true)} />
        </header>

        {(tab === 'home' || tab === 'all') && searchBar}

        {tab === 'home' && searching && <Section title={`Results for “${query.trim()}”`}>{grid(listed)}</Section>}

        {tab === 'home' && !searching && (
          <>
            <FeaturedCarousel onOpen={open} />

            <Section title="Popular Right Now" onSeeAll={() => showAll('popular')}>
              <div className="hs-row hs-row-large">
                {popular.map((g) => (
                  <GameTile key={g.id} game={g} onOpen={open} />
                ))}
              </div>
            </Section>

            <Section title="Categories" onSeeAll={() => showAll(null)}>
              <div className="hs-row hs-row-chips">
                {CATEGORIES.map((c) => (
                  <CategoryChip key={c.id} category={c} active={c.id === 'popular'} onClick={() => showAll(c.id)} />
                ))}
              </div>
            </Section>

            <Section title="New & Trending" onSeeAll={() => showAll('new')}>
              <div className="hs-row hs-row-small">
                {fresh.map((g) => (
                  <GameTile key={g.id} game={g} size="small" onOpen={open} />
                ))}
              </div>
            </Section>
          </>
        )}

        {tab === 'all' && (
          <section className="hs-section">
            <div className="hs-section-head">
              <h2>All Games</h2>
            </div>
            <div className="hs-filters" role="tablist" aria-label="Filter by category">
              {[{ id: null, name: 'All' }, { id: 'new', name: 'New' }, ...CATEGORIES].map((c) => (
                <button
                  key={c.name}
                  type="button"
                  role="tab"
                  aria-selected={category === c.id}
                  className={category === c.id ? 'on' : ''}
                  onClick={() => setCategory(c.id)}
                >
                  {c.name}
                </button>
              ))}
            </div>
            {grid(listed)}
          </section>
        )}

        {tab === 'achievements' && (
          <ComingSoonPanel icon={Icon.trophy} title="Achievements" text="Earn badges as you play. Achievements are coming soon." />
        )}
        {tab === 'leaderboards' && (
          <ComingSoonPanel icon={Icon.chart} title="Leaderboards" text="See how you stack up. Leaderboards are coming soon." />
        )}
      </div>

      {toast && (
        <div className="hs-toast" role="status">
          {toast}
        </div>
      )}

      <nav className="hs-nav" aria-label="Main">
        {TABS.map((t) => (
          <button
            key={t.id}
            type="button"
            className={tab === t.id ? 'on' : ''}
            aria-current={tab === t.id ? 'page' : undefined}
            onClick={() => setTab(t.id)}
          >
            {t.icon}
            <span>{t.label}</span>
          </button>
        ))}
      </nav>

      {settingsOpen && (
        <div className="hs-sheet-backdrop" onClick={() => setSettingsOpen(false)}>
          <div className="hs-sheet" role="dialog" aria-modal="true" aria-label="Settings" onClick={(e) => e.stopPropagation()}>
            <h2>Settings</h2>
            <button type="button" className="hs-sheet-btn" onClick={onTitle}>
              Replay intro
            </button>
            <button type="button" className="hs-sheet-btn ghost" onClick={() => setSettingsOpen(false)}>
              Close
            </button>
            <p>Game Pod · v0.1</p>
          </div>
        </div>
      )}
    </div>
  )
}
