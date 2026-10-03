import { test } from 'node:test'
import assert from 'node:assert/strict'
import { getWinner, isDraw, getAIMove } from './game.js'

const _ = null

test('detects row, column and diagonal wins', () => {
  assert.deepEqual(getWinner(['X', 'X', 'X', _, _, _, _, _, _]), { player: 'X', line: [0, 1, 2] })
  assert.deepEqual(getWinner(['O', _, _, 'O', _, _, 'O', _, _]), { player: 'O', line: [0, 3, 6] })
  assert.deepEqual(getWinner([_, _, 'X', _, 'X', _, 'X', _, _]), { player: 'X', line: [2, 4, 6] })
  assert.equal(getWinner(Array(9).fill(null)), null)
})

test('detects a draw', () => {
  assert.equal(isDraw(['X', 'O', 'X', 'X', 'O', 'O', 'O', 'X', 'X']), true)
  assert.equal(isDraw(['X', 'X', 'X', 'O', 'O', _, _, _, _]), false)
})

test('hard AI takes a winning move', () => {
  assert.equal(getAIMove(['O', 'O', _, 'X', 'X', _, 'X', _, _], 'O'), 2)
})

test('hard AI blocks an immediate threat', () => {
  assert.equal(getAIMove(['X', 'X', _, _, 'O', _, _, _, _], 'O'), 2)
})

test('hard AI never loses against itself (always draws)', () => {
  const board = Array(9).fill(null)
  let player = 'X'
  while (!getWinner(board) && !isDraw(board)) {
    board[getAIMove(board, player)] = player
    player = player === 'X' ? 'O' : 'X'
  }
  assert.equal(isDraw(board), true)
})
