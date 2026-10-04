// Stroke icons (24px grid). Add `className="solid"` to fill instead of stroke.
const S = ({ children, solid }) => (
  <svg viewBox="0 0 24 24" aria-hidden="true" className={solid ? 'icon solid' : 'icon'}>
    {children}
  </svg>
)

export const Icon = {
  search: <S><circle cx="10.5" cy="10.5" r="6.5" /><path d="M15.5 15.5 21 21" /></S>,
  sliders: (
    <S>
      <path d="M3 6h18M3 12h18M3 18h18" />
      <circle cx="15" cy="6" r="2.2" className="fill" /><circle cx="8" cy="12" r="2.2" className="fill" /><circle cx="16" cy="18" r="2.2" className="fill" />
    </S>
  ),
  crown: <S solid><path d="M3 7l4.5 4L12 4l4.5 7L21 7l-2 12H5L3 7z" /></S>,
  play: <S solid><path d="M7 4.5v15l13-7.5z" /></S>,
  clock: <S><circle cx="12" cy="12" r="8.5" /><path d="M12 7.5V12l3 2" /></S>,
  chevron: <S><path d="M9 5l7 7-7 7" /></S>,
  chevronDown: <S><path d="M6 9l6 6 6-6" /></S>,
  home: <S solid><path d="M12 3 2.5 11h2.5v9.5h5.5v-6h3v6H19V11h2.5z" /></S>,
  grid: (
    <S solid>
      <rect x="3" y="3" width="8" height="8" rx="2" /><rect x="13" y="3" width="8" height="8" rx="2" />
      <rect x="3" y="13" width="8" height="8" rx="2" /><rect x="13" y="13" width="8" height="8" rx="2" />
    </S>
  ),
  trophy: (
    <S solid>
      <path d="M6 3h12v2h3v3a4 4 0 0 1-4 4h-.3A6 6 0 0 1 13 15.9V18h3v3H8v-3h3v-2.1A6 6 0 0 1 7.3 12H7a4 4 0 0 1-4-4V5h3V3zm0 4H5v1a2 2 0 0 0 1 1.7V7zm12 0v2.7A2 2 0 0 0 19 8V7h-1z" />
    </S>
  ),
  chart: (
    <S solid>
      <rect x="3" y="12" width="5" height="9" rx="1.5" /><rect x="9.5" y="4" width="5" height="17" rx="1.5" /><rect x="16" y="9" width="5" height="12" rx="1.5" />
    </S>
  ),
  user: <S solid><circle cx="12" cy="7.5" r="4.5" /><path d="M3.5 21a8.5 8.5 0 0 1 17 0z" /></S>,
  users: (
    <S solid>
      <circle cx="9" cy="8" r="3.6" /><path d="M2 20a7 7 0 0 1 14 0z" /><circle cx="17" cy="9" r="2.8" opacity=".75" /><path d="M16.5 13.2A6 6 0 0 1 22 19h-4.6a8.6 8.6 0 0 0-1-5.8z" opacity=".75" />
    </S>
  ),
  heart: <S><path d="M12 20s-7.5-4.6-9-9.4C1.9 7 4.2 4 7.3 4c2 0 3.5 1.2 4.7 2.8C13.2 5.2 14.7 4 16.7 4c3.1 0 5.4 3 4.3 6.6C19.5 15.4 12 20 12 20z" /></S>,
  heartFill: <S solid><path d="M12 20s-7.5-4.6-9-9.4C1.9 7 4.2 4 7.3 4c2 0 3.5 1.2 4.7 2.8C13.2 5.2 14.7 4 16.7 4c3.1 0 5.4 3 4.3 6.6C19.5 15.4 12 20 12 20z" /></S>,
  gear: (
    <S solid>
      <path d="M10.3 2h3.4l.5 2.6c.7.3 1.3.6 1.9 1.1l2.5-.9 1.7 2.9-2 1.8a7.6 7.6 0 0 1 0 2.2l2 1.8-1.7 2.9-2.5-.9c-.6.5-1.2.8-1.9 1.1l-.5 2.6h-3.4l-.5-2.6c-.7-.3-1.3-.6-1.9-1.1l-2.5.9-1.7-2.9 2-1.8a7.6 7.6 0 0 1 0-2.2l-2-1.8L5.4 4.8l2.5.9c.6-.5 1.2-.8 1.9-1.1zM12 8.5a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7z" transform="translate(0 1.4) scale(1 .92)" />
    </S>
  ),
  pencil: <S><path d="M4 20h4L19 9l-4-4L4 16v4z" /><path d="M13.5 6.5l4 4" /></S>,
  image: <S><rect x="3" y="4" width="18" height="16" rx="3" /><circle cx="9" cy="10" r="2" /><path d="M21 16l-5-5-9 9" /></S>,
  link: <S><path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1" /><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1" /></S>,
  shield: <S><path d="M12 3l8 3v6c0 4.5-3.4 8.2-8 9-4.6-.8-8-4.5-8-9V6z" /><path d="M8.5 12l2.5 2.5 4.5-5" /></S>,
  speaker: <S><path d="M4 9h4l5-4v14l-5-4H4z" className="fill" /><path d="M16.5 9a4 4 0 0 1 0 6M19 6.5a7.5 7.5 0 0 1 0 11" /></S>,
  music: <S><path d="M9 18V5l11-2v13" /><circle cx="6.5" cy="18" r="2.5" className="fill" /><circle cx="17.5" cy="16" r="2.5" className="fill" /></S>,
  phone: <S><rect x="7" y="2.5" width="10" height="19" rx="2.5" /><path d="M11 18.5h2" /></S>,
  palette: <S><path d="M12 3a9 9 0 0 0 0 18c1.4 0 2-1 2-2s-1-1.5-1-2.5S14 15 15 15h2a4 4 0 0 0 4-4c0-4.4-4-8-9-8z" /><circle cx="7.5" cy="11" r="1.2" className="fill" /><circle cx="10" cy="7" r="1.2" className="fill" /><circle cx="15" cy="7.5" r="1.2" className="fill" /></S>,
  motion: <S><circle cx="14" cy="4.5" r="2" className="fill" /><path d="M9 21l3-6 3 3v5M7 12l3-4 4 1 3 4M12 15l-1-6" /></S>,
  globe: <S><circle cx="12" cy="12" r="9" /><path d="M3 12h18M12 3c3 3.3 3 14.7 0 18M12 3c-3 3.3-3 14.7 0 18" /></S>,
  bulb: <S><path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3z" /></S>,
  book: <S><path d="M3 5c3-1 6-1 9 1 3-2 6-2 9-1v14c-3-1-6-1-9 1-3-2-6-2-9-1z" /><path d="M12 6v14" /></S>,
  reset: <S><path d="M4 12a8 8 0 1 0 2.4-5.7" /><path d="M4 4v4h4" /></S>,
  cloud: <S solid><path d="M7 19a5 5 0 0 1-.5-10A6.5 6.5 0 0 1 19 8.5 5.3 5.3 0 0 1 18 19z" /></S>,
  help: <S><circle cx="12" cy="12" r="9" /><path d="M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .9-1 1.6v.6" /><circle cx="12" cy="17" r=".8" className="fill" /></S>,
  headset: <S><path d="M4 14v-2a8 8 0 0 1 16 0v2" /><rect x="3" y="13" width="4" height="7" rx="1.5" className="fill" /><rect x="17" y="13" width="4" height="7" rx="1.5" className="fill" /></S>,
  chat: <S><path d="M4 5h16v11H9l-5 4z" /><circle cx="9" cy="10.5" r=".9" className="fill" /><circle cx="12" cy="10.5" r=".9" className="fill" /><circle cx="15" cy="10.5" r=".9" className="fill" /></S>,
  bug: <S><rect x="7" y="7" width="10" height="13" rx="5" /><path d="M12 7v13M3 13h4M17 13h4M4 7l3 2M20 7l-3 2M4 19l3-2M20 19l-3-2M9 4l1.5 2M15 4l-1.5 2" /></S>,
  info: <S><circle cx="12" cy="12" r="9" /><path d="M12 11v6" /><circle cx="12" cy="7.5" r=".9" className="fill" /></S>,
  signout: <S><path d="M14 4H6v16h8" /><path d="M10 12h11M17 8l4 4-4 4" /></S>,
  sun: <S><circle cx="12" cy="12" r="4" /><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" /></S>,
  moon: <S solid><path d="M20 14.5A8.5 8.5 0 0 1 9.5 4a8.5 8.5 0 1 0 10.5 10.5z" /></S>,
  monitor: <S><rect x="3" y="4" width="18" height="12" rx="2" /><path d="M8 20h8M12 16v4" /></S>,
  calendar: <S><rect x="3.5" y="5" width="17" height="15" rx="2.5" /><path d="M3.5 10h17M8 3v4M16 3v4" /></S>,
  check: <S><path d="M5 12.5l4.5 4.5L19 7.5" /></S>,
  star: <S solid><path d="M12 2.8l2.8 5.8 6.3.9-4.6 4.4 1.1 6.3L12 17.2l-5.6 3 1.1-6.3L2.9 9.5l6.3-.9z" /></S>,
  close: <S><path d="M6 6l12 12M18 6L6 18" /></S>,
  up: <S solid><path d="M12 6l7 10H5z" /></S>,
  down: <S solid><path d="M12 18L5 8h14z" /></S>,
}
