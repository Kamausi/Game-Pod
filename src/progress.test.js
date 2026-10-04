import { test } from 'node:test'
import assert from 'node:assert/strict'
import {
  emptyStats, pointsFor, longestStreak, recordGame, levelInfo, totalXp, winRate, achievementProgress, timeAgo,
} from './progress.js'

const ttt = (result, extra = {}) => ({ gameId: 'tic-tac-toe', mode: 'ai', difficulty: 'medium', result, ...extra })

test('points depend on mode, difficulty and result', () => {
  assert.equal(pointsFor(ttt('win')), 100)
  assert.equal(pointsFor(ttt('draw', { difficulty: 'hard' })), 60)
  assert.equal(pointsFor({ mode: 'pvp', result: 'played' }), 20)
})

test('longest streak counts consecutive days only', () => {
  assert.equal(longestStreak([]), 0)
  assert.equal(longestStreak(['2026-10-01', '2026-10-02', '2026-10-03', '2026-10-05']), 3)
  assert.equal(longestStreak(['2026-12-31', '2027-01-01', '2026-12-31']), 2)
})

test('first win unlocks First Play, First Win and Rising Star once', () => {
  const now = Date.parse('2026-10-04T10:00:00Z')
  let r = recordGame(emptyStats(), {}, ttt('win'), now)
  assert.deepEqual(r.newly.map((a) => a.id).sort(), ['first-play', 'first-win', 'rising-star'])
  assert.equal(r.stats.points, 100)
  assert.equal(r.stats.won['tic-tac-toe'], 1)
  assert.equal(r.stats.history[0].result, 'win')
  r = recordGame(r.stats, r.unlocked, ttt('loss'), now + 1000)
  assert.deepEqual(r.newly, [])
  assert.equal(r.stats.losses, 1)
})

test('hard draws and 2-player games unlock their achievements', () => {
  let r = recordGame(emptyStats(), {}, ttt('draw', { difficulty: 'hard' }))
  assert.ok(r.newly.some((a) => a.id === 'ttt-unbeatable'))
  r = recordGame(r.stats, r.unlocked, { gameId: 'tic-tac-toe', mode: 'pvp', result: 'played' })
  assert.ok(r.newly.some((a) => a.id === 'ttt-rivals'))
  assert.equal(r.stats.games, 2)
})

test('XP adds achievement rewards to points and drives the level', () => {
  const r = recordGame(emptyStats(), {}, ttt('win'))
  assert.equal(totalXp(r.stats, r.unlocked), 100 + 50 + 50 + 100)
  assert.deepEqual(levelInfo(0), { level: 1, into: 0, needed: 500 })
  assert.deepEqual(levelInfo(1250), { level: 3, into: 250, needed: 500 })
})

test('win rate and progress clamp sensibly', () => {
  assert.equal(winRate(emptyStats()), null)
  const r = recordGame(emptyStats(), {}, ttt('win'))
  assert.equal(winRate(r.stats), 100)
  const centurion = achievementProgress(r.stats, r.unlocked).find((a) => a.id === 'centurion')
  assert.equal(centurion.current, 100)
  assert.equal(centurion.done, false)
})

test('history is capped at 50 entries, newest first', () => {
  let r = { stats: emptyStats(), unlocked: {} }
  for (let i = 0; i < 55; i++) r = recordGame(r.stats, r.unlocked, ttt('loss'), 1000 + i)
  assert.equal(r.stats.history.length, 50)
  assert.equal(r.stats.history[0].at, 1054)
})

test('timeAgo formats', () => {
  assert.equal(timeAgo(0, 30_000), 'just now')
  assert.equal(timeAgo(0, 5 * 3600_000), '5h ago')
  assert.equal(timeAgo(0, 2 * 86400_000), '2d ago')
})
