import { filterGames } from './catalog.js'
import ticTacToe from './assets/home/tic-tac-toe.webp'
import checkers from './assets/home/checkers.webp'
import ringToss from './assets/home/ring-toss.webp'
import darts from './assets/home/darts.webp'
import flappyBird from './assets/home/flappy-bird.webp'
import miniGolf from './assets/home/mini-golf.webp'
import blockBlast from './assets/home/block-blast.webp'
import bubblePop from './assets/home/bubble-pop.webp'
import pocketRacer from './assets/home/pocket-racer.webp'
import whackAMole from './assets/home/whack-a-mole.webp'
import catPopular from './assets/home/cat-popular.webp'
import catAction from './assets/home/cat-action.webp'
import catPuzzle from './assets/home/cat-puzzle.webp'
import catSports from './assets/home/cat-sports.webp'
import catCard from './assets/home/cat-card.webp'
import catArcade from './assets/home/cat-arcade.webp'

// `playable: false` games appear in the catalog with a "Soon" badge.
export const GAMES = [
  { id: 'tic-tac-toe', name: 'Tic Tac Toe', art: ticTacToe, playable: true, categories: ['popular', 'puzzle'], tagline: '3D classic. Beat the CPU or a friend.' },
  { id: 'checkers', name: 'Checkers', art: checkers, categories: ['popular', 'puzzle'] },
  { id: 'ring-toss', name: 'Ring Toss', art: ringToss, categories: ['popular', 'arcade'] },
  { id: 'darts', name: 'Darts', art: darts, categories: ['popular', 'sports'] },
  { id: 'whack-a-mole', name: 'Whack a Mole', art: whackAMole, categories: ['arcade', 'action'], tagline: 'Classic fun. Instant play.' },
  { id: 'flappy-bird', name: 'Flappy Bird', art: flappyBird, categories: ['action', 'arcade'], isNew: true },
  { id: 'mini-golf', name: 'Mini Golf', art: miniGolf, categories: ['sports'], isNew: true, tagline: 'Sink it in one. Eighteen tiny holes.' },
  { id: 'block-blast', name: 'Block Blast', art: blockBlast, categories: ['puzzle'], isNew: true },
  { id: 'bubble-pop', name: 'Bubble Pop', art: bubblePop, categories: ['puzzle', 'arcade'], isNew: true },
  { id: 'pocket-racer', name: 'Pocket Racer', art: pocketRacer, categories: ['action', 'sports'], isNew: true, tagline: 'Tiny cars. Big drifts.' },
]

export const FEATURED = ['whack-a-mole', 'tic-tac-toe', 'mini-golf', 'pocket-racer']

export const CATEGORIES = [
  { id: 'popular', name: 'Popular', icon: catPopular, color: '#734b12', border: '#ffc531' },
  { id: 'action', name: 'Action', icon: catAction, color: '#0a2a66', border: '#2f7bff' },
  { id: 'puzzle', name: 'Puzzle', icon: catPuzzle, color: '#35137d', border: '#9b4dff' },
  { id: 'sports', name: 'Sports', icon: catSports, color: '#6d2d2b', border: '#e0703a' },
  { id: 'card', name: 'Card', icon: catCard, color: '#671b39', border: '#e0406e' },
  { id: 'arcade', name: 'Arcade', icon: catArcade, color: '#035234', border: '#21c06b' },
]

export const gameById = (id) => GAMES.find((g) => g.id === id)

export const searchGames = (query, category) => filterGames(GAMES, query, category)
