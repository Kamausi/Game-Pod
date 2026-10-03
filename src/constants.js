export const SPACING = 1.6
export const COLORS = { X: '#ff5d73', O: '#4cc9f0' }

export function cellPosition(index) {
  const col = index % 3
  const row = Math.floor(index / 3)
  return [(col - 1) * SPACING, 0, (row - 1) * SPACING]
}
