import { useCallback, useEffect, useReducer, useState } from 'react'
import Scene from './components/Scene.jsx'
import TitleScreen from './components/TitleScreen.jsx'
import HomeScreen from './components/HomeScreen.jsx'
import { getAIMove, getWinner } from './game.js'

const emptyBoard = () => Array(9).fill(null)
const other = (p) => (p === 'X' ? 'O' : 'X')

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

export default function App() {
  const [screen, setScreen] = useState('title')
  // Game state lives here so scores survive a trip back to the home screen.
  const [state, dispatch] = useReducer(reducer, initialState)
  const [mode, setMode] = useState('ai')
  const [difficulty, setDifficulty] = useState('hard')

  if (screen === 'title') return <TitleScreen onStart={() => setScreen('home')} />
  if (screen === 'home') return <HomeScreen onPlay={() => setScreen('game')} onTitle={() => setScreen('title')} />

  return (
    <Game
      state={state}
      dispatch={dispatch}
      mode={mode}
      setMode={setMode}
      difficulty={difficulty}
      setDifficulty={setDifficulty}
      onHome={() => setScreen('home')}
    />
  )
}

function Game({ state, dispatch, mode, setMode, difficulty, setDifficulty, onHome }) {
  const { board, starter, gameId, scores } = state

  const winner = getWinner(board)
  const draw = !winner && board.every(Boolean)
  const over = Boolean(winner) || draw
  const turn = currentTurn(board, starter)
  const aiTurn = mode === 'ai' && turn === 'O' && !over
  const canPlay = !over && !aiTurn

  const play = useCallback((index) => dispatch({ type: 'play', index }), [dispatch])

  useEffect(() => {
    if (!aiTurn) return
    const id = setTimeout(() => play(getAIMove(board, 'O', difficulty)), 550)
    return () => clearTimeout(id)
  }, [aiTurn, board, difficulty, play])

  useEffect(() => {
    const onKey = (e) => {
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
  }, [canPlay, board, play, dispatch, onHome])

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
      <Scene board={board} gameId={gameId} winner={winner} canPlay={canPlay} turn={turn} onPlay={play} />

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
        <p className={`status ${winner ? winner.player.toLowerCase() : ''}`}>{status}</p>
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
    </div>
  )
}
