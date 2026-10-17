# Physics: equations and calibration

Fixed step `dt = 1/240 s` (`Vehicle::DT`), semi-implicit Euler, Y up. Vehicle frame: +Z forward,
+X left, +Y up.

## Rigid body
```
v  += (ΣF / m) dt
x  += v dt
τ_b = qᶜ τ q                       (torque in body frame)
ω̇_b = I⁻¹ (τ_b − ω_b × I ω_b)      (Euler's equations, diagonal box inertia)
ω  += q ω̇_b qᶜ dt
q  ← normalize(q + ½ (ω ⊗ q) dt)
I  = m/12 · (h²+l², w²+l², w²+h²)
```

## Suspension (raycast spring–damper per corner)
```
F_s = k·c + d·ċ  (+ bump stop k_b·(c − 0.9·travel) beyond 90 % travel)
k   = (m g / 4) / (0.38 · travel)          static sag = 38 % of travel
d   = 2 ζ √(k · m/4),  ζ = 0.42
```
Compression rate is limited to 3 m/s so ground that "appears" under a wheel (refilling ruts, berms)
can't produce a force spike. The tire is sampled as a **cylinder**: 5 points along the rolling
direction offset by the wheel curvature `r − √(r² − Δx²)` plus 2 across the tread; the highest
support wins, so tires straddle holes and climb rut walls.

## Contact patch from air pressure
```
k_tire = 1.6·10⁵ (psi / 25)^0.85      N/m
δ      = min(F_z / k_tire, 0.5 · sidewall)
L      = 2√(2rδ − δ²) + √(2r·z_rut − z_rut²)·w_mud
b      = width · (1 + clamp((20 − psi)/60, −0.1, 0.25))
p      = F_z / (b L)
```
Airing down lengthens L and widens b → lower ground pressure → less sinkage, more shear area.

## Soil: Bekker pressure–sinkage, applied per heightfield cell
```
p_bear(z) = k_eq · z^n                 (Bekker 1969; k_eq = k_c/b + k_φ)
ż         = max(0, p − p_bear(z_comp)) / η(m)       (viscous sinkage rate)
```
`z_comp` is per-cell accumulated compaction, so repeated passes firm up a rut until it carries the
load (Bekker's repetitive loading behaviour). 25 % of displaced volume is pushed into side berms.

**Calibration.** Wong's tabulated in-situ soils (`kWongSoils`, e.g. heavy clay 40 %: n = 0.11,
k_c = 1.84, k_φ = 103.27, c = 20.69 kPa, φ = 6°) predict < 1 cm sinkage for a 56 kPa tire —
real bog pits are excavated, saturated slurry. `MudModel` keeps Bekker's form but is fitted so
56 kPa sinks ≈ 5 mm / 2 cm / 10 cm / 60 cm at moisture 0 / 0.3 / 0.6 / 1.0 (log-linear k_eq),
with `η` from 2·10⁶ (dry) to 7·10³ Pa·s/m (soup). Consequence: at 15 m/s a tire crosses a 25 cm
cell in ~17 ms and sinks a few cm; parked, it goes to the hardpan.

## Traction
**Firm ground** — Pacejka Magic Formula:
```
F_x = μ F_z sin(C atan(Bκ − E(Bκ − atan Bκ))),   B=10, C=1.65, E=0.97
F_y = −μ F_z MF(α),                             B=9,  C=1.35, E=−0.5
κ = (ωr − v_x) / max(|v_x|, 2.5),   α = atan2(v_y, max(|v_x|, 2))
μ = lerp(0.80, 0.62, grass) · (1 − 0.38·wetness) · tread · water film
```
**Mud** — Janosi–Hanamoto shear on the contact patch:
```
F_x = (A c + W tan φ) · [1 − K/(iL) · (1 − e^(−iL/K))]
i   = (ωr − v_x) / max(|ωr|, |v_x|)          (slip, −1..1)
```
Grousers (tread lugs) multiply effective cohesion area (A/T 1.0, M/T 1.6, paddle 2.6). Tread
packing `p ∈ [0,1]` scales c by (1 − 0.7p) and tan φ by (1 − 0.5p). The two models are blended
by `w_mud = smoothstep(0.02, 0.18, mudDepth) · smoothstep(0.3, 0.65, moisture)`, then limited
by a friction ellipse.

