import { useEffect, useRef, useState } from 'react'
import { AVATARS } from '../../avatars.js'
import { sfx } from '../../audio.js'
import { useStore } from '../../store.jsx'
import { Sheet } from './common.jsx'
import { Icon } from './icons.jsx'
import HomePage from './HomePage.jsx'
import AllGamesPage from './AllGamesPage.jsx'
import AchievementsPage from './AchievementsPage.jsx'
import LeaderboardsPage from './LeaderboardsPage.jsx'
import ProfilePage from './ProfilePage.jsx'
import SettingsPage from './SettingsPage.jsx'
import './hub.css'
import './pages.css'

const TABS = [
  { id: 'home', label: 'Home', icon: Icon.home },
  { id: 'all', label: 'All Games', icon: Icon.grid },
  { id: 'achievements', label: 'Achievements', icon: Icon.trophy },
  { id: 'leaderboards', label: 'Leaderboards', icon: Icon.chart },
  { id: 'profile', label: 'Profile', icon: Icon.user },
]

function EditProfileSheet({ onClose }) {
  const { profile, setProfile } = useStore()
  const [form, setForm] = useState(profile)
  const set = (k) => (e) => setForm((f) => ({ ...f, [k]: e.target.value }))
  const save = (e) => {
    e.preventDefault()
    setProfile({
      name: form.name.trim() || 'Player',
      handle: form.handle.trim().replace(/[^a-zA-Z0-9_]/g, '') || 'player',
      bio: form.bio.trim(),
    })
    onClose()
  }
  return (
    <Sheet title="Edit Profile" onClose={onClose}>
      <form className="hs-form" onSubmit={save}>
        <label>
          Display name
          <input value={form.name} onChange={set('name')} maxLength={20} autoFocus />
        </label>
        <label>
          Username
          <span className="hs-at">
            @<input value={form.handle} onChange={set('handle')} maxLength={20} />
          </span>
        </label>
        <label>
          Bio
          <textarea value={form.bio} onChange={set('bio')} maxLength={80} rows={2} />
        </label>
        <button type="submit" className="hs-sheet-btn">
          Save
        </button>
      </form>
    </Sheet>
  )
}

function AvatarSheet({ onClose }) {
  const { profile, setProfile } = useStore()
  return (
    <Sheet title="Change Avatar" onClose={onClose}>
      <div className="hs-avatars">
        {AVATARS.map((a) => (
          <button
            key={a.id}
            type="button"
            className={profile.avatar === a.id ? 'on' : ''}
            aria-pressed={profile.avatar === a.id}
            onClick={() => {
              setProfile({ avatar: a.id })
              onClose()
            }}
          >
            <img src={a.src} alt={a.id} />
          </button>
        ))}
      </div>
    </Sheet>
  )
}

function InfoSheet({ title, onClose, children }) {
  return (
    <Sheet title={title} onClose={onClose}>
      <div className="hs-info">{children}</div>
      <button type="button" className="hs-sheet-btn ghost" onClick={onClose}>
        Close
      </button>
    </Sheet>
  )
}

export default function Hub({ tab, setTab, onPlay, onTitle, notify }) {
  const { resetProgress } = useStore()
  const [browse, setBrowse] = useState(null) // category filter for All Games
  const [sheet, setSheet] = useState(null)
  const scrollRef = useRef(null)

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: 0 })
  }, [tab])

  const go = (t) => {
    sfx('tap')
    setTab(t)
  }
  const open = (game) => {
    sfx('tap')
    if (game.playable) onPlay(game.id)
    else notify(`${game.name} is coming soon`)
  }
  const onBrowse = (category) => {
    setBrowse(category)
    go('all')
  }

  const header = { onProfile: () => go('profile'), onSettings: () => go('settings') }
  const sheets = {
    edit: () => setSheet('edit'),
    avatar: () => setSheet('avatar'),
  }

  return (
    <div className="home-screen">
      <div className="hs-column" ref={scrollRef}>
        {tab === 'home' && <HomePage header={header} onOpen={open} onBrowse={onBrowse} />}
        {tab === 'all' && <AllGamesPage header={header} onOpen={open} category={browse} setCategory={setBrowse} />}
        {tab === 'achievements' && <AchievementsPage header={header} />}
        {tab === 'leaderboards' && <LeaderboardsPage header={header} />}
        {tab === 'profile' && (
          <ProfilePage
            header={header}
            onOpen={open}
            onEditProfile={sheets.edit}
            onChangeAvatar={sheets.avatar}
            onBrowse={onBrowse}
            onAchievements={() => go('achievements')}
            onLeaderboards={() => go('leaderboards')}
          />
        )}
        {tab === 'settings' && (
          <SettingsPage
            header={header}
            notify={notify}
            onEditProfile={sheets.edit}
            onChangeAvatar={sheets.avatar}
            onPrivacy={() => setSheet('privacy')}
            onHelp={() => setSheet('help')}
            onAbout={() => setSheet('about')}
            onReset={() => setSheet('reset')}
            onTitle={onTitle}
          />
        )}
      </div>

      <nav className="hs-nav" aria-label="Main">
        {TABS.map((t) => (
          <button key={t.id} type="button" className={tab === t.id ? 'on' : ''} aria-current={tab === t.id ? 'page' : undefined} onClick={() => go(t.id)}>
            {t.icon}
            <span>{t.label}</span>
          </button>
        ))}
      </nav>

      {sheet === 'edit' && <EditProfileSheet onClose={() => setSheet(null)} />}
      {sheet === 'avatar' && <AvatarSheet onClose={() => setSheet(null)} />}
      {sheet === 'privacy' && (
        <InfoSheet title="Privacy & Security" onClose={() => setSheet(null)}>
          <p>Game Pod has no account or server yet. Your profile, settings, scores and achievements are stored only in this browser on this device.</p>
          <p>Nothing is sent anywhere. Clearing your browser's site data, or Reset Progress, removes it.</p>
        </InfoSheet>
      )}
      {sheet === 'help' && (
        <InfoSheet title="Help Center" onClose={() => setSheet(null)}>
          <h3>How do I earn XP?</h3>
          <p>Every finished game earns points (more for wins and harder CPU levels), and each achievement adds bonus XP. Every 500 XP is a new level.</p>
          <h3>Which games can I play?</h3>
          <p>Tic Tac Toe is ready now. Games marked “Soon” are on the way.</p>
          <h3>Can I move my progress to another device?</h3>
          <p>Not yet. Cloud sync is coming; for now progress lives on this device.</p>
        </InfoSheet>
      )}
      {sheet === 'about' && (
        <InfoSheet title="About Game Pod" onClose={() => setSheet(null)}>
          <p>Game Pod 1.0.0: a pocket arcade of quick games.</p>
          <p>Built with React, Three.js and WebGL. Sounds and music are generated live in your browser.</p>
        </InfoSheet>
      )}
      {sheet === 'reset' && (
        <Sheet title="Reset Progress?" onClose={() => setSheet(null)}>
          <p className="hs-info">This clears your points, level, achievements, play history and favorites. Your profile and settings stay. This can't be undone.</p>
          <button
            type="button"
            className="hs-sheet-btn danger"
            onClick={() => {
              resetProgress()
              setSheet(null)
              notify('Progress reset')
            }}
          >
            Reset Progress
          </button>
          <button type="button" className="hs-sheet-btn ghost" onClick={() => setSheet(null)}>
            Cancel
          </button>
        </Sheet>
      )}
    </div>
  )
}
