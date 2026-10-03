import { useRef } from 'react'
import { useFrame } from '@react-three/fiber'
import { COLORS } from '../constants.js'

const REST_Y = 0.35

// Drops the piece in with a decaying bounce, then idles with a gentle spin.
function useDropIn(ref, { ghost, spin = 0.4 }) {
  const t = useRef(0)
  useFrame((_, delta) => {
    const obj = ref.current
    if (!obj) return
    t.current += delta
    if (ghost) {
      obj.position.y = REST_Y + 0.15 + Math.sin(t.current * 3) * 0.05
      obj.rotation.y += delta * spin
      return
    }
    const s = t.current
    obj.position.y = REST_Y + 2.8 * Math.exp(-4.5 * s) * Math.abs(Math.cos(7 * s))
    obj.rotation.y = Math.sin(s * spin) * 0.25
  })
}

function PieceMaterial({ color, ghost, dim }) {
  return (
    <meshStandardMaterial
      color={color}
      emissive={color}
      emissiveIntensity={ghost ? 0.2 : dim ? 0.05 : 0.45}
      roughness={0.25}
      metalness={0.3}
      transparent={ghost || dim}
      opacity={ghost ? 0.3 : dim ? 0.35 : 1}
      depthWrite={!ghost}
    />
  )
}

export function XPiece({ position, ghost = false, dim = false }) {
  const ref = useRef()
  useDropIn(ref, { ghost })
  return (
    <group ref={ref} position={position}>
      {[Math.PI / 4, -Math.PI / 4].map((rot) => (
        <mesh key={rot} rotation={[0, rot, 0]} castShadow={!ghost}>
          <boxGeometry args={[1.25, 0.28, 0.28]} />
          <PieceMaterial color={COLORS.X} ghost={ghost} dim={dim} />
        </mesh>
      ))}
    </group>
  )
}

export function OPiece({ position, ghost = false, dim = false }) {
  const ref = useRef()
  useDropIn(ref, { ghost })
  return (
    <group ref={ref} position={position}>
      <mesh rotation={[Math.PI / 2, 0, 0]} castShadow={!ghost}>
        <torusGeometry args={[0.45, 0.14, 24, 64]} />
        <PieceMaterial color={COLORS.O} ghost={ghost} dim={dim} />
      </mesh>
    </group>
  )
}

export function Piece({ player, ...props }) {
  return player === 'X' ? <XPiece {...props} /> : <OPiece {...props} />
}
