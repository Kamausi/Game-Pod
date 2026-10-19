---
name: mbcad-vehicle
description: Model or upgrade a drivable vehicle (truck, buggy, UTV, ATV) in the Mud Buddies .mbcad CAD language to high fidelity — body shaping with deform, lofted panels, shared lamp/wheel parts, budgets and LOD, and the render-and-verify loop. Use whenever a vehicles/*.mbcad part is created or improved.
---

# High-fidelity vehicles in mbcad

Vehicle bodies live in `cad/parts/vehicles/<body>.mbcad` (body frame: +Z forward, +X left, y up,
origin at frame height). Wheels, axles, links and coilovers are drawn live by the game from
`wheels/` and `gear/` parts, so never model running gear into the body.

## The brief (fill these in before modeling)
- **Identity:** make/era/class, the three forms that make it recognisable (e.g. square-body:
  flat hood with a crease, egg-crate grille, stepped bed sides).
- **Hard points (must match `src/physics/specs.cpp`):** front/rear axle z, track, wheel radius,
  belly height. Put them in the header comment and as `set FA/RA/...`.
- **Section heights:** rocker, beltline, hood, roof; overhangs front/rear; body width `W`.
- **Budget:** 30k–70k triangles full detail; `lod=1` must drop to roughly the unshaped count.

## Build order
1. **Primary forms** — side-profile `extrude`s with `arc(...)` wheel arches for the lower body,
   greenhouse glass as one `extrude` (material glass) with roof skin and pillars around it, bed
   or tub. Body shells get `bevel=0.075–0.10`.
2. **Whole-body shaping** — at the top of the part:
   ```
   param lod = 0
   set _segmul = 1.35-0.35*lod
   set _bevelmul = 1.5-0.5*lod
   deform on=1-lod res=0.09 tumble=0.10 ty0=<beltline> ty1=<roof> barrel=0.03 by0=<rocker> by1=<beltline>
          bow=0.015 bz0=<rear> bz1=<front> taperf=0.06 tf0=<cowl> tf1=<nose> taperr=0.025 tr0=.. tr1=<tail>
          crown=0.045 cy0=<below hood> cy1=<hood> hw=<W/2>
   ```
   Everything in the part (and its `use` children) bends with it, so details stay on panels.
3. **Moulded panels** — `loft` for fenders/flares (half profile around an arc of stations over the
   wheel, placed with `pos=W/2,...  mirror=x`), hoods, tanks, seats; `loft ... full=1` for
   asymmetric sections such as bumpers and tailgate caps run along X with `rot=0,90,0`.
4. **Shared assemblies** — `vehicles/headlamp.mbcad` (round or rectangular, `n` reflectors),
   `vehicles/taillamp.mbcad` (placed with `rot=0,180,0`), `vehicles/interior.mbcad` (cab, seats,
   driver), `vehicles/kit.mbcad` (recovery points, winch, snorkel), `vehicles/bumper.mbcad`.
5. **Details last** — panel gaps, handles, mirrors, badges/plates (`text`), wipers, seals, flare
   bolts, lettering. Decals sit 4–10 mm proud; on clear-coated paint at grazing angles use matte.

## Verify (every iteration)
```
./build-native/mbcad check                       # must be all ok
./build-native/mbcad info vehicles/<body>.mbcad   # triangle count (and lod=1)
source /opt/emsdk/emsdk_env.sh; touch src/main.cpp; cmake --build build-wasm -j8
node server/server.js &                          # if not running
cd tools/shots && node vehicle.js <body> <tire> -130 -20 "fq:4.4,1.9,5.6,0,0.7,0.6,45" "rq:-4,1.8,-5,0,0.7,-1,45"
```
Check the renders for: dark (inverted) faces, floating or buried details, decals fighting the
paint, parts poking through bodywork, the silhouette against reference. Then
`./build-native/mb_tests` and `python3 tools/make_standalone.py`.

## Gotchas
- `use` children get their own variables; pass what they need as parameters. `_segmul`,
  `_bevelmul` are inherited automatically.
- Text faces +Z; on a side use `rot=0,90,0` (+X) or `rot=0,-90,0` (−X); flat on a roof `rot=-90,0,0`.
- Euler order is X, then Y, then Z (`v' = Rz·Ry·Rx·v`); to roll something around a ring on a
  side face, build it around Z in a sub-part and place that with one yaw.
- Zero-scaled primitives emit nothing: `scale=flag,flag,flag` is the feature switch.
