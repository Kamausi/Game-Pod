import { test } from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { toyOGeometry, toyPuckGeometry, toyXGeometry } from './art/kit.js'

// Normals must face away from the piece's centre; inward normals render pieces as hollow shells.
function outwardFraction(geo) {
  geo.computeBoundingBox()
  const c = geo.boundingBox.getCenter(new THREE.Vector3())
  const pos = geo.attributes.position
  const nor = geo.attributes.normal
  let out = 0
  let total = 0
  const p = new THREE.Vector3()
  const n = new THREE.Vector3()
  for (let i = 0; i < pos.count; i++) {
    p.fromBufferAttribute(pos, i).sub(c)
    n.fromBufferAttribute(nor, i)
    if (p.lengthSq() < 1e-6) continue
    total++
    if (p.dot(n) > 0) out++
  }
  return out / total
}

test('toy kit pieces have outward-facing normals', () => {
  assert.ok(outwardFraction(toyPuckGeometry()) > 0.9, 'puck')
  assert.ok(outwardFraction(toyXGeometry()) > 0.9, 'X')
})

test('toy O ring faces outward on its outer wall', () => {
  const g = toyOGeometry()
  const pos = g.attributes.position
  const nor = g.attributes.normal
  let ok = 0
  let n = 0
  for (let i = 0; i < pos.count; i++) {
    const r = Math.hypot(pos.getX(i), pos.getZ(i))
    if (r < 0.37) continue // outer wall only
    n++
    if (pos.getX(i) * nor.getX(i) + pos.getZ(i) * nor.getZ(i) > 0) ok++
  }
  assert.ok(ok / n > 0.9)
})
