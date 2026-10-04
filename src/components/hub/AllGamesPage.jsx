import { useEffect, useRef, useState } from 'react'
import { CATEGORIES, GAMES, searchGames } from '../../games.js'
import { useStore } from '../../store.jsx'
import { Sprite } from '../../art/Sprite.jsx'
import { GridTile, Header } from './common.jsx'
import { SearchBar } from './HomePage.jsx'
import { Icon } from './icons.jsx'

const SORTS = [
  { id: 'popular', name: 'Popular' },
  { id: 'az', name: 'A–Z' },
  { id: 'new', name: 'Newest' },
  { id: 'favorites', name: 'Favorites first' },
]

const CHIP_ORDER = ['popular', 'action', 'puzzle', 'arcade', 'sports', 'card']

export default function AllGamesPage({ header, onOpen, category, setCategory }) {
  const { favorites } = useStore()
  const [query, setQuery] = useState('')
  const [sort, setSort] = useState('popular')
  const [sortOpen, setSortOpen] = useState(false)
  const sortRef = useRef(null)

  useEffect(() => {
    if (!sortOpen) return
    const close = (e) => !sortRef.current?.contains(e.target) && setSortOpen(false)
    window.addEventListener('pointerdown', close)
    return () => window.removeEventListener('pointerdown', close)
  }, [sortOpen])

  let games = category === 'new' ? searchGames(query, null).filter((g) => g.isNew) : searchGames(query, category)
  const order = (g) => GAMES.indexOf(g)
  games = [...games].sort((a, b) => {
    if (sort === 'az') return a.name.localeCompare(b.name)
    if (sort === 'new') return (b.isNew ? 1 : 0) - (a.isNew ? 1 : 0) || order(a) - order(b)
    if (sort === 'favorites') return favorites.includes(b.id) - favorites.includes(a.id) || order(a) - order(b)
    return (b.playable ? 1 : 0) - (a.playable ? 1 : 0) || order(a) - order(b)
  })

  const chips = [
    { id: null, name: 'All', svg: Icon.grid },
    { id: 'new', name: 'New', svg: Icon.star },
    ...CHIP_ORDER.map((id) => CATEGORIES.find((c) => c.id === id)),
  ]

  return (
    <>
      <Header variant="page" title="All Games" meta={`${GAMES.length} Games`} {...header} />
      <div className="hs-search-row hs-search-row-sort">
        <SearchBar query={query} setQuery={setQuery} />
        <div className="hs-sort" ref={sortRef}>
          <button type="button" className="hs-sort-btn" aria-haspopup="listbox" aria-expanded={sortOpen} onClick={() => setSortOpen((o) => !o)}>
            {Icon.sliders} Sort {Icon.chevronDown}
          </button>
          {sortOpen && (
            <ul className="hs-menu" role="listbox" aria-label="Sort games">
              {SORTS.map((s) => (
                <li key={s.id}>
                  <button
                    type="button"
                    role="option"
                    aria-selected={sort === s.id}
                    className={sort === s.id ? 'on' : ''}
                    onClick={() => {
                      setSort(s.id)
                      setSortOpen(false)
                    }}
                  >
                    {s.name} {sort === s.id && Icon.check}
                  </button>
                </li>
              ))}
            </ul>
          )}
        </div>
      </div>

      <div className="hs-pills" role="tablist" aria-label="Filter by category">
        {chips.map((c) => (
          <button key={c.name} type="button" role="tab" aria-selected={category === c.id} className={category === c.id ? 'on' : ''} onClick={() => setCategory(c.id)}>
            {c.icon ? <Sprite name={c.icon} size={96} className="hs-pill-sprite" /> : <span className="hs-pill-svg">{c.svg}</span>}
            {c.name}
          </button>
        ))}
      </div>

      {games.length ? (
        <div className="hs-ggrid">
          {games.map((g) => (
            <GridTile key={g.id} game={g} onOpen={onOpen} />
          ))}
        </div>
      ) : (
        <p className="hs-none">No games here yet. More are on the way!</p>
      )}
    </>
  )
}
