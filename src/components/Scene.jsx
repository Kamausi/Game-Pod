import { useLayoutEffect } from 'react'
import { Canvas, useThree } from '@react-three/fiber'
import { ContactShadows, OrbitControls, Sparkles, Stars } from '@react-three/drei'
import { COLORS } from '../constants.js'
import Board from './Board.jsx'

const CAMERA_DIR = [0, 7, 6.2]

// Pull the camera back on narrow (portrait) screens so the whole board stays in frame.
function ResponsiveCamera() {
  const camera = useThree((s) => s.camera)
  const aspect = useThree((s) => s.size.width / s.size.height)
  useLayoutEffect(() => {
    const scale = aspect < 1.2 ? Math.min(2.2, (1.2 / aspect) ** 0.65) : 1
    camera.position.set(...CAMERA_DIR.map((v) => v * scale))
    camera.lookAt(0, 0, 0)
  }, [aspect, camera])
  return null
}

export default function Scene(props) {
  const { winner } = props
  return (
    <Canvas shadows dpr={[1, 2]} camera={{ position: CAMERA_DIR, fov: 45 }}>
      <ResponsiveCamera />
      <color attach="background" args={['#0d0b1e']} />
      <fog attach="fog" args={['#0d0b1e', 16, 36]} />

      <ambientLight intensity={0.35} />
      <directionalLight
        position={[4, 8, 5]}
        intensity={1.6}
        castShadow
        shadow-mapSize={[1024, 1024]}
        shadow-camera-left={-5}
        shadow-camera-right={5}
        shadow-camera-top={5}
        shadow-camera-bottom={-5}
      />
      <pointLight position={[-5, 3, -4]} intensity={30} color={COLORS.O} />
      <pointLight position={[5, 3, 4]} intensity={20} color={COLORS.X} />

      <Stars radius={40} depth={20} count={1500} factor={3} fade speed={0.6} />

      <Board {...props} />

      {winner && (
        <Sparkles
          key={props.gameId}
          count={80}
          scale={[5, 3, 5]}
          position={[0, 1.5, 0]}
          size={6}
          speed={0.8}
          color={COLORS[winner.player]}
        />
      )}

      <ContactShadows position={[0, -0.07, 0]} opacity={0.6} scale={12} blur={2.5} far={4} />

      <OrbitControls
        target={[0, 0, 0.3]}
        enablePan={false}
        enableDamping
        minDistance={6}
        maxDistance={22}
        minPolarAngle={0.2}
        maxPolarAngle={Math.PI / 2.3}
      />
    </Canvas>
  )
}
