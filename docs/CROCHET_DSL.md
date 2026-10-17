# Crochet pattern import: CrochetPARADE text plus `#!` extensions

This pins the bridge chosen in the crochet plan (`docs/CROCHET_PLAN.md`, sections 5.0/5.1,
decision B): patterns stay in **CrochetPARADE**'s language, and the few constructs its grammar
cannot write go into `#!` extension lines that CrochetPARADE reads as comments. Failoring
imports

* `patterns/cp/<name>.txt` — the pattern text (CrochetPARADE language and extension lines) and
* `patterns/cp/exported/<name>.json` — the exporter's dump of CrochetPARADE's parse,

into `fl::StitchGraph` through `fl::importPattern(jsonText, extensionText)` or
`fl::importPatternFile(jsonPath, patternPath)` (declared in
`crochet_core/include/failoring/crochet/pattern.hpp`, implemented in
`crochet_core/src/parse.cpp`; the graph types are in
`crochet_core/include/failoring/crochet/graph.hpp`).

The CrochetPARADE language itself is described in its own manual
(`third_party/crochetparade/Manual.md`, checked out separately; that manual is CC BY-NC-SA,
so it is only linked here and its text is not reproduced). This document pins only the
bridge: the JSON schema, the extension lines, and what is not modelled yet.

## 1. The export JSON

Produced by `tools/crochet/cp_export.mjs`; the schema is the one fixed by the plan, section 5.0:

```json
{"stitches": [{"id": 0, "type": "ch|slst|sc|hdc|dc|tr", "piece": "main", "row": 0, "index": 0,
               "loop": "both|blo|flo|fpost|bpost|space", "targets": [ids], "prev": -1, "line": 3}],
 "rows":     [{"id": 0, "piece": "main", "kind": "row|round", "turned": true}],
 "links":    [],
 "positions":[[x, y, z], ...],
 "errors":   [{"line": 7, "msg": "..."}]}
```

* `stitches` — one object per stitch, in made order; `id` is the stitch's position in this
  array. `row` is the exported row id, `index` its position in that row in made order
  (turning chains and join sl sts included). `loop` is the loop the hook entered, as written
  in the pattern (worker-relative: `both`, `blo`, `flo`, `fpost`, `bpost`, or `space` for
  chains); the importer adds the resolved facing `rs` and the RS-relative insertion
  `loopRs` to every stitch (section 3). `targets` are the ids the stitch was worked into, in
  written order. `prev` is the stitch made just before it in the working sequence (`-1` at a
  start). `line` is the 1-based source line in the pattern file. `piece` is `main` for every
  stitch today (CrochetPARADE has no piece construct).
* `rows` — one object per row or round, `id` running `0..N-1`; `kind` says whether the line
  is a turned row or a round; `turned` says whether the text turns the work at its end.
* `links` — where CrochetPARADE could state a link itself; today always `[]`, because the
  attachments it cannot write come from the extension lines instead (section 2).
* `positions` — CrochetPARADE's 3D layout, one vector per stitch. Written only when the
  exporter is run with `--layout`; the importer ignores it, and T5.3 reads it as the initial
  layout.
* `errors` — CrochetPARADE's own parse and layout messages `{"line", "msg"}`. When the array
  is non-empty the importer stops with the first entry, thrown as
  `CrochetPARADE line <line>: <msg>`; the rest of the export may be incomplete and is not
  trusted.

Before building the graph the importer checks the export and names the offending place on
failure: stitch and row ids must equal their array positions, `row` must name an existing
row, `targets` must be existing ids smaller than the stitch's own id, `prev` must be `-1` or
an existing id, every `loop` must be one of the six names, every row `kind` must be `row` or
`round`, and every link (JSON or extension) needs two existing, different stitches, each used
by at most one link.

Byte-stability, which is what makes the committed exports testable with `cmp`: `#` comment
lines never become stitches and never shift row numbers, and the extension lines are appended
after the last pattern line, so adding or editing them does not move any existing `line` or
`index` value. The import also refuses to guess: invalid UTF-8, malformed JSON and schema
violations all throw `std::runtime_error` with the byte offset, stitch or value involved.

## 2. The `#!` extension lines

Every line whose first two characters are `#!` is an extension line. CrochetPARADE treats `#`
as the start of a comment, so the line is invisible to its parser (and to the exporter's
stitch, row and line numbering). The importer reads two keywords:

```
#! link [row,pos] [row,pos] <kind>        <kind> = both_layers | join
#! count [row] N
```