**Resistances** (opposing travel; at standstill they act as static hold — you must out-pull them):
```
R_c = b k_eq z^(n+1) / (n+1)                          compaction (Bekker)
R_b = b (½ γ h² K_p + 2 c h √K_p),  K_p = tan²(45° + φ/2)   bulldozing (Rankine passive)
R_s = stickiness(m) · A · 3.5 kPa · (h / ½r)           suction
R_d = ½ ρ_slurry C_d b h_buried v² + ½ ρ_w C_d b min(d_water, r) v²   slurry / water drag
```
**Wheel spin** is integrated semi-implicitly in the tire's slip stiffness to stay stable at low speed:
```
ω ← ω + dt (T_drive − F_x r − T_churn) / (I_eff + dt (r ∂F_x/∂ω + 2 k_churn |ω|))
T_churn = k_churn ω|ω|         paddle-wheel drag of a tire spinning in slop
```
**Excavation**: depth rate `0.006 · dig_tread · (0.35 + 0.65 m) · min(v_slip − 1, 8) · load_frac`
(saturates — lugs only carry so much per revolution; unloaded tires can't dig). Removed volume is
emitted as mud chunk particles that re-deposit where they land.

**Tread packing**:
```
ṗ = +0.6 · stickiness(m) · w_mud · pack_rate · min(1, |ωr|/3)
    − max(0, ω² r / g − 14) / 18 · clear_rate          (centripetal fling-off above ~14 g)
stickiness(m) = exp(−((m − 0.65)/0.18)²)               (peaks near the plastic limit)
```

## Drivetrain
```
G = gear_ratio · axle_ratio · (low ? 2.72 : 1)         3-speed auto (2.48 / 1.48 / 1.00, R 2.08)
rpm_wheel = |G · ω_driveshaft| · 60/2π
rpm       → max(rpm_wheel, idle + throttle·(stall − idle))    torque-converter slip
TR        = lerp(2.1, 1.0, smoothstep(0, 0.85, SR)),   SR = rpm_wheel / rpm
T_wheels  = |G| (η T_e(rpm) TR − sgn(ω) T_fric SR²)
I_reflected = I_engine G² SR² / n_driven
```
Rev limiter = fuel cut with 260 rpm hysteresis (limiter bounce). Turbo engines lose 38 % torque
off-boost; boost spools at 1.4 s⁻¹. Open diffs split torque equally (so one unloaded wheel steals
it all); lockers and the part-time transfer case are velocity constraints solved by
inertia-weighted averaging of wheel speeds after integration.

## Chassis contacts, water, thermals
* Belly points (frame corners, center, bumpers) + **diff pumpkins** 0.17 m below each axle:
  `F_n = k·pen − c·v_n`, soft in mud (3·10⁴ vs 1.6·10⁵ N/m), Coulomb + plowing friction, and
  suction `−min(9 kN·soft·(½ + stickiness), 6000·v_n)` when lifting.
* Water: body drag `½ρC_dA|v|v` on the submerged frontal area, buoyancy on 22 % of displaced
  volume, splash events on entry. Intake below water/slop → **hydrolock** (4 s stall).
* Thermals: `Ṫ = 4.2·load + 0.35 − 0.11 (T − 70)(0.6 + 0.4 v/12)(1 − 0.82 clog)`;
  derate from 118 °C to 45 % torque at 132 °C. Radiator clogs when the nose plows mud, washes
  out in deep water.

## Verified behaviours (`tests/test_main.cpp`)
* Stock rig (31" A/T, open diffs, 2WD) in soup: < 3 m of progress, digs ≥ 15 cm hole, no rollover.
* Water-topped pit: stock 28 m < default Comanche 50 m < built (40" paddles, lockers, 454) clears 60 m.
* Firm ground: > 12 m/s in 5 s, stays upright, brakes to a stop then rolls into reverse.
