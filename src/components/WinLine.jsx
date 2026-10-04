import { useMemo, useRef } from 'react'
import { useFrame } from '@react-three/fiber'
import * as THREE from 'three'
import { cellPosition, COLORS } from '../constants.js'

export default function WinLine({ line, player, still = false }) {
  const ref = useRef()
  const progress = useRef(0)

  const { midpoint, length, quaternion } = useMemo(() => {
    const start = new THREE.Vector3(...cellPosition(line[0]))
    const end = new THREE.Vector3(...cellPosition(line[2]))
    const dir = end.clone().sub(start)
    return {
      midpoint: start.clone().add(end).multiplyScalar(0.5).setY(0.9),
      length: dir.length() + 1.2,
      quaternion: new THREE.Quaternion().setFromUnitVectors(
        new THREE.Vector3(0, 1, 0),
        dir.normalize(),
      ),
    }
  }, [line])

  useFrame((state, delta) => {
    progress.current = still ? 1 : Math.min(1, progress.current + delta * 2.5)
    const eased = 1 - Math.pow(1 - progress.current, 3)
    ref.current.scale.set(1, eased, 1)
    ref.current.material.emissiveIntensity = 1.2 + Math.sin(state.clock.elapsedTime * 6) * 0.4
  })

  return (
    <mesh ref={ref} position={midpoint} quaternion={quaternion} scale={[1, 0, 1]}>
      <cylinderGeometry args={[0.09, 0.09, length, 24]} />
      <meshStandardMaterial color="#ffffff" emissive={COLORS[player]} emissiveIntensity={1.2} toneMapped={false} />
    </mesh>
  )
}
