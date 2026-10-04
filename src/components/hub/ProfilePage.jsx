import { useEffect, useState } from 'react'
import Avatar from '../../art/Avatar.jsx'
import Badge from '../../art/Badge.jsx'
import { GameArt, Sprite } from '../../art/Sprite.jsx'
import { gameById } from '../../games.js'
import { timeAgo, winRate } from '../../progress.js'
import { useStore } from '../../store.jsx'
import { AchievementCard, useAchievements } from './AchievementsPage.jsx'
import { EmptyState, GameTile, Header, ProgressBar, Section, formatNumber } from './common.jsx'
import { Icon } from './icons.jsx'

const RESULT_LABEL = { win: 'Win', loss: 'Loss', draw: 'Draw', played: '2P' }
const TONES = ['purple', 'gold', 'violet', 'green']

function useOnline() {
  const [online, setOnline] = useState(() => navigator.onLine)
  useEffect(() => {
    const update = () => setOnline(navigator.onLine)
    window.addEventListener('online', update)
    window.addEventListener('offline', update)
    return () => {
      window.removeEventListener('online', update)
      window.removeEventListener('offline', update)
    }
  }, [])
  return online
}

// Tags earned from how the player actually plays.
function playerTags(stats, achievementsDone) {
  const tags = []
  if (stats.hardGames > 0) tags.push({ sprite: 'gamepad', label: 'Competitive' })
  if (achievementsDone >= 3) tags.push({ sprite: 'trophy', label: 'Achievement Hunter' })
  if (stats.twoPlayer > 0) tags.push({ svg: Icon.users, label: 'Social Player' })
  if (!tags.length) tags.push({ svg: Icon.star, label: 'Newcomer' })
  return tags
}

export default function ProfilePage({ header, onOpen, onEditProfile, onChangeAvatar, onBrowse, onAchievements, onLeaderboards }) {
  const { profile, level, stats, favorites } = useStore()
  const online = useOnline()
  const [allHistory, setAllHistory] = useState(false)
  const list = useAchievements()
  const done = list.filter((a) => a.done)
  const recent = done.filter((a) => a.unlockedAt).sort((a, b) => b.unlockedAt.localeCompare(a.unlockedAt)).slice(0, 4)
  const rate = winRate(stats)
  const history = allHistory ? stats.history : stats.history.slice(0, 5)

  return (
    <>
      <Header variant="page" title="My Profile" subtitle="Play. Compete. Earn. Level Up." {...header} />

      <div className="hs-panel hs-profile">
        <div className="hs-profile-top">
          <div className="hs-bigavatar">
            <Avatar id={profile.avatar} />
            <button type="button" className="hs-bigavatar-edit" onClick={onChangeAvatar} aria-label="Change avatar">
              {Icon.pencil}
            </button>
          </div>
          <div className="hs-profile-info">
            <div className="hs-profile-name">
              <h2>{profile.name}</h2>
              {level.level >= 10 && <i className="hs-namecrown">{Icon.crown}</i>}
            </div>
            <span className="hs-handle">@{profile.handle}</span>
            <span className={`hs-online ${online ? '' : 'off'}`}>{online ? 'Online' : 'Offline'}</span>
            <p>{profile.bio}</p>
          </div>
          <button type="button" className="hs-outline-btn" onClick={onEditProfile}>
            {Icon.pencil} Edit Profile
          </button>
        </div>
        <div className="hs-tags">
          {playerTags(stats, done.length).map((t) => (
            <span key={t.label} className="hs-tag">
              {t.sprite ? <Sprite name={t.sprite} size={96} /> : t.svg}
              {t.label}
            </span>
          ))}
        </div>

        <div className="hs-levelbar">
          <Badge icon="crown" tone="gold" wings={false} className="hs-levelbar-badge" />
          <strong>Lv. {level.level}</strong>
          <ProgressBar value={level.into} max={level.needed} />
          <span>
            {level.into} / {level.needed} XP
          </span>
        </div>

        <div className="hs-stats">
          <button type="button" className="hs-stat" onClick={onAchievements}>
            <Sprite name="trophy" size={128} />
            <strong>{done.length}</strong>
            <span>Total Achievements</span>
          </button>
          <div className="hs-stat">
            <Sprite name="gamepad" size={128} />
            <strong>{formatNumber(stats.games)}</strong>
            <span>Games Played</span>
          </div>
          <div className="hs-stat">
            <Sprite name="target" size={128} />
            <strong>{rate === null ? '—' : `${rate}%`}</strong>
            <span>Win Rate</span>
          </div>
          <button type="button" className="hs-stat" onClick={onLeaderboards}>
            <span className="hs-stat-svg">{Icon.users}</span>
            <strong>—</strong>
            <span>Global Rank</span>
          </button>
        </div>
      </div>

      <Section title="Recent Achievements" icon={Icon.trophy} onSeeAll={onAchievements}>
        {recent.length ? (
          <div className="hs-row hs-row-acards">
            {recent.map((a, i) => (
              <AchievementCard key={a.id} a={a} tone={TONES[i]} date={a.unlockedAt} />
            ))}
          </div>
        ) : (
          <p className="hs-none">Play a game to earn your first achievement.</p>
        )}
      </Section>

      <Section title="Favorite Games" icon={<span className="hs-red">{Icon.heartFill}</span>} onSeeAll={() => onBrowse(null)}>
        {favorites.length ? (
          <div className="hs-row hs-row-favs">
            {favorites.map(gameById).filter(Boolean).map((g) => (
              <GameTile key={g.id} game={g} size="small" onOpen={onOpen} />
            ))}
          </div>
        ) : (
          <p className="hs-none">Tap the ♥ on any game in All Games to keep it here.</p>
        )}
      </Section>

      <Section
        title="Play History"
        icon={Icon.clock}
        onSeeAll={stats.history.length > 5 && !allHistory ? () => setAllHistory(true) : undefined}
      >
        {stats.history.length ? (
          <ul className="hs-panel hs-history">
            {history.map((h) => {
              const g = gameById(h.gameId)
              return (
                <li key={h.at}>
                  <button type="button" onClick={() => onOpen(g)}>
                    <GameArt game={g} className="hs-history-art" />
                    <strong>{g.name}</strong>
                    <span className={`hs-result ${h.result}`}>{RESULT_LABEL[h.result]}</span>
                    <span className="hs-pts">+{h.points} pts</span>
                    <span className="hs-ago">{timeAgo(h.at)}</span>
                    <span className="hs-arow-chev">{Icon.chevron}</span>
                  </button>
                </li>
              )
            })}
          </ul>
        ) : (
          <EmptyState icon={Icon.clock} title="No games yet" text="Your games will show up here." />
        )}
      </Section>
    </>
  )
}
