import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { emptyStats, recordGame, totalXp, levelInfo } from './progress.js'
import { ARCADE_IDS } from './games.js'

const KEY = 'gamepod:v1'

export const DEFAULT_PROFILE = {
  name: 'Player',
  handle: 'player',
  bio: 'Play games. Good vibes. Always leveling up.',
  avatar: 'you',
}

export const DEFAULT_SETTINGS = {
  sound: true,
  music: true,
  volume: 0.8,
  haptics: true,
  reduceMotion: null, // null = follow the system setting
  hints: true,
  tutorials: true,
}

const fresh = () => ({
  profile: DEFAULT_PROFILE,
  settings: DEFAULT_SETTINGS,
  stats: emptyStats(),
  unlocked: {},
  favorites: [],
  seenTutorials: [],
})

function load() {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY))
    if (!saved) return fresh()
    const base = fresh()
    return {
      ...base,
      ...saved,
      profile: { ...base.profile, ...saved.profile },
      settings: { ...base.settings, ...saved.settings },
      stats: { ...base.stats, ...saved.stats },
    }
  } catch {
    return fresh()
  }
}

const StoreContext = createContext(null)

export function StoreProvider({ children }) {
  const [data, setData] = useState(load)
  const dataRef = useRef(data)
  dataRef.current = data
  const [systemReduced, setSystemReduced] = useState(
    () => typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches,
  )

  useEffect(() => {
    try {
      localStorage.setItem(KEY, JSON.stringify(data))
    } catch {
      // Storage can be unavailable (private mode, blocked); progress then lasts for this visit only.
    }
  }, [data])

  useEffect(() => {
    const mq = window.matchMedia?.('(prefers-reduced-motion: reduce)')
    if (!mq) return
    const onChange = () => setSystemReduced(mq.matches)
    mq.addEventListener('change', onChange)
    return () => mq.removeEventListener('change', onChange)
  }, [])

  // Computed from the latest committed data so the caller gets the result (new achievements) right away.
  const record = useCallback((game) => {
    const d = dataRef.current
    const r = recordGame(d.stats, d.unlocked, game, Date.now(), ARCADE_IDS)
    dataRef.current = { ...d, stats: r.stats, unlocked: r.unlocked }
    setData(dataRef.current)
    return r
  }, [])

  const actions = useMemo(
    () => ({
      record,
      setProfile: (patch) => setData((d) => ({ ...d, profile: { ...d.profile, ...patch } })),
      setSetting: (key, value) =>
        setData((d) => ({
          ...d,
          settings: { ...d.settings, [key]: value },
          // Turning tutorials back on shows them again.
          seenTutorials: key === 'tutorials' && value ? [] : d.seenTutorials,
        })),
      toggleFavorite: (id) =>
        setData((d) => ({
          ...d,
          favorites: d.favorites.includes(id) ? d.favorites.filter((f) => f !== id) : [...d.favorites, id],
        })),
      markTutorialSeen: (id) =>
        setData((d) => (d.seenTutorials.includes(id) ? d : { ...d, seenTutorials: [...d.seenTutorials, id] })),
      resetProgress: () =>
        setData((d) => ({ ...d, stats: emptyStats(), unlocked: {}, favorites: [], seenTutorials: [] })),
    }),
    [record],
  )

  const xp = totalXp(data.stats, data.unlocked)
  const value = {
    ...data,
    ...actions,
    xp,
    level: levelInfo(xp),
    reduceMotion: data.settings.reduceMotion ?? systemReduced,
  }
  return <StoreContext.Provider value={value}>{children}</StoreContext.Provider>
}

export const useStore = () => useContext(StoreContext)
