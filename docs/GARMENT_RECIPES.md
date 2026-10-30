# Garment recipes: making garments with `./garment`

A garment is a JSON **recipe** in `garments/`: flat pattern pieces, where each one starts on the body, and seams that
join their edges. `./garment` expands the recipe on a body, simulates the cloth, photographs it and puts it in the
editor. You never need to read the Godot code to make a garment.

## The loop

```sh
./garment body                      # body sizes for expressions (girths, heights above the floor, metres)
$EDITOR garments/my-dress.json      # write the recipe
./garment check garments/my-dress.json   # pieces, edges with names and lengths, seams, ERROR lines with the fix
./garment drape garments/my-dress.json   # simulated: where each piece ends up, seam gaps, OK or ERROR
./garment shots garments/my-dress.json   # photos (front, 3/4, side, back): look at shots/my-dress/sheet.png
./garment wear garments/my-dress.json    # into the library and onto the mannequin in the running editor
```

Every command prints `ERROR ...` lines that say what to change, and exits 1 when there is one. Fix, run again.
`check` takes a second; `drape` about 15 s; `shots` about a minute (opens a window). `--body average|petite|tall|curvy|plus|athletic`
picks a preset body; the default is the user's own measurements.

## Recipe format

```json
{
  "format": "failoring-garment/1",
  "name": "Sun dress",
  "category": "dresses",
  "fabric": "cotton_poplin",
  "notes": "One line for the user.",
  "include": [ {"draft": "tank", "opt": {"hemY": "y_waist"}} ],
  "pieces": [
    {"id": "skirt", "shape": {"kind": "trapezoid", "top": "waist * 0.55", "bottom": "hip * 0.9", "h": 0.45},
     "around": 2, "wrap": {"theta": 0, "y": "y_waist - 0.45"}}
  ],
  "seams": [
    {"a": ["front", "hem"], "b": ["skirt1", "top"]},
    {"a": ["back", "hem"],  "b": ["skirt2", "top"]}
  ]
}
```

**Units are metres.** 0.45 = 45 cm. Any number can be an expression in quotes over the body variables
(`./garment body` lists them): girths `bust waist hip thigh knee ankle`, heights above the floor `y_neck y_shoulder
y_bust y_waist y_hip y_crotch y_knee y_ankle`, and `height`. So `"waist * 0.55"` or `"y_knee - 0.05"`; the garment then
fits any body.

**category**: tops, dresses, skirts, bottoms, custom. **fabric**: cotton_poplin, cotton_crepe, linen_medium,
silk_charmeuse, polyester_chiffon (very light, floaty), cotton_jersey (knit, stretchy), wool_suiting, denim_12oz,
canvas_heavy, lambskin_leather.

### Pieces

| key | meaning |
|---|---|
| `id` | unique name, used by seams |
| `shape` | `{"kind": "rect", "w", "h"}`, `{"kind": "trapezoid", "top", "bottom", "h"}` (wide bottom = flare), `{"kind": "triangle", "b", "h"}`. Edges are named: rect and trapezoid `bottom right top left`, triangle `bottom right left`. |
| `outline` | instead of `shape`: `[[x, y], ...]` counter-clockwise, y up. Edge `i` runs from point `i` to point `i+1`. Name them with `"edges": ["hem", "side", ...]` (one name per edge). |
| `around` | `N`: N copies round the body (ids `skirt1`..`skirtN`, `skirt1` at the front), each side seam sewn for you (copy i `right` to copy i+1 `left`). Use 2 (front/back) to 6 (gores). |
| `wrap.y` | height of the piece's **lowest point** above the floor where it starts. Its top is at `y + h`. Put each piece where it will hang: the top of a skirt at the waist means `y = y_waist - h`. |
| `wrap.theta` | where round the body its centre starts, degrees: 0 front, 180 back, 90 and 270 the sides. With `around`, the first copy's angle. |
| `wrap.ring` | set by `around`; for a single piece that goes all the way round, 1. |
| `wrap.clear` | gap to the body (default 0.012). Outer layers (a frill over a skirt): 0.03. |

