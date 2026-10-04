import { filterGames } from './catalog.js'

// Every game's art is its 3D model (src/art/models.js, same id) on a backdrop of `colors`.
// `playable: true` marks games that exist; the rest show a "Soon" badge.
export const GAMES = [
  { id: 'tic-tac-toe', name: 'Tic Tac Toe', colors: ['#ffb36b', '#b4471c'], playable: true, categories: ['popular', 'puzzle'], tagline: '3D classic. Beat the CPU or a friend.' },
  { id: 'checkers', name: 'Checkers', colors: ['#d8894a', '#4a2412'], categories: ['popular', 'puzzle'] },
  { id: 'ring-toss', name: 'Ring Toss', colors: ['#7ad0ff', '#2a62c8'], categories: ['popular', 'arcade'] },
  { id: 'darts', name: 'Darts', colors: ['#7a6cff', '#1a1f6e'], categories: ['popular', 'sports'] },
  { id: 'whack-a-mole', name: 'Whack a Mole', colors: ['#ffcf6a', '#e0582a'], categories: ['arcade', 'action'], tagline: 'Classic fun. Instant play.' },
  { id: 'mini-golf', name: 'Mini Golf', colors: ['#9fe2ff', '#2f8fd6'], categories: ['sports'], isNew: true, tagline: 'Sink it in one. Eighteen tiny holes.' },
  { id: 'basketball', name: 'Basketball', colors: ['#ffb347', '#a8381a'], categories: ['sports', 'arcade'] },
  { id: 'bubble-pop', name: 'Bubble Pop', colors: ['#8a6cff', '#2a1f8a'], categories: ['puzzle', 'arcade'], isNew: true },
  { id: 'block-blast', name: 'Block Blast', colors: ['#5a6cff', '#141d5c'], categories: ['puzzle'], isNew: true },
  { id: 'solitaire', name: 'Solitaire', colors: ['#3fd06a', '#0d5a2a'], categories: ['card'] },
  { id: 'memory-match', name: 'Memory Match', colors: ['#ffb84d', '#c2541a'], categories: ['puzzle', 'card'] },
  { id: 'maze-runner', name: 'Maze Runner', colors: ['#8fc0ff', '#2f5bd6'], categories: ['puzzle', 'action'] },
  { id: 'air-hockey', name: 'Air Hockey', colors: ['#ff8a9a', '#a8233a'], categories: ['sports', 'arcade'] },
  { id: 'puzzle-jigsaw', name: 'Puzzle Jigsaw', colors: ['#ffd97a', '#d0702a'], categories: ['puzzle'] },
  { id: 'pocket-racer', name: 'Pocket Racer', colors: ['#7ab8ff', '#2b3a8e'], categories: ['action', 'sports'], isNew: true, tagline: 'Tiny cars. Big drifts.' },
  { id: '2048', name: '2048', colors: ['#ffd08a', '#b0642a'], categories: ['puzzle'] },
  { id: 'flappy-bird', name: 'Flappy Bird', colors: ['#9fe0ff', '#3a9fdd'], categories: ['action', 'arcade'], isNew: true },
  { id: 'snake', name: 'Snake', colors: ['#2a5a8a', '#0b1a3e'], categories: ['arcade', 'action'] },
  { id: 'pinball', name: 'Pinball', colors: ['#7a3aff', '#160b4a'], categories: ['arcade'] },
  { id: 'word-search', name: 'Word Search', colors: ['#7a8cff', '#2a2f8a'], categories: ['puzzle'] },
]

export const FEATURED = ['whack-a-mole', 'tic-tac-toe', 'mini-golf', 'pocket-racer']

// `icon` names a 3D model in src/art/models.js.
export const CATEGORIES = [
  { id: 'popular', name: 'Popular', icon: 'star', color: '#734b12', border: '#ffc531' },
  { id: 'action', name: 'Action', icon: 'gamepad', color: '#0a2a66', border: '#2f7bff' },
  { id: 'puzzle', name: 'Puzzle', icon: 'puzzle-piece', color: '#35137d', border: '#9b4dff' },
  { id: 'sports', name: 'Sports', icon: 'ball', color: '#6d2d2b', border: '#e0703a' },
  { id: 'card', name: 'Card', icon: 'cards', color: '#671b39', border: '#e0406e' },
  { id: 'arcade', name: 'Arcade', icon: 'joystick', color: '#035234', border: '#21c06b' },
]

export const ARCADE_IDS = GAMES.filter((g) => g.categories.includes('arcade')).map((g) => g.id)

export const gameById = (id) => GAMES.find((g) => g.id === id)

export const searchGames = (query, category) => filterGames(GAMES, query, category)