* `[row,pos]` addresses a stitch by its exported row id and its index within that row —
  exactly the two numbers the JSON writes. An address is a single token: the numbers are
  decimal and are not separated by spaces around the comma (`[51,0]`, not `[51, 0]`).
* Whitespace between tokens is free, including directly after `#!`.
* Both addresses must name existing stitches, and a stitch may be named by at most one link
  overall (including the JSON `links`). Links are appended to the graph in written order;
  nothing is sorted.
* When the extension lines of a file are read, the importer also requires every
  `(row, index)` address to be unique, so an address always names one stitch.
* An extension-line error throws `std::runtime_error` naming the physical line number and
  the offending keyword, token or address, for example
  `crochet extension line 3: address [9,0] does not name a stitch`.

`#! count [row] N` asserts a row's stitch count:

* `[row]` addresses a row by its exported row id — one number in brackets, **not** the
  `[row,pos]` stitch address. `N` is the expected number of stitch nodes in that row:
  exactly the stitches whose `row` is that row, turning chains and join sl sts included.
* A mismatch throws
  `crochet extension line <line>: row [<r>] has <A> stitches, the pattern says <N>`,
  for example `crochet extension line 2: row [1] has 2 stitches, the pattern says 3`. A
  nonexistent row, the wrong number of tokens (`count [row] N` is exactly three), an
  address that is not `[row]` and a non-integer `N` throw before the comparison, in the
  same style as `link`.
* Every fixture carries one generated line per row (section 6), so importing a fixture
  checks every row's count.

Meaning of a link (the plan's point seam, section 6.7):

* `both_layers` — `a` is the seam stitch (for the pouch, a border `sc`) that CrochetPARADE
  attached to one layer only; `a.targets` holds that export-chosen stitch, and `b` is the
  partner stitch in the other layer that the same seam stitch also grabs. The importer keeps
  the link as `Link{a, b, "both_layers"}`; `pointSeams` resolves it for the mesh (section 4).
* `join` — `a` is the closing `sl st` of a strap end; its own export target is the rim
  stitch it was worked into, and `b` is the strap-end stitch it also sews to, closing the
  loop. The importer keeps `Link{a, b, "join"}` with `a` still carrying the rim target;
  `pointSeams` resolves it the same way.

`importPatternFile(jsonPath)` without a pattern path imports the export alone (the JSON
`links` array then applies, extensions are not read) — handy for tools that treat the
extension text as optional.

## 3. Facing (RS/WS) and RS-relative insertion

In flat rows the work is turned at every row end, so every other row is worked with the
wrong side (WS) facing the worker, and "back loop" in a pattern always means the loop away
from the person crocheting. `importPattern` therefore resolves the facing for every stitch
at the end of every import (`fl::resolveFacing`, plan section 5.5):

* `rs(row 0) = true`.
* Row 1 is RS by default. When row 0 is a chains-only foundation — it has stitch nodes and
  every one of them is `ch` — its `turned` flag is part of working back along the chain and
  does **not** flip row 1. A worked row 0 does flip row 1 like any other row.
* For every row r >= 2: `rs(r) = rows[r-1].turned ? !rs(r-1) : rs(r-1)`.

`Stitch::rs` stores the result (true = right side faces the worker). `Stitch::loopRs` is
the RS-relative insertion: on a WS row `blo` and `flo` swap, and every other as-written
value (`both`, `fpost`, `bpost`, `space`) copies through unchanged. All mechanics use
`loopRs`, never the as-written `loop` (plan section 5.5: positive `restAngle` folds toward
the WS, and in the T5.x mesh the triangle normals point to the RS).

Consequences for the fixtures: the pouch's 50 turned body rows alternate from row 1 RS to
row 50 WS; the border round (51) does not turn, so the strap restarts RS at row 52 and
alternates to row 131 WS; the join round (132) is RS again. The bag's wall rounds 1..44
never turn, so they are all RS, and its strap alternates from row 45 RS to row 144 WS
(join round 145 RS). The circles and the tube are worked in joined rounds without turning,
so every round is RS.

CrochetPARADE has no `rs row N` header line and none of the fixtures uses one, so the
plan's override for a non-default first RS row is not modelled: row 1 is always RS.

## 4. Point seams resolved for the mesh

`fl::pointSeams(g)` (`crochet_core/src/graph.cpp`) resolves every `Link` into the point
seam T5.2 adds to the mesh (plan section 6.7):

```cpp
struct PointSeam { int stitch = -1; int partner = -1; std::string kind; };
```

* `stitch` is the link's `a`: the seam stitch whose width `w_s` becomes the seam share —
  the border `sc` for `both_layers`, the closing `sl st` for `join`.
* `partner` is the link's `b`, the stitch's second target.
* `kind` stays the string `both_layers` or `join`, and the seams come out in the links'
  written order.

T5.2 materialises each one as `addPointSeam(stitch, partner, share = w_s)`, reading the
seam stitch's width `w` in metres. Validation (throws `std::runtime_error` naming the
stitch ids): the seam stitch must have at least one target in the export — its primary
attachment, which the export always writes — and the partner must not already be among its
targets, because the link is exactly the missing second attachment.

