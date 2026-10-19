# mud-buddies-cad

The parametric CAD source for every Mud Buddies asset. The game builds its meshes from these
files at load time (they're embedded in the WASM at `/cad`), so editing a part here and
rebuilding changes the game — no export step, no binary art in git.

```
cad/
  parts/
    vehicles/   comanche, bogcar76, silverado, utv   (body frame: +Z fwd, +X left, +Y up)
    wheels/     wheel.mbcad  (tire + rim + beadlock, fully parametric: size, width, lugs, tread)
    props/      barrel, tpost, schoolbus, dozer450, spectator, fencepost_wood
    vegetation/ poplar, spruce, teasel, goldenrod, sumac, brushpile
  lib/
    mbcad.scad  OpenSCAD modules backing the .scad exports
```

## The language
One feature per line; `param` values can be overridden by the game or by `use`:
```
param tire_r = 0.4445          # 35" tire
set W = 1.74
color paint_r paint_g paint_b  # or #rrggbb
box  size=W,0.52,4.40 pos=0,0.12,-0.26
box  size=0.10,0.32,0.06 pos=W/2+0.02,0.04,1.80 rot=-25,0,0 mirror=x
cyl  r=0.29 h=0.88 axis=y seg=20 [r2=...]
sphere r=1.1 scale=1,2.2,1
wedge size=1,0.4,0.8
lathe   pts=r0,y0,r1,y1,...  seg=24 axis=y     # walk with the outside on your right
extrude pts=z0,y0,z1,y1,...  depth=1.98        # side profile extruded across the width
tire  r=tire_r w=0.32 rim=0.19 lugs=18 depth=0.028 tread=mt|at|paddle
rim   r=0.19 w=0.26 spokes=6
use   wheels/wheel.mbcad pos=... tire_r=0.5
material paint                 # sticky: plastic|paint|glass|chrome|rubber|lens|matte|steel|mud
box  size=1,0.5,2 bevel=0.04   # rounded edges with smooth normals (also on extrude)
extrude depth=1.74 bevel=0.05 pts=0.5,-0.1,arc(1.29,-0.30,0.55,159,21,12),1.95,-0.1,...
                               # arc(zc,yc,r,a0,a1,n): n+1 points on a circle (wheel arches)
text str=MUD_BUDDIES size=0.3 [t=0.07] [depth=0.02]   # block lettering, centred, facing +Z (_ = space)
for i = 0, 12 [, step]         # repeat the lines up to `end` (inclusive range, nestable);
  set a = i*30                 #   i (and anything `set` from it) works in every expression
  box size=0.38,0.045,0.13 pos=0.72,0.3+0.27*sin(a),0.27*cos(a) rot=90-a,0,0
end
```
Distance LOD: a part that declares `param lod = 0` and builds a cheaper mesh with `lod=1` gets that
version drawn beyond ~60 m (45 m Medium, 30 m Low) when it has 20+ instances (trees, weeds).

Materials drive the game's physically based shading (clearcoat paint, reflective glass,
chrome, satin rubber, glowing lenses); tires are always rubber. Mud is applied in-shader on
top, so parts are authored clean.
Expressions: `+ - * / ^ ( )`, `sin cos tan atan2 sqrt min max abs floor ceil round mod` (degrees), `pi`.

## Tooling
```
cmake -B build-native && cmake --build build-native
./build-native/mbcad check                                  # validate every part (CI runs this)
./build-native/mbcad info vehicles/comanche.mbcad paint_r=1
./build-native/mbcad export wheels/wheel.mbcad tire_r=0.5 tread=2 --obj paddle40.obj --scad paddle40.scad
./build-native/mbcad export-all out/                        # OBJ (Blender, vertex colors) + SCAD (OpenSCAD/FreeCAD)
```
CI publishes `export-all` output as a build artifact on every push.

## Using it as its own git repository
This folder is self-contained so it can live as a separate repo shared by artists:
```
git subtree split --prefix=cad -b cad-repo         # history of just cad/
git push git@github.com:<you>/mud-buddies-cad.git cad-repo:main
# later, in the game repo, track it as a subtree (or a submodule at cad/):
git subtree pull --prefix=cad git@github.com:<you>/mud-buddies-cad.git main --squash
```
`CMakeLists.txt` embeds `cad/parts` regardless of which way it's vendored.
