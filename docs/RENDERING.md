# The verified rendering model

Everything below was measured against the original game — its shipped data
files, its saves, and pixel comparisons with captures of it running under
Wine. Where a rule carries a number, that number came out of a comparison,
not a guess. The reference captures themselves contain original game pixels
and therefore live outside this repository.

## Terrain

**Geometry.** Tiles are 64x32 pixel diamonds at zoom 1 (`TILE_HALF_WIDTH`
31, `TILE_HALF_HEIGHT` 16). A height step is 16 pixels (`HEIGHT_STEP`),
and as a slope normal factor it is `HEIGHT_SCALE = 1/sqrt(6)` of a tile
width — confirmed by measuring flank brightness ratios on a synthetic
stepped pyramid against the original (predicted 0.926, measured 0.924).

**Tile record.** Each save tile is 10 bytes: height `i32` (whole steps,
the NW corner's), shape `u8` (2-bit corner offsets above
`base = height - (shape & 3)`; bits 1:0 NW, 3:2 SW, 5:4 SE, 7:6 NE),
type `u8`, edges `u8`, 3 zero pad bytes. The `edges` byte is derived data
(2 bits per edge: 0 flat, 1 sloped, 2 cliff neighbour-higher, 3 cliff
neighbour-lower) — but the original renders from it without recomputing:
a save with real cliffs and a zeroed `edges` byte renders as a black void
in the original. Writers must recompute it after terrain edits; our reader
derives from corners instead of trusting it.

**Lighting.** The original lights the terrain mesh through Direct3D from
`terrain/tilevar.cfg`: material diffuse/ambient 1.0, light 0 direction
(-1,-10,0) at 0.7, light 1 (1,-1,0) at 0.4, no ambient terms. Directions
are direction-of-travel, so a vertex shades by `N . (-dir)`. Flat ground
comes out at 0.9794. The light axes map to the tile grid as world
x = map -y, world z = map +x — settled by a synthetic pyramid whose flank
ratios refuted the three other axis assignments.

**Textures.** Ground types ship 128x128 and cover one tile (128 texels to
the tile). Water, deep water, concrete and the waterfall filler ship
256x256 and stretch over 2x2 tiles — squeezing them onto one tile doubles
their grain against the original (feature-retention 0.61 at span 2 matches
the original exactly). Sampling is point sampling; the linear filter
halves the original's local contrast on sand and does not reproduce its
water either. Per-tile stamp orientation is mirrored by a position hash to
break up the repeating grid; the original's own per-tile texel placement
matches no orientation of a per-tile stamp (winner analysis over 1295
cells split 69/96/104 with a 1.9 MAD margin where a wrong orientation
costs ~30), so it likely translates UVs from a seed internal to zoo.exe.
Its placement is statistically identical to ours and not reproducible
without disassembly, which is out of scope.

**Blending.** Where a different type touches a tile — including
diagonally — its texture splats over the tile, weighted per corner by
`count of that type among the 4 tiles at the corner / 4`, with no
priority between types. Three refinements, each measured:

- Ground-ground pairs pinch to zero alpha at the tile centre (a lone
  gray rock tile in sand measures half sand: 49.9% vs the model's 50.0%).
- Pairs with a two-tile texture on either side — water against a shore —
  interpolate through the centre instead (mean of the corners), which
  stretches the shore feather to the roughly two tiles the original
  shows (fshore coast envelope RMS 0.070 -> 0.047). A wider 4x4 corner
  window adds nothing over this rule.
- Corners only count neighbours whose per-tile corner height claim is
  within 1.5 steps: one-step terraces feather (Death Mountain), deeper
  drops keep the original's hard edge (tundra's walled pond).

Concrete and asphalt carry `blend=0` in the tiletex configs and take no
part in blending.

**Cliff faces.** The rocky wedges come from fringe.ztd, one sprite per
height step of a tile edge. Sprite 0011 is the band (one step at both
ends); 0001/0010 are the high flank rising/falling to the right;
0110/1011 the low flank. Each spans a step at one end only and carries
its art at its own height in the 64px canvas, so pieces are placed where
their own high edge meets a running cursor rather than sharing a top.
The band count for an edge is `left_steps - the wedges' own left_steps`
(both flanks tilt on 25.9% of shipped cliff edges). The original also
shows about the same count of single-pixel black seams we do (35 vs 56
on the worst synthetic map) — chasing them to zero moves away from it.

## Objects

**Records.** Object records are three length-prefixed strings (category,
subcategory, code), a length field and that many bytes of data: x, y
(64ths of a tile), elevation (16ths of a height step, the anchor height
the original computed at placement), rotation, an id, a length-prefixed
display name, then per-type state. Entities (guests, animals, staff)
have a different interior layout and are position-scanned instead.

**Anchoring.** Sprites anchor by the box their `.ani` declares around
the anchor point, snapped to whole pixels like the original's blitter;
art without a box anchors bottom-centre. Rotation steps 2 per quarter
turn clockwise, rotation 0 facing NW.

**Paint order.** Back-to-front by screen depth rows of whole tiles.
Fence and tank wall pieces attach to the further-back flanking tile and
draw right after that tile's own objects (phase 1): the original hides a
rock behind a wall yet lets a tree in the diagonal neighbour droop its
crown over it. Tank water surfaces join the order at phase 2 of their
tile.

**FATZ backgrounds.** Animations whose file opens with `FATZ` carry a
background frame at the end of the frame list holding the static image;
the cycle frames carry only the moving pixels. 75 object codes — every
shop, restaurant, bathroom and animal house — reduce to an animated
speck if the background is not drawn first.

**Building colors.** Color-replaced buildings ship neutral gray art with
16 recolorable palette entries after the `[cr_color] ncolors` fixed ones.
The ai's `[colorrep]` names the default 16-color ramp out of building.ai's
shared `[cr_part1]` list (blue 0, green 1, teal 2, gold 3, gray 4,
grayb 5, lime 6, orang 7, purp 8, rose 9, steel 10, tan 11, yello 12,
red 13, brwn 14, palyel 15, fucia 16, watmel 17, burg 18, choc 19,
ulmar 20, pink 21, sky 22, bone 23). Two-ramp buildings name both
palettes semicolon-separated; the ramps sit back to back in the art's
palette. The placed record stores the player's choice as an index into
that list, 43 bytes into the state after the display name — found by
recoloring a hot dog stand in the original and diffing the save (gold 3
became sky 22); default-placed buildings store their defaultpal's own
index.

## Tank water (Marine Mania)

Tanks are regions enclosed by tank wall pieces on tile edges. Nothing in
the save stores a water level: the wall art is three stacked glass bands
of two steps each, so a tank fills to the top of its six-step walls or
back up to the rim it was dug from, whichever is higher. The surface is
the salt `top` diamond from XPACK2/water.ztd at half opacity (clean
surface samples match the original within 2/255 per channel), drawn in
painter order. Walls rising above the outside ground show the water
column through their glass — the `front`/`right` side views per half-edge
segment per step, with the grey glass pane over them, both at half
opacity — plus the dark frame from the tank1 kit: rails at the bottom and
top, a post-only band between, in left-corner/middle/right-corner/column
variants picked by where the wall's run stops. Transient edge ripples
(their saved records carry no position) lay one `edgrip` along each wall
edge of a surface tile at the water level.

The show tank floor type 17 is `[ttgunnite]` from XPACK2/terrain6.ztd —
pool-lining concrete, legitimately blue.
