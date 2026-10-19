# Asset screenshots

Headless renders of the running game for reviewing CAD assets (used by the `mbcad-vehicle` skill
and the `3d-artist` agent). Start the server first (`node server/server.js`, serves :8787), then:

```
cd tools/shots && npm install
mkdir -p shots
# vehicle portraits: body, tire inches, warp x z, then name:dx,dy,dz,tx,ty,tz[,fov] relative to the truck
node vehicle.js silverado 37 -130 -20 "fq:4.4,1.9,5.6,0,0.7,0.6,45"
CFG='{"beadlock":true,"snorkel":true}' node vehicle.js comanche 35 -130 -20 "wheel:2.6,0.7,2.6,0.9,0.4,1.3,30"
# free-camera scene shots (truck parked out of view): name:px,py,pz,tx,ty,tz[,fov] (y above ground)
node shoot.js "grandstand:-2,4,-38,-15,2.5,-53,55"
```

Env: `SHOTS_DIR` (default `shots`), `GAME_URL` (default `http://localhost:8787/`), `CHROME`
(Chromium binary). The headless renderer is software GL, so each shot takes tens of seconds.
