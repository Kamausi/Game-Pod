// Vehicle-vs-vehicle contact: sampled box points of each chassis against the other's oriented
// box, resolved with positional correction plus a restitution + friction impulse that also
// spins the bodies (r x J through the diagonal body inertia).
#pragma once
#include "vehicle.h"

namespace mb {

// Returns the largest normal impulse applied (N*s), 0 if no contact. If `bMovable` is false,
// b is treated as kinematic (e.g. an interpolated network replica).
float collideVehicles(Vehicle& a, Vehicle& b, bool bMovable);
void applyImpulse(Vehicle& v, Vec3 point, Vec3 J);

}  // namespace mb
