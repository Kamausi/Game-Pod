import { filterGames } from './catalog.js'
// Home-screen art (cut from the home mockup, no heart icon baked in).
import ticTacToe from './assets/home/tic-tac-toe.webp'
import checkers from './assets/home/checkers.webp'
import ringToss from './assets/home/ring-toss.webp'
import darts from './assets/home/darts.webp'
import flappyBird from './assets/home/flappy-bird.webp'
import miniGolf from './assets/home/mini-golf.webp'
import blockBlast from './assets/home/block-blast.webp'
import bubblePop from './assets/home/bubble-pop.webp'
import pocketRacer from './assets/home/pocket-racer.webp'
import whackAMoleBanner from './assets/home/whack-a-mole.webp'
import catPopular from './assets/home/cat-popular.webp'
import catAction from './assets/home/cat-action.webp'
import catPuzzle from './assets/home/cat-puzzle.webp'
import catSports from './assets/home/cat-sports.webp'
import catCard from './assets/home/cat-card.webp'
import catArcade from './assets/home/cat-arcade.webp'

// Grid tiles (cut from the All Games mockup; the heart button is drawn over the baked-in one).
const tiles = import.meta.glob('./assets/tiles/*.webp', { eager: true, import: 'default' })
const tile = (id) => tiles[`./assets/tiles/${id}.webp`]

// `playable: true` marks games that exist; the rest show a "Soon" badge.
const CATALOG = [
  { id: 'tic-tac-toe', name: 'Tic Tac Toe', art: ticTacToe, playable: true, categories: ['popular', 'puzzle'], tagline: '3D classic. Beat the CPU or a friend.' },
  { id: 'checkers', name: 'Checkers', art: checkers, categories: ['popular', 'puzzle'] },
  { id: 'ring-toss', name: 'Ring Toss', art: ringToss, categories: ['popular', 'arcade'] },
  { id: 'darts', name: 'Darts', art: darts, categories: ['popular', 'sports'] },
  { id: 'whack-a-mole', name: 'Whack a Mole', banner: whackAMoleBanner, categories: ['arcade', 'action'], tagline: 'Classic fun. Instant play.' },
  { id: 'mini-golf', name: 'Mini Golf', art: miniGolf, categories: ['sports'], isNew: true, tagline: 'Sink it in one. Eighteen tiny holes.' },
  { id: 'basketball', name: 'Basketball', categories: ['sports', 'arcade'] },
  { id: 'bubble-pop', name: 'Bubble Pop', art: bubblePop, categories: ['puzzle', 'arcade'], isNew: true },
  { id: 'block-blast', name: 'Block Blast', art: blockBlast, categories: ['puzzle'], isNew: true },
  { id: 'solitaire', name: 'Solitaire', categories: ['card'] },
  { id: 'memory-match', name: 'Memory Match', categories: ['puzzle', 'card'] },
  { id: 'maze-runner', name: 'Maze Runner', categories: ['puzzle', 'action'] },
  { id: 'air-hockey', name: 'Air Hockey', categories: ['sports', 'arcade'] },
  { id: 'puzzle-jigsaw', name: 'Puzzle Jigsaw', categories: ['puzzle'] },
  { id: 'pocket-racer', name: 'Pocket Racer', art: pocketRacer, categories: ['action', 'sports'], isNew: true, tagline: 'Tiny cars. Big drifts.' },
  { id: '2048', name: '2048', categories: ['puzzle'] },
  { id: 'flappy-bird', name: 'Flappy Bird', art: flappyBird, categories: ['action', 'arcade'], isNew: true },
  { id: 'snake', name: 'Snake', categories: ['arcade', 'action'] },
  { id: 'pinball', name: 'Pinball', categories: ['arcade'] },
  { id: 'word-search', name: 'Word Search', categories: ['puzzle'] },
]

export const GAMES = CATALOG.map((g) => ({ ...g, tile: tile(g.id), art: g.art ?? tile(g.id) }))

export const FEATURED = ['whack-a-mole', 'tic-tac-toe', 'mini-golf', 'pocket-racer']

export const CATEGORIES = [
  { id: 'popular', name: 'Popular', icon: catPopular, color: '#734b12', border: '#ffc531' },
  { id: 'action', name: 'Action', icon: catAction, color: '#0a2a66', border: '#2f7bff' },
  { id: 'puzzle', name: 'Puzzle', icon: catPuzzle, color: '#35137d', border: '#9b4dff' },
  { id: 'sports', name: 'Sports', icon: catSports, color: '#6d2d2b', border: '#e0703a' },
  { id: 'card', name: 'Card', icon: catCard, color: '#671b39', border: '#e0406e' },
  { id: 'arcade', name: 'Arcade', icon: catArcade, color: '#035234', border: '#21c06b' },
]

export const ARCADE_IDS = GAMES.filter((g) => g.categories.includes('arcade')).map((g) => g.id)

export const gameById = (id) => GAMES.find((g) => g.id === id)

export const searchGames = (query, category) => filterGames(GAMES, query, category)
