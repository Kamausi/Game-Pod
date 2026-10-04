# Game-Pod

## Title screen

The app opens on an animated Game Pod title screen made with HTML, CSS, canvas and JavaScript over the poster art. It needs no video file:

- slow push-in / pull-out camera, pulsing pod light and LEDs, rotating light rays and a glowing floor ring
- the logo "breathes" and catches a light sweep every few seconds
- canvas sparks rise out of the pod while dust and stars drift across the frame
- parallax that follows the mouse, or the phone's tilt where the browser allows it
- every animation loops seamlessly, and the particles pause while the tab is hidden
- the poster fills the screen on phones and sits over a blurred copy of itself on wide screens
- with the system's reduce-motion setting on, it shows a still poster
- tap anywhere or press Enter to dive into the pod and open the home screen

The art is `src/assets/title.webp` (compressed from the 2 MB PNG to about 150 KB). To make individual objects float on their own later, cut them into separate transparent layers and position them the same way as the logo layer in `src/components/TitleScreen.jsx`.

## App screens

After the title screen, Game Pod has five tabs (Home, All Games, Achievements, Leaderboards, Profile) plus Settings behind the gear. Their layout follows the Game Pod mockups, laid out in container-query units against the 941px-wide designs with minimum sizes for small phones.

### Art

All in-app art is generated in code; the only bitmap made outside the project is the title-screen poster (`src/assets/title.webp`).

**3D assets** go through a studio bake pipeline, one asset at a time:

1. **Model** (`src/art/models.js`): each game, icon and decoration is a Three.js model built from bevelled primitives, lathe-turned profiles and canvas textures (wood grain, board faces), with glossy plastic, satin wood and metal materials.
2. **Studio** (`src/art/studio.js`): renders the model with an HDR environment (`npm run make-hdr` generates `src/art/env/studio.hdr`: warm key softbox, overhead strip, cool fill, hot-pink and cyan rim lights), a shadow-casting key light onto an invisible floor, a fresnel rim glow layered onto the model's own materials, ACES tone mapping, a camera fitted to the model's outline, and an alpha-preserving bloom pass.
3. **Bake** (`npm run bake -- <asset>`): renders at 2x in headless Chromium, downsamples with Lanczos and saves a transparent WebP to `src/assets/art/`. Add `--compare` to get ACES / AgX / Neutral variants for review. Sizes are listed in `src/art/manifest.js`. (Needs Playwright's Chromium: `npx playwright install chromium`.)

The app shows baked images; an asset that hasn't been baked yet falls back to a live render of the same model, so assets can be upgraded individually. Baked so far: Tic Tac Toe, Checkers, Ring Toss, Darts.

The live game scene uses the same HDR (`studio-small.hdr`) for reflections.

**Vector art**: the Game Pod logo (`Logo.jsx`), cartoon avatars (`Avatar.jsx`) and winged achievement medals (`Badge.jsx`) are SVG. **Header** (`HeaderArt.jsx`): a live scene with a nebula sky, twinkling stars and floating 3D props around the logo.

### Progress

Everything is real and saved in the browser (`localStorage`), so it survives reloads but stays on that device:

- Each finished game earns points: vs CPU a win is 50 / 100 / 150 (easy / medium / hard), a draw 10 / 30 / 60 and a loss 5 / 10 / 15; a 2-player game is 20.
- XP = points + achievement rewards; every 500 XP is a level.
- 13 achievements (`ACHIEVEMENTS` in `src/progress.js`) unlock automatically and pop up in-game.
- Logic lives in `src/progress.js` (pure, unit-tested); state and saving in `src/store.jsx`.

### Sound

`src/audio.js` synthesizes effects (moves, win/lose/draw, achievement unlocks) and a gentle generative music loop with the Web Audio API, so the app ships no audio files. Browsers only allow sound after the first tap or key press.

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
npm run build:html  # rebuild tic-tac-toe.html
```

### Standalone HTML

`tic-tac-toe.html` is the whole game in one self-contained file: all JavaScript and CSS are inlined and nothing is loaded from the network. Open it straight from disk or upload it to any static host (GitHub Pages, Netlify, S3…). After changing the source, run `npm run build:html` to regenerate it.

### Layout

- `src/components/TitleScreen.jsx` / `.css`: animated title screen
- `src/components/hub/`: the five tabs, Settings and shared pieces (`Hub.jsx` is the shell)
- `src/art/`: 3D models and their renderer, logo, avatars, badges and the header scene
- `src/games.js`: game catalog; `src/progress.js`: points, levels, achievements; `src/store.jsx`: saved state; `src/audio.js`: sound
- `src/game.js`: win/draw detection and the minimax AI (no React, unit-tested)
- `src/App.jsx`: game state, CPU turns, keyboard input and the on-screen controls
- `src/components/Scene.jsx`: canvas, lights, camera, effects
- `src/components/Board.jsx`: board, grid and clickable squares
- `src/components/Pieces.jsx`: X and O meshes and their animations
- `src/components/WinLine.jsx`: animated line through the winning three
