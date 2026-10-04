import { avatarSrc } from '../../avatars.js'
import { useStore } from '../../store.jsx'
import { Header, Toggle } from './common.jsx'
import { Icon } from './icons.jsx'

function Row({ icon, label, hint, children, onClick }) {
  const body = (
    <>
      <span className="hs-set-icon">{icon}</span>
      <span className="hs-set-label">
        {label}
        {hint && <small>{hint}</small>}
      </span>
      <span className="hs-set-control">{children}</span>
    </>
  )
  return onClick ? (
    <button type="button" className="hs-set-row link" onClick={onClick}>
      {body}
      <span className="hs-arow-chev">{Icon.chevron}</span>
    </button>
  ) : (
    <div className="hs-set-row">{body}</div>
  )
}

function Group({ icon, title, children }) {
  return (
    <section className="hs-panel hs-set-group">
      <h2>
        {icon}
        {title}
      </h2>
      {children}
    </section>
  )
}

function Tile({ tone, icon, label, onClick }) {
  return (
    <button type="button" className={`hs-set-tile ${tone}`} onClick={onClick}>
      {icon}
      <span>{label}</span>
      {Icon.chevron}
    </button>
  )
}

export default function SettingsPage({ header, notify, onEditProfile, onChangeAvatar, onPrivacy, onHelp, onAbout, onReset, onTitle }) {
  const { profile, settings, setSetting, reduceMotion } = useStore()
  const soon = (what) => () => notify(`${what} ${what.endsWith('s') ? 'are' : 'is'} coming soon`)

  return (
    <>
      <Header variant="page" title="Settings" subtitle="Customize your Game Pod experience." settingsActive {...header} />

      <Group icon={Icon.user} title="Account">
        <button type="button" className="hs-account" onClick={onEditProfile}>
          <img src={avatarSrc(profile.avatar)} alt="" />
          <span>
            <strong>{profile.name}</strong>
            <small>@{profile.handle}</small>
            <small>Guest · progress saved on this device</small>
          </span>
          {Icon.chevron}
        </button>
        <div className="hs-set-tiles">
          <Tile tone="blue" icon={Icon.pencil} label="Edit Profile" onClick={onEditProfile} />
          <Tile tone="purple" icon={Icon.image} label="Change Avatar" onClick={onChangeAvatar} />
          <Tile tone="gold" icon={Icon.link} label="Linked Accounts" onClick={soon('Linked accounts')} />
          <Tile tone="green" icon={Icon.shield} label="Privacy & Security" onClick={onPrivacy} />
        </div>
      </Group>

      <Group icon={Icon.gear} title="Preferences">
        <Row icon={Icon.speaker} label="Sound Effects">
          <Toggle checked={settings.sound} onChange={(v) => setSetting('sound', v)} label="Sound effects" />
        </Row>
        <Row icon={Icon.music} label="Background Music">
          <input
            type="range"
            className="hs-range"
            min="0"
            max="100"
            value={Math.round(settings.volume * 100)}
            onChange={(e) => setSetting('volume', Number(e.target.value) / 100)}
            aria-label="Music volume"
            disabled={!settings.music}
            style={{ '--v': `${settings.volume * 100}%` }}
          />
          <span className="hs-range-val">{Math.round(settings.volume * 100)}%</span>
          <Toggle checked={settings.music} onChange={(v) => setSetting('music', v)} label="Background music" />
        </Row>
        <Row icon={Icon.phone} label="Haptic Feedback" hint={'vibrate' in navigator ? null : 'Not supported on this device'}>
          <Toggle checked={settings.haptics} onChange={(v) => setSetting('haptics', v)} label="Haptic feedback" />
        </Row>
        <Row icon={Icon.palette} label="Theme">
          <div className="hs-theme" role="radiogroup" aria-label="Theme">
            <button type="button" role="radio" aria-checked="false" onClick={soon('Light theme')}>
              {Icon.sun} Light
            </button>
            <button type="button" role="radio" aria-checked="true" className="on">
              {Icon.moon} Dark
            </button>
            <button type="button" role="radio" aria-checked="false" onClick={soon('System theme')}>
              {Icon.monitor} System
            </button>
          </div>
        </Row>
        <Row icon={Icon.motion} label="Reduce Motion" hint={settings.reduceMotion === null ? 'Following your device setting' : null}>
          <Toggle checked={reduceMotion} onChange={(v) => setSetting('reduceMotion', v)} label="Reduce motion" />
        </Row>
        <Row icon={Icon.globe} label="Language" onClick={soon('More languages')}>
          English
        </Row>
      </Group>

      <Group icon={<span className="hs-set-pad">{Icon.grid}</span>} title="Gameplay">
        <Row icon={Icon.bulb} label="Move Hints" hint="Glows a good move if you pause">
          <Toggle checked={settings.hints} onChange={(v) => setSetting('hints', v)} label="Move hints" />
        </Row>
        <Row icon={Icon.book} label="Show Tutorials" hint="How-to-play card on first visit">
          <Toggle checked={settings.tutorials} onChange={(v) => setSetting('tutorials', v)} label="Show tutorials" />
        </Row>
        <Row icon={Icon.play} label="Replay Intro" onClick={onTitle} />
        <Row icon={Icon.reset} label="Reset Progress" onClick={onReset} />
        <Row icon={Icon.cloud} label="Data & Cloud Sync" onClick={() => notify('Cloud sync is coming soon. Progress is saved on this device.')} />
      </Group>

      <Group icon={Icon.help} title="Support">
        <div className="hs-set-tiles">
          <Tile tone="blue" icon={Icon.headset} label="Help Center" onClick={onHelp} />
          <Tile tone="purple" icon={Icon.chat} label="Send Feedback" onClick={soon('Feedback')} />
          <Tile tone="gold" icon={Icon.bug} label="Report a Bug" onClick={soon('Bug reports')} />
          <Tile tone="red" icon={Icon.info} label="About Game Pod" onClick={onAbout} />
        </div>
      </Group>

      <div className="hs-panel hs-set-footer">
        <button type="button" className="hs-signout" onClick={() => notify("You're playing as a guest. Accounts are coming soon.")}>
          {Icon.signout} Sign Out
        </button>
        <div>
          <span>Version 1.0.0</span>
          <button type="button" onClick={onPrivacy}>
            Privacy
          </button>
        </div>
      </div>
    </>
  )
}
