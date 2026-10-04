import { useCallback, useEffect, useReducer, useRef, useState } from 'react'
import Scene from './components/Scene.jsx'
import TitleScreen from './components/TitleScreen.jsx'
import Hub from './components/hub/Hub.jsx'
import Badge from './art/Badge.jsx'
import { getAIMove, getWinner } from './game.js'
import { useStore } from './store.jsx'
import { setMusicVolume, setSoundEnabled, sfx, startMusic, stopMusic, unlockAudio, vibrate } from './audio.js'

const emptyBoard = () => Array(9).fill(null)
const other = (p) => (p === 'X' ? 'O' : 'X')
const HINT_DELAY = 5000

function currentTurn(board, starter) {
  const moves = board.filter(Boolean).length
  return moves % 2 === 0 ? starter : other(starter)
}

const initialState = {
  board: emptyBoard(),
  starter: 'X',
  gameId: 0,
  scores: { X: 0, O: 0, draw: 0 },
}

function reducer(state, action) {
  switch (action.type) {
    case 'play': {
      const { board, starter, scores } = state
      if (board[action.index] || getWinner(board) || board.every(Boolean)) return state
      const next = [...board]
      next[action.index] = currentTurn(board, starter)
      const win = getWinner(next)
      const draw = !win && next.every(Boolean)
      return {
        ...state,
        board: next,
        scores: win
          ? { ...scores, [win.player]: scores[win.player] + 1 }
          : draw
            ? { ...scores, draw: scores.draw + 1 }
            : scores,
      }
    }
    case 'newGame':
      // Alternate who opens each round so neither side always has the first-move edge.
      return { ...state, board: emptyBoard(), starter: other(state.starter), gameId: state.gameId + 1 }
    case 'reset':
      return { ...initialState, gameId: state.gameId + 1 }
    default:
      return state
  }
}

function useToasts() {
  const [toasts, setToasts] = useState([])
  const notify = useCallback((text, icon = null) => {
    const id = Math.random()
    setToasts((t) => [...t.slice(-2), { id, text, icon }])
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 2600)
  }, [])
  return [toasts, notify]
}

// Keeps sound effects and background music in step with settings. Audio can only start
// after the first tap or key press, so music waits for that.
function useAudio() {
  const { settings } = useStore()
  const [ready, setReady] = useState(false)

  useEffect(() => {
    const unlock = () => {
      unlockAudio()
      setReady(true)
    }
    window.addEventListener('pointerdown', unlock, { once: true })
    window.addEventListener('keydown', unlock, { once: true })
    return () => {
      window.removeEventListener('pointerdown', unlock)
      window.removeEventListener('keydown', unlock)
    }
  }, [])

  useEffect(() => setSoundEnabled(settings.sound), [settings.sound])
  useEffect(() => {
    if (ready && settings.music) startMusic(settings.volume)
    else stopMusic()
  }, [ready, settings.music]) // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => setMusicVolume(settings.volume), [settings.volume])
}

export default function App() {
  const { reduceMotion } = useStore()
  const [screen, setScreen] = useState('title')
  const [hubTab, setHubTab] = useState('home')
  // Game state lives here so scores survive a trip back to the home screen.
  const [state, dispatch] = useReducer(reducer, initialState)
  const [mode, setMode] = useState('ai')
  const [difficulty, setDifficulty] = useState('hard')
  const [toasts, notify] = useToasts()
  useAudio()

  useEffect(() => {
    document.documentElement.classList.toggle('reduce-motion', reduceMotion)
  }, [reduceMotion])

  let view
  if (screen === 'title') view = <TitleScreen reducedMotion={reduceMotion} onStart={() => setScreen('home')} />
  else if (screen === 'home')
    view = (
      <Hub tab={hubTab} setTab={setHubTab} notify={notify} onPlay={() => setScreen('game')} onTitle={() => setScreen('title')} />
    )
  else
    view = (
      <Game
        state={state}
        dispatch={dispatch}
        mode={mode}
        setMode={setMode}
        difficulty={difficulty}
        setDifficulty={setDifficulty}
        notify={notify}
        onHome={() => setScreen('home')}
      />
    )

  return (
    <>
      {view}
      <div className="toasts" role="status" aria-live="polite">
        {toasts.map((t) => (
          <div key={t.id} className={`toast ${t.icon ? 'with-icon' : ''}`}>
            {t.icon}
            <span>{t.text}</span>
          </div>
        ))}
      </div>
    </>
  )
}

function Tutorial({ onClose }) {
  return (
    <div className="tutorial-backdrop" onClick={onClose}>
      <div className="tutorial" role="dialog" aria-modal="true" aria-label="How to play" onClick={(e) => e.stopPropagation()}>
        <h2>How to play</h2>
        <ol>
          <li>Take turns placing X and O on the 3×3 board.</li>
          <li>Get three in a row (across, down or diagonal) to win.</li>
          <li>Tap a square or press 1–9. Drag to spin the board.</li>
        </ol>
        <p>Wins earn points and XP. Hard CPU never loses, so a draw there is an achievement!</p>
        <button type="button" className="primary" onClick={onClose} autoFocus>
          Got it
        </button>
      </div>
    </div>
  )
}

