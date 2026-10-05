import { useSprite } from './sprites.js'
import './art.css'

// Studio-baked renders (scripts/bake-art.mjs). Assets not baked yet fall back to a live render.
const baked = import.meta.glob('../assets/art/*.webp', { eager: true, import: 'default' })
export const bakedArt = (name) => baked[`../assets/art/${name}.webp`]

// A 3D model as an image. Shows a soft placeholder while a live fallback renders.
export function Sprite({ name, size = 320, aspect = 1, className = '' }) {
  const bakedUrl = bakedArt(name)
  const liveUrl = useSprite(name, size, aspect, !bakedUrl)
  const url = bakedUrl ?? liveUrl
  if (url) return <img className={`sprite ${className}`} src={url} alt="" draggable="false" />
  return <span className={`sprite sprite-wait ${className}`} aria-hidden="true" />
}

// A game's 3D model on its own color backdrop: used for tiles, banners and thumbnails. A featured banner uses
// <id>-banner.webp when there is one (a fixed image kept separate from the tile's art). Baked game art is a
// full picture with its own background, so it fills the box edge to edge.
export function GameArt({ game, hero = false, className = '' }) {
  const full = !hero && bakedArt(game.id)
  return (
    <div className={`game-art ${hero ? 'game-art-hero' : ''} ${full ? 'game-art-full' : ''} ${className}`} style={{ '--c1': game.colors[0], '--c2': game.colors[1] }}>
      <Sprite name={hero && bakedArt(`${game.id}-banner`) ? `${game.id}-banner` : game.id} size={hero ? 320 : 288} aspect={hero ? 1.4 : 1} />
    </div>
  )
}
