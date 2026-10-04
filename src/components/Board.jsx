import { useRef, useState } from 'react'
import { useFrame } from '@react-three/fiber'
import { RoundedBox } from '@react-three/drei'
import { cellPosition, COLORS, SPACING } from '../constants.js'
import { Piece } from './Pieces.jsx'
import WinLine from './WinLine.jsx'

const GRID_LENGTH = SPACING * 3

function GridBars() {
  const offsets = [-SPACING / 2, SPACING / 2]
  return (
    <group position={[0, 0.12, 0]}>
      {offsets.map((o) => (
        <group key={o}>
          <RoundedBox args={[GRID_LENGTH, 0.12, 0.1]} radius={0.04} position={[0, 0, o]} castShadow receiveShadow>
            <meshStandardMaterial color="#e8e6ff" roughness={0.3} metalness={0.2} />
          </RoundedBox>
          <RoundedBox args={[0.1, 0.12, GRID_LENGTH]} radius={0.04} position={[o, 0, 0]} castShadow receiveShadow>
            <meshStandardMaterial color="#e8e6ff" roughness={0.3} metalness={0.2} />
          </RoundedBox>
        </group>
      ))}
    </group>
  )
}

function Cell({ index, value, canPlay, turn, onPlay, hovered, setHovered }) {
  const position = cellPosition(index)
  const isHovered = hovered === index && canPlay && !value

  return (
    <group position={position}>
      <mesh
        rotation={[-Math.PI / 2, 0, 0]}
        position={[0, 0.07, 0]}
        onPointerOver={(e) => {
          e.stopPropagation()
          setHovered(index)
          document.body.style.cursor = canPlay && !value ? 'pointer' : 'default'
        }}
        onPointerOut={() => {
          setHovered((h) => (h === index ? null : h))
          document.body.style.cursor = 'default'
        }}
        onClick={(e) => {
          e.stopPropagation()
          if (canPlay && !value) onPlay(index)
        }}
      >
        <planeGeometry args={[SPACING * 0.92, SPACING * 0.92]} />
        <meshStandardMaterial
          color={isHovered ? COLORS[turn] : '#2a2550'}
          emissive={isHovered ? COLORS[turn] : '#000000'}
          emissiveIntensity={isHovered ? 0.35 : 0}
          transparent
          opacity={isHovered ? 0.55 : 0.0}
        />
      </mesh>
      {isHovered && <Piece player={turn} position={[0, 0, 0]} ghost />}
    </group>
  )
}

// Pulsing gold ring marking a suggested move.
function HintRing({ index, still }) {
  const ref = useRef()
  useFrame((state) => {
    if (still || !ref.current) return
    const t = state.clock.elapsedTime
    ref.current.scale.setScalar(1 + Math.sin(t * 4) * 0.08)
    ref.current.material.opacity = 0.55 + Math.sin(t * 4) * 0.3
  })
  const [x, , z] = cellPosition(index)
  return (
    <mesh ref={ref} position={[x, 0.1, z]} rotation={[-Math.PI / 2, 0, 0]}>
      <ringGeometry args={[0.52, 0.66, 48]} />
      <meshBasicMaterial color="#ffc531" transparent opacity={0.7} toneMapped={false} />
    </mesh>
  )
}

export default function Board({ board, gameId, winner, canPlay, turn, onPlay, hint = null, reduceMotion = false }) {
  const [hovered, setHovered] = useState(null)

  return (
    <group>
      <RoundedBox args={[GRID_LENGTH + 0.5, 0.12, GRID_LENGTH + 0.5]} radius={0.06} receiveShadow>
        <meshStandardMaterial color="#1d1940" roughness={0.6} metalness={0.1} />
      </RoundedBox>
      <GridBars />

      {board.map((value, i) => (
        <Cell
          key={i}
          index={i}
          value={value}
          canPlay={canPlay}
          turn={turn}
          onPlay={onPlay}
          hovered={hovered}
          setHovered={setHovered}
        />
      ))}

      {board.map((value, i) =>
        value ? (
          <Piece
            key={`${gameId}-${i}`}
            player={value}
            position={cellPosition(i)}
            dim={Boolean(winner) && !winner.line.includes(i)}
            still={reduceMotion}
          />
        ) : null,
      )}

      {hint !== null && !board[hint] && canPlay && <HintRing index={hint} still={reduceMotion} />}
      {winner && <WinLine key={gameId} line={winner.line} player={winner.player} still={reduceMotion} />}
    </group>
  )
}
