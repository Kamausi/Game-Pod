# Multiplayer netcode

```
browser A ──WebSocket──┐                         ┌──WebSocket── browser B
  C++ sim (authoritative │   server/server.js      │  C++ sim
  for its own truck)     └─► rooms · lanes · relay ◄┘
                             leaderboards (REST)
```

* **Topology**: client-authoritative vehicles, server relay. Each client simulates its own truck
  at 240 Hz and its own copy of the terrain; remote trucks are interpolated replicas. This keeps
  the terramechanics local (no deformable-terrain sync cost) and latency-free for the driver.
  Remote trucks cosmetically scuff your local mud.
* **Lobby protocol** (JSON text frames): `hello`, `quick`, `join {room}`, `start {mode}` (host
  only), `chat`, `cfg`, `ping {c}` → `welcome {id}`, `room {players, host, lanes}`,
  `start {mode, startAt, lanes}`, `pong {c, s}`.
* **Snapshots** (binary, 96-byte frame, 72 used, little-endian, `NetSnapshot`): id, seq,
  server time (f64), position (f32×3), velocity and quaternion (int16-quantized), rpm, throttle,
  steer, gear, boost, dirt, wheel ω ×4, compression ×4, mud slap / water speed / lug rate for
  remote audio, live score and distance. Sent at **20 Hz**; server caps 40 Hz per client and
  overwrites the sender id (anti-spoofing).
* **Clock sync** (NTP-style): `offset = ((t₁ − t₀) + (t₂ − t₃)) / 2`, keeping the sample with the
  lowest RTT out of the last 8 pings. All clients render remotes at `server_now − 120 ms`.
* **Interpolation**: cubic Hermite on position using snapshot velocities as tangents,
  slerp on orientation, lerp on everything else; extrapolation capped at 250 ms, then hold.
  Out-of-order / duplicate sequence numbers are dropped.
* **Synchronized starts**: host sends `start`; the server stamps `startAt = now + 4.5 s` and
  every client counts down against the shared clock, so green lights within a frame of each other.
* **Leaderboards**: `POST /api/score` with plausibility bounds per board (e.g. a full 80 m pass
  can't beat 2.3 s), name sanitization, per-IP rate limit, top 100 kept in
  `server/data/leaderboards.json`. Results are also kept locally (`localStorage`) when offline.

## Hardening roadmap
Client authority means a modified client can lie about its position/results. For ranked play:
server-side replay validation (clients upload input logs + seed; a headless native build of the
same C++ sim re-simulates — the sim is already platform-independent and deterministic per build),
or move to authoritative simulation on the server with client-side prediction.