function Game({ state, dispatch, mode, setMode, difficulty, setDifficulty, notify, onHome }) {
  const { record, settings, seenTutorials, markTutorialSeen, reduceMotion } = useStore()
  const { board, starter, gameId, scores } = state
  const [hint, setHint] = useState(null)
  const recorded = useRef(-1)

  const winner = getWinner(board)
  const draw = !winner && board.every(Boolean)
  const over = Boolean(winner) || draw
  const turn = currentTurn(board, starter)
  const aiTurn = mode === 'ai' && turn === 'O' && !over
  const canPlay = !over && !aiTurn
  const showTutorial = settings.tutorials && !seenTutorials.includes('tic-tac-toe')

  const play = useCallback(
    (index) => {
      sfx('place')
      vibrate(settings.haptics)
      dispatch({ type: 'play', index })
    },
    [dispatch, settings.haptics],
  )

  useEffect(() => {
    if (!aiTurn) return
    const id = setTimeout(() => {
      sfx('cpu')
      dispatch({ type: 'play', index: getAIMove(board, 'O', difficulty) })
    }, 550)
    return () => clearTimeout(id)
  }, [aiTurn, board, difficulty, dispatch])

  // Save each finished game once, then celebrate any achievements it unlocked.
  useEffect(() => {
    if (!over || recorded.current === gameId) return
    recorded.current = gameId
    const result = mode === 'ai' ? (draw ? 'draw' : winner.player === 'X' ? 'win' : 'loss') : 'played'
    const r = record({ gameId: 'tic-tac-toe', mode, difficulty, result })
    sfx(result === 'loss' ? 'lose' : result === 'draw' ? 'draw' : 'win')
    vibrate(settings.haptics && result === 'win', [20, 40, 20])
    notify(`+${r.points} points`)
    r.newly.forEach((a, i) =>
      setTimeout(() => {
        sfx('unlock')
        notify(`Achievement unlocked: ${a.name} (+${a.xp} XP)`, <Badge icon={a.icon} tone={a.tone} />)
      }, 700 + i * 900),
    )
  }, [over, gameId]) // eslint-disable-line react-hooks/exhaustive-deps

  // Move hint: if the player pauses on their turn, highlight a strong move.
  useEffect(() => {
    setHint(null)
    if (!settings.hints || !canPlay || showTutorial) return
    const id = setTimeout(() => setHint(getAIMove(board, turn, 'hard')), HINT_DELAY)
    return () => clearTimeout(id)
  }, [board, canPlay, turn, settings.hints, showTutorial])

  useEffect(() => {
    const onKey = (e) => {
      if (showTutorial) return
      if (e.key >= '1' && e.key <= '9') {
        const index = Number(e.key) - 1
        if (canPlay && !board[index]) play(index)
      } else if (e.key === 'r' || e.key === 'R') {
        dispatch({ type: 'newGame' })
      } else if (e.key === 'Escape') {
        onHome()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [canPlay, board, play, dispatch, onHome, showTutorial])

  const changeMode = (m) => {
    setMode(m)
    dispatch({ type: 'reset' })
  }

  const changeDifficulty = (d) => {
    setDifficulty(d)
    dispatch({ type: 'reset' })
  }

  const label = (p) => (mode === 'ai' ? (p === 'X' ? 'You' : 'CPU') : `Player ${p}`)

  let status
  if (winner) status = mode === 'ai' ? (winner.player === 'X' ? 'You win!' : 'CPU wins!') : `${winner.player} wins!`
  else if (draw) status = "It's a draw"
  else if (aiTurn) status = 'CPU is thinking…'
  else status = mode === 'ai' ? 'Your move' : `${turn} to move`

  return (
    <div className="app">
      <Scene board={board} gameId={gameId} winner={winner} canPlay={canPlay} turn={turn} onPlay={play} hint={hint} reduceMotion={reduceMotion} />

      <button className="home" onClick={onHome} aria-label="Back to home screen">
        ← Home
      </button>

      <header className="hud top">
        <h1>
          Tic <span className="x">Tac</span> <span className="o">Toe</span>
        </h1>
        <div className="scores">
          <div className={`score x ${!over && turn === 'X' ? 'active' : ''}`}>
            <span>{label('X')}</span>
            <strong>{scores.X}</strong>
          </div>
          <div className="score draw">
            <span>Draws</span>
            <strong>{scores.draw}</strong>
          </div>
          <div className={`score o ${!over && turn === 'O' ? 'active' : ''}`}>
            <span>{label('O')}</span>
            <strong>{scores.O}</strong>
          </div>
        </div>
        <p className={`status ${winner ? winner.player.toLowerCase() : ''}`}>{hint !== null && canPlay ? 'Hint: try the glowing square' : status}</p>
      </header>

      <footer className="hud bottom">
        <div className="group">
          <button className={mode === 'ai' ? 'on' : ''} onClick={() => changeMode('ai')}>vs CPU</button>
          <button className={mode === 'pvp' ? 'on' : ''} onClick={() => changeMode('pvp')}>2 Players</button>
        </div>
        {mode === 'ai' && (
          <div className="group">
            {['easy', 'medium', 'hard'].map((d) => (
              <button key={d} className={difficulty === d ? 'on' : ''} onClick={() => changeDifficulty(d)}>
                {d}
              </button>
            ))}
          </div>
        )}
        <div className="group">
          <button className={over ? 'primary pulse' : 'primary'} onClick={() => dispatch({ type: 'newGame' })}>
            New game
          </button>
          <button onClick={() => dispatch({ type: 'reset' })}>Reset scores</button>
        </div>
        <p className="hint">Click a square or press 1–9 · drag to orbit · scroll to zoom · R for new game · Esc for home</p>
      </footer>

      {showTutorial && <Tutorial onClose={() => markTutorialSeen('tic-tac-toe')} />}
    </div>
  )
}