### Drafted parts: `include`

Fitted parts drafted from the body, with all their own seams. Their piece ids and edge names show in `./garment check`.

| draft | pieces | edges to sew to | options (`opt`) |
|---|---|---|---|
| `tank` | `front`, `back` | `hem`, `neck*` (neckline), `arm*`/`armL*` (armholes, the two sides) | `hemY` hem height (default `y_hip`), `scoop` neckline depth (0.11), `armholeDepth` (0.17), `straightBelow` |
| `crop` | `front`, `back` | as tank | as tank; the hem is just above the waist |
| `shirt` | `front`, `back`, `sleeveL`, `sleeveR` | `hem`, `neck*`, sleeve `hem` (armholes are sewn to the sleeves) | as tank and `sleeveLen` (0.20), `sleeveHem` |
| `pants` | `frontL`, `frontR`, `backL`, `backR` | `hem` (leg hems), `waist` | `hemY` leg hem height (0.04: full length; `y_knee` for shorts), `ease` |

`"prefix": "b_"` renames the included pieces (`b_front`) when you include two drafts.

### Seams

`{"a": [piece, edge], "b": [piece, edge]}`. An edge is:

- a name (`"top"`, `"hem"`) or an index (`2`);
- a family `"neck*"`: all of a drafted piece's numbered edges with that name, in order. The other side must be one
  edge; it is shared out along them (a frill round a neckline: `{"a": ["frill1", "top"], "b": ["front", "neck*"]}`);
- part of an edge: `{"edge": "bottom", "from": 0.0, "to": 0.5}`.

A piece id made with `around` (`"skirt"`) stands for all its copies: `{"a": ["skirt", "bottom"], "b": ["frill", "top"]}`
sews skirt1 to frill1 and skirt2 to frill2 (both need the same `around`).

`reverse` defaults to true, which is right for two pieces side by side or one above the other. Leave it out.

**Gathers and frills**: sew a long edge to a short one; the longer side gathers. 1.3x to 2.5x looks like a ruffle;
over 3.5x is an ERROR. Every edge can be sewn once; leave hems and necklines open.

## What makes a garment hang right

- Something must hold it up: a bodice over the shoulders (`tank`/`crop`/`shirt` include), or a waistband that is no
  wider than the waist (top edges summing to about `waist * 1.05`). A skirt whose top is much wider than the waist
  slides to the floor (`drape` says SLID DOWN / ON THE FLOOR).
- Start each piece where it will hang, next to the pieces it is sewn to (sewn edges at the same height). Seams pull
  the edges together; edges that start far apart tangle.
- Flare comes from a wider bottom (trapezoid) or more panels; frills from gathering (long edge to short edge).
- Fabric: poplin and linen hold a shape; chiffon and charmeuse fall in soft folds; jersey clings.
- Keep pieces at least 3 cm in each direction and under 2 m; a hem tier 10 to 25 cm deep reads as a frill.

## Reading `drape`

```
PIECE skirt1  hangs 0.62..1.09 m (knee+15cm .. waist+0cm), top moved -0.4 cm
SEAMS widest gap 6.0 mm  (closed)
SETTLE fastest point moves 0.3 mm/frame at the end
OK drapes
```

- `hangs` is where the piece ended up, also named by the nearest body height. Check the hem is where you meant.
- `top moved` large and negative, `SLID DOWN`, `ON THE FLOOR`: nothing holds it up (see above).
- `SEAMS ... open` over 15 mm: a seam joins edges that start far apart, or the wrong edges.
- `--walk` simulates a few steps of walking too.

Then `./garment shots` and **look at the sheet**: the numbers cannot tell you whether it looks like the garment asked for.

## Examples

`garments/` holds working recipes to copy from: `tiered-skirt.json` (around, gathered tiers),
`peplum-top.json` (tank include, a frill sewn to the hem).
