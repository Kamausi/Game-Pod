# Game Pod 3D Rendering Standard

> Every game should look like a premium miniature toy set photographed for a commercial,
> not geometry rendered inside a WebGL demo.

This applies to every Game Pod asset: baked tiles, icons and banners (`src/art/`), and live game scenes
(`src/components/Scene.jsx` and future games). Values are defaults; a scene may deviate when matching a
reference, but must say why in a comment.

## 0. Two render modes

| | Showcase mode | Gameplay mode |
| --- | --- | --- |
| Where | Game tiles, featured banners, menus, win screens, title/idle scenes (baked by `src/art/studio.js`) | Live play (`src/components/Scene.jsx` and future game scenes) |
| Camera | Composed key-art shot; cropping allowed and encouraged | Functional, stable, whole play area readable |
| Depth of field | Pronounced (aperture 0.006–0.012), focused on the hero pieces | Off, or barely perceptible |
| Lighting | Strong key/fill ratio, dark cavities, environment bokeh, enhanced reflections | Same rig, softer ratio; nothing may hide game state |
| Exposure | Per-shot grading | Stable across the session |

## Benchmark

**Tic Tac Toe is the canonical Game Pod visual benchmark** (`src/assets/art/tic-tac-toe.webp`, model in
`src/art/models.js`). A new game is not visually complete until its showcase render sits next to the benchmark
without looking like a different product: same bevel language, material response, lighting contrast,
grounding and backdrop depth.

## 1. Geometry

| Rule | Value |
| --- | --- |
| No untouched primitive in the foreground | Hero pieces come from the toy kit (`src/art/kit.js`) or custom lathe/extrude profiles. Primitives are only for parts that are hidden, tiny or blurred. |
| Bevel radius | 8–15% of the part's smallest dimension (frames/rails: 10–25%). Never a hard 90° edge on anything the key light can hit. |
| Bevel smoothness | ≥ 5 segments on rounded boxes, ≥ 8 on extrude bevels, ≥ 48 radial segments on lathes, ≥ 64 on hero rings/pucks. |
| Proportions | Toy proportions for readability at 200–400 px: frames 1.5–2× realistic thickness, pieces ~70–85% of their cell, oversized heads/handles on tools. |
| Shape character | Tapers and mass variation over uniform tubes (e.g. the X is thick at the centre, `arm` > `tip`). Flat-topped slabs with soft shoulders over cylinders for dividers. |
| Cavities | Playing cells sit visibly below their walls: cell depth ≥ 0.4 × cell wall height. |
| Lathe profiles | Run bottom-to-top. Enforced by `src/art.test.js` (an inverted profile renders as a hollow cup). |

## 2. Surfaces (PBR)

| Material | roughness | clearcoat / clearcoatRoughness | Notes |
| --- | --- | --- | --- |
| Painted toy plastic | 0.45–0.55 | 0.1–0.2 / 0.5 | Broad soft highlights. Default for game pieces. |
| Glossy moulded plastic | 0.2–0.3 | 0.8–1.0 / 0.08 | Checkers, rings, mallets. |
| Near-black plastic | 0.35 | 0.6 / 0.25 | Use `#1c1917`-ish, never pure black; `envMapIntensity` ≥ 0.4 so tops catch light. |
| Varnished wood | 0.5–0.6 | 0.2–0.3 / 0.4–0.5 | Always `woodTexture()` grain; grain contrast low (dark within ~10% of base) to avoid crack-like streaks. |
| Glossy casing | 0.15–0.2 | 0.8 / 0.1 | Board casings. |
| Metal | 0.15–0.2, metalness 1 | – | Darts, bands. |

- Avoid `transmission` on small saturated pieces: it picks up the background and shifts hue.
- **Variation:** every repeated piece goes through `vary(object, seed, ranges)`.
  Defaults: rotation ±1.5°, scale ±1%, height ±0.8%, colour value ±2.5%, roughness ±0.03.
  Glossy pieces (checkers): rotation ±2°, scale ±0.75%, value ±1.5%, roughness ±0.025.
- **Tactile surfaces:** large flat faces (board squares, trays) get `noiseTexture()` as a faint roughness + bump map.
  It must not read as distressed; it only varies the reflections.
- Canvas textures: 512–1024 px, sRGB, anisotropy 8.

## 3. Lighting rig

Defaults in `src/art/studio.js`; per-scene overrides in a model's `look`.

