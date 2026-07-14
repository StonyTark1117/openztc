# OpenZTC roadmap and architecture

OpenZTC is developed as its own project. We do not send changes upstream;
upstream improvements can still be merged in when useful.

## Where the project stands (July 2026)

The menu shell is complete and verified against the real Complete Collection
data: resource pipeline (ZTD archives, PE resources, palettes, the custom
`.ani` graphics format, TGA/BMP quirks), a data-driven UI system with ten
element types, credits, scenario selection and freeform map selection, game
cursors, deterministic resource loading and an animation cache. What does not
exist yet is the game: no map view, no simulation, no saving.

## Language: staying with C++20

Considered: continuing in C++20, or rewriting in Rust before gameplay work.

Staying with C++20 is the right call:

- The hard, boring work — the resource formats — is done and debugged here.
  A rewrite throws away the most expensive thing we have.
- SDL3 and the vendored C libraries (libzip, pe-resource-loader, freetype)
  are C APIs; C++ wraps them with zero friction.
- Every major classic-game reimplementation that reached maturity (OpenRCT2,
  OpenTTD, openage, devilutionX) is C or C++. That is where contributors for
  this kind of project are.
- Two prior Rust reimplementation attempts of ZT1 exist and both stalled at
  an early stage. The graveyard is instructive.

Mitigations for C++ risks, adopted going forward: new code prefers
`std::unique_ptr`/explicit ownership comments over raw owning pointers,
Debug CI builds with ASan/UBSan, and unit tests for parsers (the first test
target is the `.zoo` loader against all ~40 shipped maps).

## What we can do better than the original

Already delivered: 64-bit, native Linux/macOS/Windows, threaded loading,
deterministic resource overrides, no CD checks.

Planned, roughly in order of appearance:

- Arbitrary window sizes and resolutions (original is fixed 800×600),
  UI scaling, camera zoom.
- Uncapped render framerate decoupled from a fixed simulation tick;
  fast-forward speeds beyond the original's.
- GPU sprite batching with texture atlases (original is a 2001 software
  blitter).
- Larger maps (below).
- Quality-of-life: autosave, faster loads, windowed mode, screenshots,
  modern pathfinding for guests/animals if the original's proves poor.
- Modding: the loose-file resource paths already work; a documented mod
  folder plus load-order config is cheap to add.

## Map sizes

The `.zoo` format stores map dimensions as two 32-bit integers; vanilla maps
are 75×75, 125×125 and 150×150. Community experiments show the original
engine loads hacked maps up to 233×233 and freezes beyond that — a limit of
the 2001 engine, not the format. Our engine sets its own ceiling. Terrain is
10 bytes per tile, so even a 512×512 map is ~2.6 MB of terrain — trivial.
The renderer and pathfinding must be written with culling and hierarchy so
big maps stay fast; that is a design requirement from day one, not a
retrofit. Vanilla-size saves stay byte-compatible; oversized maps are simply
saves with bigger dimensions, which the original engine will not load.

## Multiplayer

Feasible, and cheap **if** the simulation is architected for it now:

1. **Fixed-tick simulation** completely separated from rendering and UI.
2. **All mutations go through a serializable command type** (`GameAction`:
   place fence, adopt animal, set price…). The UI never touches game state
   directly. This is the OpenRCT2 model: multiplayer is "send the actions to
   everyone and run the same deterministic sim".
3. **Determinism discipline** in the sim: integer/fixed-point math where
   possible, one seeded RNG stream advanced only by sim code, no iteration
   over containers with unspecified order when it affects outcomes.
4. State checksums per tick to detect desync when networking arrives.

Networking itself (lobby, transport, sync) comes much later, but items 1–3
cost almost nothing now and are near-impossible to retrofit.

## The `.zoo` save format

Reconnaissance on the shipped maps plus community research (Gymnasiast's
format notes, the 233×233 experiment) gives us a strong starting point:

- Magic `TZFB` + a variant byte, version, language id, campaign type,
  map X/Y dimensions.
- Exhibits: count-prefixed records with name, entrance, rotation and six
  float money fields; tank/show-tank variants flagged in an extension field.
- Terrain: a flat stream of `X*Y` 10-byte elements — 32-bit height, a shape
  bitfield byte (raised corners), a terrain-type byte, four unknown bytes.
- Objects: category/name strings, position in 1/64-tile units, rotation,
  age, instance name.
- All strings are 32-bit-length-prefixed, no terminator. Later sections
  (money, research state) are still undocumented.

Unknowns are workable: we run the original game under Wine on the same
machine, so we can make one change in the real game, save, and diff the
bytes. That loop is our reverse-engineering engine.

## Relationship to other projects

- **sharkwouter/zt1-engine** (upstream): we merge from it opportunistically;
  we do not PR into it.
- **OpenZT (openztcc)**: a Rust DLL injected into the *original* exe for
  modding/fixes. Different approach, complementary — their detour work
  documents original-engine internals we can learn from.
- No from-scratch reimplementation we found is further along than this one.

## Milestones

Each milestone has an acceptance test. A milestone is done when its test
passes, not before.

### 1. Hardening (before any gameplay code)

- Split `src/` into `src/engine/` (resources, formats, window, input,
  cursors, fonts), `src/ui/` (unchanged) and `src/game/` (screens,
  simulation).
- Fixed-timestep main loop: render as fast as allowed, simulate at a fixed
  tick through an accumulator. The simulation object owns a tick counter
  and a single seeded RNG.
- `GameAction` command bus skeleton: a serializable struct, a queue on the
  simulation, actions applied only at tick boundaries. The UI never mutates
  game state directly.
- Unit test scaffolding (doctest) with a `tests` build target and the first
  tests for `IniReader`.
- CI: a Debug job with AddressSanitizer/UBSan that runs the tests.
- *Acceptance*: menu works as before; tests green in CI with sanitizers.

### 2. The `.zoo` reader

- `src/engine/ZooFile.*`: header, terrain stream, exhibits, objects, with
  unknowns preserved as opaque blobs.
- A `dumpzoo` developer tool that prints a summary of any `.zoo` file.
- Byte-diff research loop against saves made in the real game under Wine
  for the undocumented sections.
- *Acceptance*: all shipped maps (~40, all three sizes and all five header
  variants) parse without error; unit tests lock the header/terrain layout.

### 3. Terrain rendering (ZTMapView v1)

- Isometric heightmapped terrain from parsed maps: tile sprites by terrain
  type, height steps, edge cliffs; camera pan and zoom with culling.
- The freeform screen's Play button loads the selected map into the view.
- *Acceptance*: side-by-side screenshot of the same map in OpenZTC and the
  original under Wine is recognizably the same terrain.

### 4. Objects on the map

- Fences, buildings, paths and foliage from the object records, rendered
  through the existing `.ani` pipeline with correct isometric sort order.
- *Acceptance*: a vanilla map renders with its placed content and stays
  above 60 fps at normal zoom.

### 5. Simulation skeleton

- Fixed-tick world state: money (from `economy.cfg`), a clock/calendar,
  guests spawning at the gate and pathing on paths, animals as data-driven
  agents from the `.uca`/`.ucb`/`.ucs` config family. Iterative from here
  on; this milestone only needs guests to walk and money to tick.

### 6. Interaction

- Build tools (paths, fences, terrain painting) emitting GameActions;
  placement rules and costs.

### 7. Later

- Save writing (round-trip a vanilla save), oversized custom maps,
  multiplayer transport on the GameAction bus, mod loading conveniences.
