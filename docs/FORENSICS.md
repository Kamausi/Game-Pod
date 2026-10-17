# Reference forensics

What the six reference photos show, and the design decision each observation drove.
Coordinates and numbers in brackets point at where it lives in code.

> **About video/audio references:** only still photos were attached to the request — no video
> clips. Everything about sound below is therefore modeled from the visible hardware plus
> established mud-bog / vehicle acoustics, not measured from your footage. Drop clips into
> `tools/audio_probe` (see [AUDIO.md](AUDIO.md#calibrating-against-your-footage)) to measure firing
> frequency, brightness and transient rate, then tune the voices to match.

## Site and conditions

| Observation | Evidence | Design consequence |
|---|---|---|
| Upstate-NY / Great Lakes farmland in mid-October | Aspen / poplar with yellow crowns, staghorn sumac turning red, dried teasel and goldenrod, rolling glacial-till fields, guardrail road at the far edge | Tree line of yellow poplar + a few spruce, red sumac by the bus mound, teasel at fence posts, goldenrod clumps (`cad/parts/vegetation/*`, `World::generate`) |
| Clear, low-humidity afternoon, sun behind/left of camera | Deep saturated blue sky, hard short shadows to the lower right | Single warm sun from the south-west, deep blue sky gradient, light haze only (`Renderer` `sunDir`, `skyColor`) |
| Soil is clay-loam till: tan when dry, chocolate-to-black when worked | Track ruts and the pit floor in photos 3-4; tread imprints hold their shape | Calibrated clay slurry model; ruts persist when moisture is moderate and flow back in when soupy (`MudModel`, `Terrain::relax`) |
| Standing water in the low spots of the pit, not a flooded pit | Photo 4: puddles + sheen between ruts | Water table set just below the pit floor so only pools and dug ruts fill (`water = hPit - 0.53`) — ruts you dig become water-filled |

## The pit and the event

| Observation | Evidence | Design consequence |
|---|---|---|
| Excavated rectangular drag pit, spoil pushed into berms | Photo 3 (pit with mounded grass edges), photo 4 (graded mud flat) | 80 m x 26 m pit, ~0.4 m below grade, hardpan ~1.3 m down, berms both sides, entry/exit ramps (`World::generate`) |
| A crawler dozer parked in the mud | Photo 4: yellow dozer with ROPS canopy and straight blade (reads as a Deere 450-class machine) | Modeled dozer prop with idle diesel audio; it is the "tow" service when you get stuck (`props/dozer450.mbcad`, `ModeRunner::requestTow`) |
| Orange HDPE safety fence on green steel T-posts, sagging and partly flattened | Photos 4-6: fence lying in the weeds, posts leaning | Procedural fence shader with the diamond/oval hole pattern; posts are colliders that lean permanently when hit and drag the fence down; 12% pre-flattened (`kFenceFS`, `World::stepDynamics`) |
| 55-gal drums as lane/course markers: black with yellow sponsor band, plus faded blue drums | Photo 3 (PJ1-branded drums) | Two barrel variants, dynamic (knock them over: sound + crowd + freestyle points). Sponsor text not reproduced — no third-party trademarks in assets |
| Spectators lined up behind the fence, puffer vests, jeans | Photo 3 | Crowd line on the north fence with randomized outfits; crowd murmur and cheer synthesis keyed to distance (`props/spectator.mbcad`, crowd voice) |
| Multiple lanes / vehicles in the pit at once | Photo 3: several trucks and a UTV in the bog | 4-lane drag format for multiplayer; lane assignment by the server |

## Vehicles

| Seen | Identification | In game |
|---|---|---|
| Grey compact pickup, lifted, black plate bumper with amber lamps, chrome mirror, whip antenna, black trimmed flares, ~33-35" mud tires on black steel wheels | 1986-92 Jeep Comanche MJ (XJ-family front clip) | `comanche` body, 4.0 I6 default, 35x12.5 MT, 4" lift, rear locker (`specs.cpp`, `vehicles/comanche.mbcad`) |
| White full-size sedan, "1976" on the glass | Mid-70s GM full-size (Caprice/Impala class) run as a bog car | `bogcar76` body, 454 big-block, 2WD by default (`vehicles/bogcar76.mbcad`) |
| Mud-caked crew-cab pickup, roof light bar, **hood propped open** | GMT800-generation Silverado/Sierra HD | `silverado` body with Duramax-style turbo diesel. The open hood inspired the **radiator clogging + overheating** mechanic (`Vehicle::radiatorClog`, `engineTemp`) |
| White/black side-by-side with roll cage | Sport UTV (RZR-class) | `utv` body, turbo parallel twin, IRS (no diff pumpkins to high-center on) |
| Abandoned conventional school bus in the brush pile | 1970s-80s conventional bus, yellow roof cap with name board, smashed glass, blue hood | Static prop on the spoil mound with a bleached brush pile (`props/schoolbus.mbcad`, `vegetation/brushpile.mbcad`) |

## How trucks get stuck (what the physics reproduces)

Observed failure modes in mud bogging, each mapped to a modeled mechanism:

1. **Sinkage to the hardpan** — slurry has almost no bearing strength; tires press through until
   they hit firm clay. Bekker pressure–sinkage with a viscous rate term, per terrain cell. Speed
   matters: at 15 m/s a tire spends ~20 ms on a cell and barely sinks (*momentum carries you*).
2. **Digging in** — wheelspin excavates and throws mud (roost). Volume is removed from the
   heightfield and redeposited where the particles land, so spinning in place digs holes.
3. **High-centering** — once tires sink, axle housings (diff pumpkins) and frame rails touch.
   Belly contacts carry weight → tires unload → no traction. Suction resists lifting off.
4. **Bulldozing** — a sunk tire must push a wall of mud ahead of it (Rankine passive pressure).
5. **Tread packing** — sticky clay fills the lugs and turns an MT into a slick; only high wheel
   speed (centripetal acceleration > ~14 g) flings it out. That is why boggers keep the wheel speed up.
6. **Open differentials** — one unloaded wheel spins, the loaded one gets nothing. Lockers fix it.
7. **Water** — hydrodynamic drag on tires and body, bow-wave splash, and **hydrolock** if water
   reaches the intake (snorkel upgrade raises it).
8. **Overheating** — mud packed into the radiator + pinned throttle at low airspeed → engine temp
   climbs → power derate (the propped-open hood in photo 3).

The way out mirrors real technique: **rock it** (auto-reverse makes throttle/brake rocking work),
**air down** (longer contact patch, lower ground pressure), **lock it**, **4-LO**, or **call the dozer**.

## Sound sources implied by the scene

| Source | Character | Voice |
|---|---|---|
| Open-header V8s at the limiter | Lumpy idle lope, uneven cross-plane burble, "bwap-bwap" limiter bounce, pops on lift-off | Per-cylinder pulses in true firing order per bank → exhaust waveguides (`EngineVoice`) |
| Jeep 4.0 I6 | Smooth even-fire 120° hum, raspier at high rpm | Same model, single bank, even firing |
| Duramax-style diesel | Injector clatter, turbo whistle, blow-off on lift | Per-firing click + turbo tone/boost |
| Dozer idling | Slow 4-cyl diesel knock | Ambient spatialized `EngineVoice` |
| Tires in slop | Rhythmic "whump-whump" lug slap at wheel-speed × lugs | Granular low-passed noise + thump grains (`RoadVoice`) |
| Mud chunks hitting sheet metal | Rattling patter | Poisson click train scaled by roost volume |
| Tires pulling out of suction | Wet "schlorp" | Down-swept resonant band-pass |
| Water crossings | Broadband wash + bubble resonances | High-passed noise + Minnaert bubbles |
| Crowd behind the fence | Murmur, cheers, whistles | Pink-noise formants + cheer envelope |
| Tree line ~30-40 m away | Slapback echo | 190/230 ms cross-fed stereo delay |