Resolved counts: the pouch has 55 seams (50 `both_layers` + 5 `join`), each bag has its 6
strap-end `join`s, and the circles and the tube have none.

## 5. Worked examples: the pouch (`patterns/cp/pouch.txt`)

The pouch is 16 `ch`, 50 turned body rows of 15 `sc`, one border round (`row 51`: 80 `sc`
around the folded body), a strap worked off the front rim, and a 5-`sl st` join round
(`row 132`). The import of the committed fixture has 1380 stitches, 133 rows and 55 links:
50 `both_layers` seams (25 per side) and 5 `join`s.

Side seam 1, border positions `0..24` (full block in the file, one line per seam stitch):

```
#! link [51,0]  [26,15] both_layers
#! link [51,1]  [27,1]  both_layers
#! link [51,2]  [28,15] both_layers
...
#! link [51,24] [50,15] both_layers
```

The fold puts the body's two side edges behind the border; column `i` of the border pairs
with the edge stitch of body row `26 + i`, which sits at index `15` when the row's edge
stitch is the last `sc` and at index `1` when it is the first (the alternating even/odd edge,
see the fixture's header comment). The pairing follows physical columns across the fold, not
source order: side seam 2 walks back from `[51,40]` to rows `50..26`,

```
#! link [51,40] [50,1]  both_layers
#! link [51,41] [49,15] both_layers
...
#! link [51,64] [26,1]  both_layers
```

so border positions `25..39` (the front rim) and `65..80` (the back rim) stay plain.

The strap end, joined to the back rim and to the strap's last row:

```
#! link [132,0] [131,1] join
#! link [132,1] [131,2] join
#! link [132,2] [131,3] join
#! link [132,3] [131,4] join
#! link [132,4] [131,5] join
```

Each closing `sl st` of `row 132` already has its rim target from the export; the link adds
the strap stitch it also sews to, one column each. The five round bags
(`patterns/cp/bag_round_*.txt`) use the same `join` shape, six links each: `[145,i]` to
`[144,6-i]` (`[146,i]` to `[145,6-i]` for the surface variant). The three circles and the
tube need no links.

The facing swatch (`patterns/cp/facing.txt`) is the section 3 case study: a 6-`ch`
foundation and four turned rows of 5 stitches (`scbl`, `scbl`, `scfl`, `scfl`), so a single
import exercises all four RS/WS and blo/flo combinations (10 of its 20 stitch bodies swap
their loop).

## 6. Regenerating the committed exports

The eleven exports under `patterns/cp/exported/` are committed so `ctest` is hermetic — no
node, no CrochetPARADE checkout at test time. Regenerate one with:

```
CP_DIR=/Users/swozny/projects/failoring/third_party/crochetparade \
  node tools/crochet/cp_export.mjs --out patterns/cp/exported/<name>.json patterns/cp/<name>.txt
```

for `<name>` in `pouch`, `bag_round_plain`, `bag_round_blo`, `bag_round_flo`,
`bag_round_slst`, `bag_round_surface`, `circle_plus5`, `circle_plus6`, `circle_plus8`,
`tube40`, `facing`. The committed files are pinned without `--layout` (empty `positions`).
After any edit to a pattern file, regenerate and compare byte-for-byte with the committed
file; the appended extension lines alone (`#! link`, `#! count` and comment lines) must
never change the output (see section 1).

## 7. What is not modelled yet

* **Point seams in the mesh** — T4.2 resolves the `Link`s into `PointSeam`s (section 4);
  T5.2 turns them into the actual `addPointSeam` constraints.
* **`rs row N` header overrides** — the plan's section 5.5 override for a non-default first
  RS row: CrochetPARADE has no such line, and no fixture needs it (section 3).
* **Piece names, fold hints, surface placement, layouts** — T5.x. Every stitch is `main`
  today; the fold and surface work are T5.3, which also picks up CrochetPARADE's `positions`
  as the initial layout.
* **Charts, colour/symbol tables, per-stitch overrides** — later tickets; the graph gains
  them as they are needed.
