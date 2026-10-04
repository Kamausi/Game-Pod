import { useSprite } from './sprites.js'
import './art.css'

// A 3D model rendered to an image (see sprites.js). Shows a soft placeholder while it renders.
export function Sprite({ name, size = 320, aspect = 1, className = '' }) {
  const url = useSprite(name, size, aspect)
  if (url) return <img className={`sprite ${className}`} src={url} alt="" draggable="false" />
  return <span className={`sprite sprite-wait ${className}`} aria-hidden="true" />
}

// A game's 3D model on its own color backdrop: used for tiles, banners and thumbnails.
export function GameArt({ game, hero = false, className = '' }) {
  return (
    <div className={`game-art ${hero ? 'game-art-hero' : ''} ${className}`} style={{ '--c1': game.colors[0], '--c2': game.colors[1] }}>
      <Sprite name={game.id} size={hero ? 320 : 288} aspect={hero ? 1.4 : 1} />
    </div>
  )
}
