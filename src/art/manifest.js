// Baked art: each entry is a model in models.js rendered by scripts/bake-art.mjs to
// src/assets/art/<name>.webp at width x height (rendered at 2x, then downsampled).
const tile = { width: 384, height: 384 }
const hero = { width: 640, height: 448 }
const icon = { width: 192, height: 192 }
const prop = { width: 256, height: 256 }

export const ASSETS = {
  // Games (tiles)
  'tic-tac-toe': tile,
  checkers: tile,
  'ring-toss': tile,
  darts: tile,
  'whack-a-mole': tile,
  'mini-golf': tile,
  basketball: tile,
  'bubble-pop': tile,
  'block-blast': tile,
  solitaire: tile,
  'memory-match': tile,
  'maze-runner': tile,
  'air-hockey': tile,
  'puzzle-jigsaw': tile,
  'pocket-racer': tile,
  2048: tile,
  'flappy-bird': tile,
  snake: tile,
  pinball: tile,
  'word-search': tile,
  // Category icons
  star: icon,
  gamepad: icon,
  'puzzle-piece': icon,
  ball: icon,
  cards: icon,
  joystick: icon,
  trophy: icon,
  target: icon,
  // Header decorations
  dice: prop,
  rings: prop,
  mole: prop,
  'gem-purple': prop,
  'gem-green': prop,
  'gem-blue': prop,
}

export const HERO = hero
