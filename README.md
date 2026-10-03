# Game-Pod

## Tic Tac Toe 3D

A 3D tic-tac-toe game built with React, Three.js and WebGL (via `@react-three/fiber` and `@react-three/drei`).

### Features

- 3D board with animated pieces that drop and bounce in, a glowing win line, and sparkles when someone wins
- Ghost preview of your piece when you hover over a square
- Play against the CPU (easy / medium / unbeatable hard, using minimax) or with 2 players on one device
- The first move switches sides each round; scores are kept for X, O and draws
- Orbit camera (drag to rotate, scroll to zoom), sized to fit phone screens

### Controls

| Action        | Input                       |
| ------------- | --------------------------- |
| Place a piece | Click a square, or keys 1–9 (left to right, top to bottom) |
| New game      | `R` or the **New game** button |
| Rotate / zoom | Drag / scroll                |

### Running it

```bash
npm install
npm run dev      # start the dev server
npm run build    # production build in dist/
npm test         # game logic and AI tests
```

### Layout

- `src/game.js`: win/draw detection and the minimax AI (no React, unit-tested)
- `src/App.jsx`: game state, CPU turns, keyboard input and the on-screen controls
- `src/components/Scene.jsx`: canvas, lights, camera, effects
- `src/components/Board.jsx`: board, grid and clickable squares
- `src/components/Pieces.jsx`: X and O meshes and their animations
- `src/components/WinLine.jsx`: animated line through the winning three