| Light | Default | Purpose |
| --- | --- | --- |
| HDR environment | `src/art/env/studio.hdr` (`npm run make-hdr`), intensity 0.4–1.0 | Reflections: warm key softbox, overhead strip, cool fill, pink/cyan rims. |
| Key | Directional, warm white `#fff3e6`, 1.4–3.2, from front-left above, soft PCF shadows (2048 map, radius 6) | Shape and shadow. |
| Fill | From the HDR's cool fill; warm scenes replace it with `look.ambient` hemisphere bounce | Keeps shadows from going black. |
| Rim | Fresnel rim layered on materials (`addRim`), intensity 0.04–0.35 | Silhouette separation. Lower it on large flat ground (it washes grass out at grazing angles). |

Mood: warm scenes (tavern, sunset) set `envIntensity` ≤ 0.15 plus `ambient`, so cool studio colours don't tint reds pink or blacks blue.

## 4. Grounding

- Scene tiles render through **GTAO**: radius 2.5–6% of scene radius, `distanceExponent` 2, intensity 1.0–1.7. Cavities that hold pieces should go near-black so pieces pop. Small radius only: large radii darken flat tops next to tall neighbours.
- Two shadow scales together: a tight, dark contact shadow right under each piece (GTAO) and a broad soft directional shadow (key light).
- Key-light shadow maps on every object (`castShadow`/`receiveShadow`).
- Objects without a scene get a shadow catcher plus a soft AO blob.

## 5. Camera

| Setting | Value |
| --- | --- |
| FOV | 24–34° (default 30). Tighter (24–26°) for heavy objects that should feel compressed. |
| Pitch | 35–60° for boards (0.6–1.05 rad); 10–25° for upright subjects (darts, characters). |
| Coverage | Hero subject fills 70–92% of the frame (`fill`), or deliberately crops for key-art close-ups. |
| Framing | Fit on the hero parts (`frame`); scenery is allowed to run off the tile. Prefer rotating the object over extreme camera angles. |

Key art is not the gameplay camera: tiles and banners are composed shots; in-game cameras prioritise readability.

## 6. Environment and depth

- Never a bare gradient: every tile has a painted out-of-focus backdrop (`backdrop`: colour masses, bokeh, and low-detail `shapes` such as cabinets, shelves, lamps and windows, all blurred) matching its reference's setting. More blurred structure gives richer bokeh than blobs alone.
- Physical context where it helps (a table under a board, grass around holes), allowed to fall out of focus.
- **Depth of field** (`look.dof`, BokehPass): aperture 0.003–0.008, maxblur ≤ 0.016, focused on the framed subject. The subject must stay sharp; check at 100%.

## 7. Post and output

- Tone mapping: ACES Filmic (`tone: 'aces'`), exposure 1.0; compared against AgX/Neutral with `npm run bake -- <asset> --compare` when colours look off.
- Bloom: alpha-preserving, threshold 0.82, strength ≤ 0.55. Never used to hide weak geometry.
- Render at 2× and Lanczos-downsample; WebP quality 90 (alpha 95).
- Live scenes: `dpr` capped at 2, MSAA on, sRGB output, same HDR (`studio-small.hdr`).

## 8. Budgets (live scenes, mid-range phone)

| Item | Budget |
| --- | --- |
| Frame time | 60 fps target, ≤ 16 ms; drop effects before resolution. |
| Triangles on screen | ≤ 300k; hero characters 15k–60k. |
| Draw calls | ≤ 150; instance repeated pieces. |
| Textures | ≤ 1024² per map; total ≤ 32 MB GPU. |
| Shadow maps | One 2048² directional. |
| Post | GTAO half-res and DOF only on menus/win screens/idle, not active gameplay. |
| Baked art | Tiles ≤ 40 KB WebP; whole single-file app ≤ 3 MB. |

LODs: not needed for tabletop scenes at current counts. Add them if a scene exceeds the triangle budget.

## 9. Animation (live games)

Pieces drop in with a short eased bounce (≤ 400 ms); win lines sweep in ≤ 400 ms; characters (e.g. moles) need
idle, blink, emerge, hit/squash and retreat states. Every motion respects the Reduce Motion setting.

## 10. Review process

1. Build or modify the model in `src/art/models.js`.
2. `npm run bake -- <asset>`, compare side-by-side with the reference crop.
3. Iterate until composition, colour, materials and proportions match; then check it in the app at phone size.
4. `npm test` (includes the geometry normals check) before committing.
