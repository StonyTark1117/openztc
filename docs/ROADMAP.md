# OpenZTC roadmap and architecture

OpenZTC is developed as its own project. We do not send changes upstream;
upstream improvements can still be merged in when useful.

## Where the project stands (July 2026)

The menu shell is complete and verified against the real Complete Collection
data: resource pipeline (ZTD archives, PE resources, palettes, the custom
`.ani` graphics format, TGA/BMP quirks), a data-driven UI system with ten
element types, credits, scenario selection and freeform map selection, game
cursors, deterministic resource loading and an animation cache.

The game exists now. The map view renders every shipped map — terrain with
the original's lighting rig and blend rules, cliffs, water, paths, fences on
slopes, buildings with their color replacements, Marine Mania tank water
with glass walls — and has been verified against captures of the original
running under Wine to ±1px geometry and ±2.2% color medians, at all four
camera rotations and both zoom levels (docs/RENDERING.md holds every
verified rule with the measurement that pinned it down). A fixed-tick
simulation moves money, spawns guests that walk the paths and animals that
wander and swim in their exhibits. The `.zoo` reader parses all shipped
maps and saves, and the writer round-trips every one of them byte
identical — including saves the original game wrote this month. The mod
manager is live behind the "Get New Zoo Tycoon Items" button, with
loadouts and no-restart apply. What does not exist yet is interaction:
build tools, placement, prices — the game plays itself but cannot yet be
played.

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

Largely decoded now, well past the community notes we started from:

- Magic `TZFB` + a variant byte, version, language id, campaign type,
  map X/Y dimensions. Header sizes vary per variant; the parser anchors
  on the first valid object record rather than trusting a fixed size.
- Exhibits: count-prefixed records with name, entrance, rotation and six
  float money fields; tank/show-tank variants flagged in an extension field.
- Terrain: a flat stream of `X*Y` 10-byte tiles, every byte decoded —
  height `i32` in whole steps (the NW corner's), a shape byte (2-bit
  corner offsets above `height - (shape & 3)`), the type byte, an edges
  byte (2 bits per edge: flat/sloped/cliff-up/cliff-down — derived data
  the original renders from without recomputing; zero it and the original
  draws a black void, so writers must maintain it), and 3 zero pads.
- Objects: three length-prefixed strings (category, subcategory, code),
  then a length and that many bytes — position in 1/64-tile units,
  elevation in 1/16 steps, rotation, an id, the display name, and typed
  state. Buildings keep their recolor choice 43 bytes into the state (an
  index into building.ai's shared palette list). Entities are variable
  length inside and are position-scanned; the guests' four color choices
  (shirt, pants or skirt, hair, skin) live in that undecoded interior.
- All strings are 32-bit-length-prefixed, no terminator.

The reverse-engineering loop that got us here: run the original under
Wine, make one change, save, diff the bytes. Recoloring one hot dog stand
is how the building color field fell. The reader parses all ~100 shipped
maps and saves, and `serialize()` round-trips every one byte identical.
`dumpzoo` prints headers, terrain grids, exhibits, objects and colors,
and `--roundtrip` verifies the writer.

## Relationship to other projects

- **sharkwouter/zt1-engine** (upstream): we merge from it opportunistically;
  we do not PR into it.
- **OpenZT (openztcc)**: a Rust DLL injected into the *original* exe for
  modding/fixes. Different approach, complementary — their detour work
  documents original-engine internals we can learn from.
- No from-scratch reimplementation we found is further along than this one.

## Milestones

Each milestone has an acceptance test. A milestone is done when its test
passes, not before. Status as of July 2026: milestones 1–4 are done with
their acceptance met (milestone 3/4's "recognizably the same" bar was
left far behind — the map view is verified to ±1px against the original
across seven scenes, four camera rotations and both zoom levels);
milestone 5 is partially done (money ticks, guests walk, animals wander
and swim); milestone 6 is the current frontier. Save round-tripping from
"Later" is done. The mod manager section is fully delivered.

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

### Built-in mod manager (independent of the gameplay milestones)

The main menu's "Get New Zoo Tycoon Items" button pointed at official
download servers that no longer exist. OpenZTC repurposes it as a built-in
mod manager, keeping the vanilla look of the screen. The engine's resource
path system already loads loose archives, so managing mods is managing
`.ztd` files and directories plus a load order.

Implemented: mods are `.ztd` archives in a `mods` directory next to the
game data. They load before the game archives so they win conflicts, and
earlier mods win over later ones. The screen lists them with their enabled
state; Enable / Disable, Move Up and Done drive it, state persists in
`mods/openztc-mods.txt` (plain text, one `+name`/`-name` line per mod in
load order) and changes apply on the next launch. Mods the manager has not
seen before come up enabled, like dropping a `.ztd` into the game
directory of the original. The manager never touches or distributes game
data.

Conflict display and loadouts are in: selecting a mod shows who
overrides whom, and `openztc-loadout-<name>.txt` files in the mods
directory (same format as the state file, create one by copying it)
appear in the list where Enable / Disable applies them.

Both of the remaining items — creating and editing loadouts in the
screen itself, and applying changes without a restart via a resource map
reload — are implemented. The screen is feature complete.

### Aquarium completeness (Marine Mania)

The tank water renders now, and the acceptance bar — med_kids' dolphin
show tank and hammerhead tank side by side with the original reading as
the same filled aquarium — is met (clean water within 2/255 per channel
of the original's captures). What we learned getting there: nothing in
the save stores a fill level. The wall art is three stacked glass bands
of two steps each, and a tank fills to the top of its six-step walls or
back up to the rim it was dug from, whichever is higher. The translucent
surface draws in painter order per tile; walls rising above ground show
the water column through their glass plus the dark frame kit; transient
edge ripples are re-placed at runtime because their records carry no
position.

Still open, now smaller:

- Sub/surface animal states driven by depth: `subswim` under water,
  dive and rise transitions (surface swim works).
- Tank equipment behavior: filters, show tank stands, the show
  schedule from the tank records.
- Guests watching through tank walls / from bleachers (they gather on
  the bleachers already via the simulation records).
- Water quality scum variants and fresh-vs-salt selection (no vanilla
  test case found in any shipped map).

### 7. Later

- Oversized custom maps, multiplayer transport on the GameAction bus.
  (Save writing is done: every shipped map and save round-trips byte
  identical, including saves written by the original this month.)

## How we verify against the original

The methodology that produced the ±1px/±2.2% numbers, so it can be
repeated: the original runs under Wine on the same machine. Pause it —
the simulation freezes — save, and screenshot; the save and the capture
now describe the same instant, entities included. Render that save,
solve the camera by template-matching a landmark, and compare. The
camera-rotate button and both zoom levels multiply one frozen save into
ten comparable views. Hard-won metric rules live in docs/RENDERING.md
and the git history; the two worth repeating here are that colour is
compared as the median of per-cell ratios (never a ratio of means), and
that a 1px alignment error on dithered ground costs ~7 MAD, so global
shifts get re-searched before concluding anything is worse. The one
thing this loop cannot reach is the original's per-tile texture-placement
seed; its output is statistically identical to ours and matching it
per-pixel would need disassembly, which is out of scope for a clean-room
project.
