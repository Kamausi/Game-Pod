import { useEffect, useRef, useState } from 'react'
import { CATEGORIES, FEATURED, GAMES, gameById, searchGames } from '../../games.js'
import { useStore } from '../../store.jsx'
import { GameTile, GridTile, Header, Section } from './common.jsx'
import { Icon } from './icons.jsx'

function FeaturedCarousel({ onOpen }) {
  const { reduceMotion } = useStore()
  const trackRef = useRef(null)
  const [index, setIndex] = useState(0)
  const indexRef = useRef(0)
  const pausedUntil = useRef(0)
  const slides = FEATURED.map(gameById)

  const goTo = (i) => {
    const track = trackRef.current
    const slide = track?.children[i]
    if (slide) track.scrollTo({ left: slide.offsetLeft - (track.clientWidth - slide.clientWidth) / 2, behavior: reduceMotion ? 'auto' : 'smooth' })
  }

  const onScroll = () => {
    const track = trackRef.current
    const step = track.children[1].offsetLeft - track.children[0].offsetLeft
    indexRef.current = Math.round(track.scrollLeft / step)
    setIndex(indexRef.current)
  }

  useEffect(() => {
    if (reduceMotion) return
    const id = setInterval(() => {
      if (Date.now() < pausedUntil.current || document.hidden) return
      goTo((indexRef.current + 1) % slides.length)
    }, 5000)
    return () => clearInterval(id)
  }, [slides.length, reduceMotion])

  return (
    <div className="hs-featured" onPointerDown={() => (pausedUntil.current = Date.now() + 10000)}>
      <div className="hs-featured-track" ref={trackRef} onScroll={onScroll}>
        {slides.map((game) => (
          <article key={game.id} className="hs-slide" style={{ '--art': `url(${game.banner ?? game.art})` }}>
            <div className="hs-slide-bg" />
            <img className="hs-slide-art" src={game.banner ?? game.art} alt="" draggable="false" />
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

export function CategoryChip({ category, active, onClick }) {
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

export function SearchBar({ query, setQuery, children }) {
  return (
    <div className="hs-search-row">
      <div className="hs-search">
        {Icon.search}
        <input type="search" placeholder="Search games..." aria-label="Search games" value={query} onChange={(e) => setQuery(e.target.value)} />
        {children}
      </div>
    </div>
  )
}

export default function HomePage({ header, onOpen, onBrowse }) {
  const [query, setQuery] = useState('')
  const popular = GAMES.filter((g) => g.categories.includes('popular'))
  const fresh = GAMES.filter((g) => g.isNew)
  const searching = query.trim() !== ''

  return (
    <>
      <Header variant="home" {...header} />
      <SearchBar query={query} setQuery={setQuery}>
        <button type="button" className="hs-icon-btn" aria-label="Browse all games" onClick={() => onBrowse(null)}>
          {Icon.sliders}
        </button>
      </SearchBar>

      {searching ? (
        <Section title={`Results for “${query.trim()}”`}>
          {searchGames(query, null).length ? (
            <div className="hs-ggrid">
              {searchGames(query, null).map((g) => (
                <GridTile key={g.id} game={g} onOpen={onOpen} />
              ))}
            </div>
          ) : (
            <p className="hs-none">No games match “{query.trim()}”.</p>
          )}
        </Section>
      ) : (
        <>
          <FeaturedCarousel onOpen={onOpen} />
          <Section title="Popular Right Now" onSeeAll={() => onBrowse('popular')}>
            <div className="hs-row hs-row-large">
              {popular.map((g) => (
                <GameTile key={g.id} game={g} onOpen={onOpen} />
              ))}
            </div>
          </Section>
          <Section title="Categories" onSeeAll={() => onBrowse(null)}>
            <div className="hs-row hs-row-chips">
              {CATEGORIES.map((c) => (
                <CategoryChip key={c.id} category={c} active={c.id === 'popular'} onClick={() => onBrowse(c.id)} />
              ))}
            </div>
          </Section>
          <Section title="New & Trending" onSeeAll={() => onBrowse('new')}>
            <div className="hs-row hs-row-small">
              {fresh.map((g) => (
                <GameTile key={g.id} game={g} size="small" onOpen={onOpen} />
              ))}
            </div>
          </Section>
        </>
      )}
    </>
  )
}
