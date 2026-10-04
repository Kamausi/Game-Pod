import { useState } from 'react'
import { ACHIEVEMENTS, ACHIEVEMENT_CATEGORIES, achievementProgress } from '../../progress.js'
import { ARCADE_IDS } from '../../games.js'
import { useStore } from '../../store.jsx'
import Badge, { Trophy } from '../../art/Badge.jsx'
import { AchievementBadge, Header, ProgressBar, Section, Sheet, formatDate } from './common.jsx'
import { Icon } from './icons.jsx'

const BAR_COLORS = ['gold', 'blue', 'purple', 'green', 'orange', 'pink']
const CARD_TONES = ['purple', 'gold', 'violet', 'green']
const CHIP_ICONS = { general: ['star', 'gold'], game: ['xo', 'blue'], challenges: ['target', 'green'], special: ['crown', 'red'] }

const SORTS = { progress: 'Progress', name: 'Name', xp: 'XP' }

export function useAchievements() {
  const { stats, unlocked } = useStore()
  return achievementProgress(stats, unlocked, ARCADE_IDS)
}

export function LevelCard() {
  const { level } = useStore()
  const list = useAchievements()
  const done = list.filter((a) => a.done).length
  return (
    <div className="hs-levelcard">
      <Badge icon="crown" tone="gold" className="hs-levelcard-emblem" />
      <div className="hs-levelcard-main">
        <span className="hs-levelcard-label">Achievement Level</span>
        <div className="hs-levelcard-row">
          <strong>Lv. {level.level}</strong>
          <span>
            {level.into} / {level.needed} XP
          </span>
        </div>
        <ProgressBar value={level.into} max={level.needed} />
      </div>
      <div className="hs-levelcard-total">
        <Trophy className="hs-levelcard-trophy" />
        <div>
          <span>Total Achievements</span>
          <strong>
            {done}
            <small> / {list.length}</small>
          </strong>
          <span>{Math.round((done / list.length) * 100)}% Complete</span>
        </div>
      </div>
    </div>
  )
}

export function AchievementCard({ a, tone, date }) {
  return (
    <div className={`hs-acard hs-acard-${tone} ${a.done ? 'done' : ''}`}>
      <AchievementBadge a={a} className="hs-acard-badge" />
      <h3>{a.name}</h3>
      <p>{a.desc}</p>
      {date ? (
        <span className="hs-acard-date">{formatDate(date)}</span>
      ) : a.done ? (
        <span className="hs-completed">{Icon.check} Completed</span>
      ) : (
        <div className="hs-acard-progress">
          <ProgressBar value={a.current} max={a.target} color={tone === 'gold' ? 'blue' : tone === 'green' ? 'green' : 'purple'} />
          <span>
            {a.current} / {a.target}
          </span>
        </div>
      )}
    </div>
  )
}

export default function AchievementsPage({ header }) {
  const list = useAchievements()
  const [filter, setFilter] = useState(null)
  const [sort, setSort] = useState('progress')
  const [detail, setDetail] = useState(null)

  const featured = list.filter((a) => a.featured)
  const shown = list
    .filter((a) => !filter || a.category === filter)
    .sort((a, b) => {
      if (sort === 'name') return a.name.localeCompare(b.name)
      if (sort === 'xp') return b.xp - a.xp
      return b.done - a.done || b.current / b.target - a.current / a.target
    })

  return (
    <>
      <Header variant="page" title="Achievements" subtitle="Play games. Earn achievements. Show what you're made of." {...header} />
      <LevelCard />

      <div className="hs-pills" role="tablist" aria-label="Filter achievements">
        {[{ id: null, name: 'All' }, ...ACHIEVEMENT_CATEGORIES].map((c) => (
          <button key={c.name} type="button" role="tab" aria-selected={filter === c.id} className={filter === c.id ? 'on' : ''} onClick={() => setFilter(c.id)}>
            {c.id ? <Badge icon={CHIP_ICONS[c.id][0]} tone={CHIP_ICONS[c.id][1]} wings={false} className="hs-pill-badge" /> : <span className="hs-pill-svg">{Icon.grid}</span>}
            {c.name}
          </button>
        ))}
      </div>

      <Section title="Featured Achievements">
        <div className="hs-row hs-row-acards">
          {featured.map((a, i) => (
            <AchievementCard key={a.id} a={a} tone={CARD_TONES[i % CARD_TONES.length]} />
          ))}
        </div>
      </Section>

      <Section
        title="All Achievements"
        action={
          <label className="hs-select">
            <span className="sr-only">Sort achievements</span>
            <select value={sort} onChange={(e) => setSort(e.target.value)}>
              {Object.entries(SORTS).map(([id, name]) => (
                <option key={id} value={id}>
                  {name}
                </option>
              ))}
            </select>
            {Icon.chevronDown}
          </label>
        }
      >
        <ul className="hs-alist">
          {shown.map((a) => (
            <li key={a.id}>
              <button type="button" className={`hs-arow ${a.done ? 'done' : ''}`} onClick={() => setDetail(a)}>
                <span className="hs-arow-icon">
                  <AchievementBadge a={a} />
                </span>
                <span className="hs-arow-text">
                  <strong>{a.name}</strong>
                  <small>{a.desc}</small>
                  <span className="hs-arow-bar">
                    <ProgressBar value={a.current} max={a.target} color={BAR_COLORS[ACHIEVEMENTS.findIndex((x) => x.id === a.id) % BAR_COLORS.length]} />
                    <em>
                      {a.current} / {a.target}
                    </em>
                  </span>
                </span>
                <span className="hs-xp">+{a.xp} XP</span>
                <span className={`hs-status ${a.done ? 'done' : ''}`}>{a.done ? <>{Icon.check} Completed</> : 'In Progress'}</span>
                <span className="hs-arow-chev">{Icon.chevron}</span>
              </button>
            </li>
          ))}
        </ul>
      </Section>

      {detail && (
        <Sheet title={detail.name} onClose={() => setDetail(null)}>
          <div className="hs-adetail">
            <AchievementBadge a={detail} className="hs-adetail-badge" />
            <p>{detail.desc}</p>
            <ProgressBar value={detail.current} max={detail.target} />
            <span>
              {detail.current} / {detail.target} · +{detail.xp} XP
            </span>
            <strong className={detail.done ? 'done' : ''}>
              {detail.unlockedAt ? `Unlocked ${formatDate(detail.unlockedAt)}` : detail.done ? 'Completed' : 'Not unlocked yet'}
            </strong>
          </div>
        </Sheet>
      )}
    </>
  )
}
