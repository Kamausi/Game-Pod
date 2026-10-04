export function filterGames(games, query, category) {
  const q = query.trim().toLowerCase()
  return games.filter(
    (g) => (!category || g.categories.includes(category)) && (!q || g.name.toLowerCase().includes(q)),
  )
}
