const files = import.meta.glob('./assets/avatars/*.webp', { eager: true, import: 'default' })
const src = (id) => files[`./assets/avatars/${id}.webp`]

export const AVATARS = ['you', 'gameking', 'pixelplayz', 'ninjanate', 'lunastars', 'playrocket', 'tetratom', 'joystickjen', 'blockboss', 'cardshark', 'diceduel'].map(
  (id) => ({ id, src: src(id) }),
)

export const avatarSrc = (id) => src(id) ?? src('you')
export const podiumSrc = (place) => src(`podium-${place}`)
