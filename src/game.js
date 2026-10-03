export const LINES = [
  [0, 1, 2], [3, 4, 5], [6, 7, 8],
  [0, 3, 6], [1, 4, 7], [2, 5, 8],
  [0, 4, 8], [2, 4, 6],
]

export function getWinner(board) {
  for (const line of LINES) {
    const [a, b, c] = line
    if (board[a] && board[a] === board[b] && board[a] === board[c]) {
      return { player: board[a], line }
    }
  }
  return null
}

export function isDraw(board) {
  return !getWinner(board) && board.every(Boolean)
}

function minimax(board, player, ai, depth) {
  const win = getWinner(board)
  if (win) return win.player === ai ? 10 - depth : depth - 10
  if (board.every(Boolean)) return 0

  const scores = []
  for (let i = 0; i < 9; i++) {
    if (board[i]) continue
    board[i] = player
    scores.push(minimax(board, player === 'X' ? 'O' : 'X', ai, depth + 1))
    board[i] = null
  }
  return player === ai ? Math.max(...scores) : Math.min(...scores)
}

// difficulty: 'easy' plays randomly, 'medium' plays perfectly half the time, 'hard' is unbeatable.
export function getAIMove(board, ai, difficulty = 'hard', rand = Math.random) {
  const empty = board.map((v, i) => (v ? null : i)).filter((i) => i !== null)
  if (empty.length === 0) return null

  const random = () => empty[Math.floor(rand() * empty.length)]
  if (difficulty === 'easy') return random()
  if (difficulty === 'medium' && rand() < 0.5) return random()

  const human = ai === 'X' ? 'O' : 'X'
  let best = null
  let bestScore = -Infinity
  const scratch = [...board]
  for (const i of empty) {
    scratch[i] = ai
    const score = minimax(scratch, human, ai, 1)
    scratch[i] = null
    if (score > bestScore) {
      bestScore = score
      best = i
    }
  }
  return best
}
