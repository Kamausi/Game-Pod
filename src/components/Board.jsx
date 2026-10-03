import { useState } from 'react'
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

export default function Board({ board, gameId, winner, canPlay, turn, onPlay }) {
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
          />
        ) : null,
      )}

      {winner && <WinLine key={gameId} line={winner.line} player={winner.player} />}
    </group>
  )
}
