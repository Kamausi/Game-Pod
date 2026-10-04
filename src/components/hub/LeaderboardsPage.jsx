import { useState } from 'react'
import { avatarSrc, podiumSrc } from '../../avatars.js'
import { useStore } from '../../store.jsx'
import { EmptyState, Header, formatNumber } from './common.jsx'
import { Icon } from './icons.jsx'

// Sample standings from the mockup. There is no online service yet, so these are clearly
// labeled as sample players; only the "You" row is real.
const ALL_TIME = [
  ['gameking', 'GameKing', 58, 2158430, 0],
  ['pixelplayz', 'PixelPlayz', 52, 1984210, 0],
  ['ninjanate', 'NinjaNate', 49, 1762990, 0],
  ['lunastars', 'LunaStars', 47, 1620441, 2],
  ['playrocket', 'PlayRocket', 46, 1518320, -1],
  ['tetratom', 'TetraTom', 44, 1402881, 3],
  ['joystickjen', 'JoystickJen', 43, 1391204, -1],
  ['blockboss', 'BlockBoss', 41, 1284662, 0],
  ['cardshark', 'CardShark', 40, 1221508, 4],
  ['diceduel', 'DiceDueler', 39, 1198330, -2],
]
const THIS_WEEK = [
  ['lunastars', 'LunaStars', 47, 48210, 3],
  ['gameking', 'GameKing', 58, 46980, -1],
  ['cardshark', 'CardShark', 40, 41377, 6],
  ['pixelplayz', 'PixelPlayz', 52, 39120, -2],
  ['tetratom', 'TetraTom', 44, 35644, 1],
  ['ninjanate', 'NinjaNate', 49, 33902, -3],
  ['diceduel', 'DiceDueler', 39, 30155, 4],
  ['blockboss', 'BlockBoss', 41, 28470, 0],
  ['joystickjen', 'JoystickJen', 43, 26318, -2],
  ['playrocket', 'PlayRocket', 46, 25090, -4],
]

const TABS = [
  { id: 'global', name: 'Global', icon: Icon.globe, rows: ALL_TIME },
  { id: 'friends', name: 'Friends', icon: Icon.users, rows: null },
  { id: 'week', name: 'This Week', icon: Icon.calendar, rows: THIS_WEEK },
  { id: 'all', name: 'All Time', icon: Icon.trophy, rows: ALL_TIME },
]

const toRows = (raw) => raw.map(([avatar, name, level, points, move], i) => ({ rank: i + 1, avatar, name, level, points, move }))

function Move({ move }) {
  if (move === null) return <span className="hs-move" />
  if (!move) return <span className="hs-move">–</span>
  return (
    <span className={`hs-move ${move > 0 ? 'up' : 'down'}`}>
      {move > 0 ? Icon.up : Icon.down}
      {Math.abs(move)}
    </span>
  )
}

function LevelPill({ level }) {
  return (
    <span className={`hs-lvpill ${level >= 45 || level % 3 === 1 ? 'purple' : 'blue'}`}>
      {Icon.star} Lv. {level}
    </span>
  )
}

function Row({ row, you }) {
  return (
    <li className={`hs-lrow ${you ? 'you' : ''} ${row.rank && row.rank <= 3 ? `top${row.rank}` : ''}`}>
      <span className="hs-lrank">{row.rank ?? '—'}</span>
      <span className="hs-lplayer">
        <img src={avatarSrc(row.avatar)} alt="" />
        <span>
          {row.name}
          {row.rank === 1 && <i className="hs-lcrown">{Icon.crown}</i>}
        </span>
      </span>
      <LevelPill level={row.level} />
      <span className="hs-lpoints">
        <i>{Icon.trophy}</i>
        {formatNumber(row.points)}
      </span>
      <Move move={row.move} />
    </li>
  )
}

export default function LeaderboardsPage({ header }) {
  const { profile, level, stats } = useStore()
  const [tab, setTab] = useState('global')
  const current = TABS.find((t) => t.id === tab)
  const rows = current.rows && toRows(current.rows)
  const weekStart = Date.now() - 7 * 86400000
  const myPoints = tab === 'week' ? stats.history.filter((h) => h.at >= weekStart).reduce((n, h) => n + h.points, 0) : stats.points
  const you = { rank: null, avatar: profile.avatar, name: 'You', level: level.level, points: myPoints, move: null }

  return (
    <>
      <Header variant="page" title="Leaderboards" subtitle="Compete. Climb. Play more." {...header} />
      <div className="hs-segment" role="tablist" aria-label="Leaderboard">
        {TABS.map((t) => (
          <button key={t.id} type="button" role="tab" aria-selected={tab === t.id} className={tab === t.id ? 'on' : ''} onClick={() => setTab(t.id)}>
            {t.icon}
            {t.name}
          </button>
        ))}
      </div>

      {rows ? (
        <>
          <p className="hs-note">{Icon.info} Sample players for now. Online leaderboards are coming soon; your points are real.</p>
          <div className="hs-podium">
            {[1, 0, 2].map((i) => (
              <div key={rows[i].rank} className={`hs-podium-spot place${rows[i].rank}`}>
                <img src={podiumSrc(rows[i].rank)} alt="" />
                <div className="hs-podium-base">
                  <strong>{rows[i].name}</strong>
                  <span>
                    <i>{Icon.trophy}</i>
                    {formatNumber(rows[i].points)}
                  </span>
                </div>
              </div>
            ))}
          </div>
          <div className="hs-ltable">
            <div className="hs-lhead" aria-hidden="true">
              <span>#</span>
              <span>Player</span>
              <span>Level</span>
              <span>{tab === 'week' ? 'Points This Week' : 'Total Points'}</span>
              <span />
            </div>
            <ol className="hs-lrows">
              {rows.map((r) => (
                <Row key={r.name} row={r} />
              ))}
            </ol>
          </div>
          <ol className="hs-lrows hs-lyou">
            <Row row={you} you />
          </ol>
        </>
      ) : (
        <EmptyState icon={Icon.users} title="Friends" text="Add friends and compare scores. Friends are coming soon." />
      )}
    </>
  )
}
