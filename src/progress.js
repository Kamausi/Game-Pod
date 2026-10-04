// Pure progress logic: points, XP/levels, streaks and achievements. No React, no storage.

export const XP_PER_LEVEL = 500

export function emptyStats() {
  return {
    games: 0,
    wins: 0,
    losses: 0,
    draws: 0,
    points: 0,
    played: {}, // gameId -> games played
    won: {}, // gameId -> wins
    hardGames: 0,
    hardDraws: 0,
    twoPlayer: 0,
    days: [], // unique YYYY-MM-DD dates with at least one game
    history: [], // newest first
  }
}

// Points for one finished game. `result` is 'win' | 'loss' | 'draw' vs the CPU, or 'played' for 2-player.
export function pointsFor({ mode, difficulty, result }) {
  if (mode !== 'ai') return 20
  const table = {
    easy: { win: 50, draw: 10, loss: 5 },
    medium: { win: 100, draw: 30, loss: 10 },
    hard: { win: 150, draw: 60, loss: 15 },
  }
  return (table[difficulty] ?? table.medium)[result] ?? 0
}

export function dayKey(date) {
  const d = new Date(date)
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

export function longestStreak(days) {
  const sorted = [...new Set(days)].sort()
  let best = 0
  let run = 0
  let prev = null
  for (const day of sorted) {
    const t = Date.parse(`${day}T12:00:00Z`)
    run = prev !== null && Math.round((t - prev) / 86400000) === 1 ? run + 1 : 1
    best = Math.max(best, run)
    prev = t
  }
  return best
}

const distinct = (map) => Object.values(map).filter((n) => n > 0).length

// `value(stats)` measures progress toward `target`.
export const ACHIEVEMENTS = [
  { id: 'first-play', name: 'First Play', desc: 'Play your first game', category: 'general', icon: 'badge-first-play', xp: 50, target: 1, value: (s) => s.games, featured: true },
  { id: 'game-explorer', name: 'Game Explorer', desc: 'Play 10 different games', category: 'general', icon: 'badge-game-explorer', xp: 200, target: 10, value: (s) => distinct(s.played), featured: true },
  { id: 'rising-star', name: 'Rising Star', desc: 'Earn 100 total points', category: 'general', icon: 'badge-rising-star', xp: 100, target: 100, value: (s) => s.points, featured: true },
  { id: 'high-score-hero', name: 'High Score Hero', desc: 'Win in 5 different games', category: 'challenges', icon: 'badge-high-score-hero', xp: 300, target: 5, value: (s) => distinct(s.won), featured: true },
  { id: 'first-win', name: 'First Win', desc: 'Win your first game', category: 'general', icon: 'icon-star', xp: 50, target: 1, value: (s) => s.wins },
  { id: 'streak-7', name: '7 Day Streak', desc: 'Play games for 7 days in a row', category: 'general', icon: 'icon-calendar', xp: 150, target: 7, value: (s) => longestStreak(s.days) },
  { id: 'arcade-enthusiast', name: 'Arcade Enthusiast', desc: 'Play 25 arcade games', category: 'challenges', icon: 'icon-joystick', xp: 100, target: 25, value: (s, arcadeIds = []) => arcadeIds.reduce((n, id) => n + (s.played[id] ?? 0), 0) },
  { id: 'challenge-master', name: 'Challenge Master', desc: 'Finish 10 games against the Hard CPU', category: 'challenges', icon: 'icon-target', xp: 150, target: 10, value: (s) => s.hardGames },
  { id: 'ttt-pro', name: 'Tic Tac Pro', desc: 'Win 10 games of Tic Tac Toe', category: 'game', icon: 'tile-tic-tac-toe', xp: 150, target: 10, value: (s) => s.won['tic-tac-toe'] ?? 0 },
  { id: 'ttt-unbeatable', name: 'Unbeatable?', desc: 'Hold the Hard CPU to a draw in Tic Tac Toe', category: 'game', icon: 'tile-tic-tac-toe', xp: 200, target: 1, value: (s) => s.hardDraws },
  { id: 'ttt-rivals', name: 'Local Rivals', desc: 'Play a 2-player game of Tic Tac Toe', category: 'game', icon: 'tile-tic-tac-toe', xp: 50, target: 1, value: (s) => s.twoPlayer },
  { id: 'centurion', name: 'Centurion', desc: 'Earn 1,000 total points', category: 'special', icon: 'icon-crown', xp: 200, target: 1000, value: (s) => s.points },
  { id: 'game-collector', name: 'Game Collector', desc: 'Play 50 different games', category: 'special', icon: 'icon-diamond', xp: 250, target: 50, value: (s) => distinct(s.played) },
]

export const ACHIEVEMENT_CATEGORIES = [
  { id: 'general', name: 'General' },
  { id: 'game', name: 'Game Specific' },
  { id: 'challenges', name: 'Challenges' },
  { id: 'special', name: 'Special' },
]

export function achievementProgress(stats, unlocked = {}, arcadeIds = []) {
  return ACHIEVEMENTS.map((a) => {
    const current = Math.min(a.target, a.value(stats, arcadeIds))
    return { ...a, current, done: Boolean(unlocked[a.id]) || current >= a.target, unlockedAt: unlocked[a.id] ?? null }
  })
}

export function totalXp(stats, unlocked) {
  const bonus = ACHIEVEMENTS.filter((a) => unlocked[a.id]).reduce((n, a) => n + a.xp, 0)
  return stats.points + bonus
}

export function levelInfo(xp) {
  return { level: Math.floor(xp / XP_PER_LEVEL) + 1, into: xp % XP_PER_LEVEL, needed: XP_PER_LEVEL }
}

/**
 * Records one finished game. Returns the new stats and unlocked map plus the achievements
 * this game unlocked (so the UI can celebrate them).
 */
export function recordGame(stats, unlocked, game, now = Date.now(), arcadeIds = []) {
  const { gameId, mode, difficulty, result } = game
  const points = pointsFor(game)
  const s = {
    ...stats,
    games: stats.games + 1,
    wins: stats.wins + (result === 'win' ? 1 : 0),
    losses: stats.losses + (result === 'loss' ? 1 : 0),
    draws: stats.draws + (result === 'draw' ? 1 : 0),
    points: stats.points + points,
    played: { ...stats.played, [gameId]: (stats.played[gameId] ?? 0) + 1 },
    won: result === 'win' ? { ...stats.won, [gameId]: (stats.won[gameId] ?? 0) + 1 } : stats.won,
    hardGames: stats.hardGames + (mode === 'ai' && difficulty === 'hard' ? 1 : 0),
    hardDraws: stats.hardDraws + (mode === 'ai' && difficulty === 'hard' && result === 'draw' ? 1 : 0),
    twoPlayer: stats.twoPlayer + (mode === 'ai' ? 0 : 1),
    days: [...new Set([...stats.days, dayKey(now)])],
    history: [{ gameId, mode, difficulty, result, points, at: now }, ...stats.history].slice(0, 50),
  }

  const nextUnlocked = { ...unlocked }
  const newly = []
  for (const a of achievementProgress(s, unlocked, arcadeIds)) {
    if (a.current >= a.target && !unlocked[a.id]) {
      nextUnlocked[a.id] = new Date(now).toISOString()
      newly.push(a)
    }
  }
  return { stats: s, unlocked: nextUnlocked, newly, points }
}

export function winRate(stats) {
  const decided = stats.wins + stats.losses + stats.draws
  return decided ? Math.round((stats.wins / decided) * 100) : null
}

export function timeAgo(then, now = Date.now()) {
  const s = Math.max(0, Math.round((now - then) / 1000))
  if (s < 60) return 'just now'
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`
  return `${Math.floor(s / 86400)}d ago`
}
