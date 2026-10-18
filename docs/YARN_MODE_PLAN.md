# Yarn mode: a stitch-by-stitch crochet simulator (draft, for review)

Status: draft written 2026-10-18 after the first hands-on session. Nothing here is built. It adds a second engine beside the cloth engine of
`docs/CROCHET_PLAN.md`; it does not replace it. Rules of that plan still apply (never merge `crochet/main` into `main` without being told; ask at the
gates marked **GATE** below).

## 1. What the user asked for

* Crochet virtually, one action at a time, starting from a slip knot: a hook, an action menu (chain, sc, hdc, dc, turn, sl st, change colour ...).
* Choose freely **which loop** each stitch goes into and from where, not only the loops a pattern line implies: any loop, either strand, the space
  between stitches, round a post.
* Yarn over versus yarn under, visible. Close-ups from any angle.
* Close to the real thing: the **V**s of the stitches visible, the **yarn twist (torsion) modelled**.
* Compare ways of changing colour at a seam between two panels (e.g. pick up two orange loops before pulling red through), side by side.
* Foundation chains, foundation sc and similar.
* An **expand / shrink mode**: loosen the work (more yarn than a real, pulled-tight piece could ever have) to see how the loops hook into each other, then
  tighten it again.
* Physics approximate: the plausible shape matters, not the exact mechanics; it must stay light enough to be interactive.

## 2. Why a second engine

The cloth engine models each stitch as a patch of triangles with measured stiffness: right for how a bag hangs and holds a laptop (6000+ vertices,
one stitch is one vertex). It has no strand, no loop to point at, no colour per stitch, no over/under. Yarn mode models the strand. The two share the
pattern parser and the stitch graph, and the Compare view and chart; they share no solver.

## 3. The model

* **One continuous yarn** as a chain of nodes about 2 mm apart (a cotton worsted strand is about 2.5 mm across; the working loops are 3-4 mm in radius),
  with a radius, a position per node and an **orientation frame per segment** (a Cosserat rod). The frames are what carry the twist.
* **Solver:** position- and orientation-based Cosserat rods (Kugelstadt and Schomer 2016) on XPBD (Macklin et al. 2016): stretch, shear, bend and twist
  constraints along the rod; strand against strand capsule contact with friction (loops stay hooked because of contact and friction, not because anyone
  glues them). Sub-stepped, a spatial hash for contact. Stable and cheap by construction, approximate by design.
* **Torsion:** the rod has a rest twist (the yarn's ply twist) and a twist stiffness. Forming a loop twists or untwists the yarn, which is what makes
  real stitches lean (the slant of sc) and the work bias. The ply direction (S or Z) is a yarn parameter.
* **Yarn length:** the working end grows as stitches are made (a loop pulled up takes yarn from the ball). A stitch's yarn per loop comes from the
  gauge (the cloth engine already has yarn length per stitch; the two must agree, see section 8).
* **Scale:** about 15-20 nodes per stitch. A 10 x 15 x 2 swatch is about 6000 nodes, interactive on one CPU thread. The 5800 stitch laptop bag would be
  about 100 000 nodes: not for the editor, possible later (this is where a GPU would pay off, unlike the Newton and Cholesky solve of the cloth engine).

## 4. The editor: hook actions, not stitch types

The primitive is what the hook does; a stitch is a macro of primitives.

| primitive | what it does |
|---|---|
| `slipknot` | starts the yarn: one loop on the hook |
| `insert(opening, from)` | the hook goes through an **opening** (the eye of any finished loop, the gap between two stitches, a post), from the front or the back |
| `yo(over \| under)` | wraps the yarn round the hook (over: the usual direction) |
| `pull(k)` | pulls the yarn through k loops on the hook |
| `chain`, `turn` | as named; turn flips the working direction |
| `colour(c)` | the next strand is colour c: a new tail joined at the hook |

`sc` = insert, yo, pull(1), yo, pull(2); `hdc`, `dc`, `tr`, `sl st`, foundation sc and the post stitches are macros the same way. The **action list is the
pattern**: the editor writes CrochetPARADE text and the parser reads it into actions, so what is clicked can be pasted into the bag engine and back.

* **Picking:** every finished loop is pickable (a ring of nodes), the way chart and cloth picking already work; a picked opening is highlighted and
  the next `insert` goes there. Free choice means exactly this: the menu offers any opening, and the macros take the opening as a parameter.
* **The hook** is drawn (shaft, throat, handle) and animates between actions. The motion is cosmetic; the topology is constructive: an action appends the
  new yarn along a path through the chosen opening and the solver relaxes it. (Simulating the hook pushing yarn through physically would be a
  project of its own and is not needed to see the result.) Undo pops the last action.

## 5. Looking at it

* **Rendering:** each strand as a tube with its plies drawn as helices that follow the rod's twist angle, a fibre halo as in the cloth view's relief
  shader, colour per segment. The V of a stitch is not painted: it comes from the strand geometry and the lean.
* **Camera:** the new orbit camera (`crochet/crochet_orbit.gd`) with its minimum distance made relative to the subject, so millimetres can be inspected.
* **Expand / shrink:** two sliders on the solver, not a render trick.
  * *Slack*: the rest length of every segment is scaled (1 = as made, 1.5 = a third more yarn in every loop). Contact keeps the loops linked, so the
    work opens up with the loops still hooked into each other; sliding back tightens it again. Animated, it is a quasi-static continuation.
  * *Yarn thickness*: the render and contact radius is scaled down (a thin-yarn view), to see through the interlocking.
  Both are also the debugging tools of the solver.

## 6. Colour changes at a seam

Colour is per yarn segment; a colour change is the `colour(c)` primitive at a chosen point of a stitch (before the last pull-through, after it, picking up
one or two loops of the old colour first, carrying the unused yarn up the edge or not). Each way is a macro, so each is a pattern text, and the
variants run side by side in the Compare view like the five bag variants. Whether a simulated seam looks like the real seam of a given technique is for
the user to judge from the real piece; the simulation cannot decide it.

## 7. Phases

Each phase ends in tests and, where marked, a look by the user.

| phase | content | exit |
|---|---|---|
| Y1 | rod core in C++: nodes, frames, stretch / bend / twist, capsule contact with friction, tests | a chain stays inside its length; two linked rings stay linked when pulled; a twisted rod untwists to its rest twist; no strand pair closer than 2r - eps after relaxation |
| Y2 | slip knot, chain, sc as primitives and macros on a flat swatch; the plied-tube renderer; slack and thickness sliders | an sc swatch relaxes; **linking numbers of the loops are the same at slack 1.0, 1.5 and back**; **GATE: the user looks at the sc swatch, close up, from front and back** (do the Vs look right?) |
| Y3 | the editor: hook, action menu, opening picking, undo, text round trip | a swatch built by clicking equals the one parsed from its text (same loops, same yarn length) |
| Y4 | hdc, dc, tr, sl st, foundation chain and foundation sc, post stitches, BLO / FLO / between-stitch openings | each stitch type against the stitch graph's loop insertions |
| Y5 | colours: `colour` primitive, the seam variants, Compare for them | **GATE: the user's list of seam variants, and a look at each** |
| Y6 | performance (threads, then GPU if a larger piece needs it) | measured, not assumed |

## 8. How it is checked

* **Invariants a machine can check:** yarn length per stitch against the cloth engine's yarn model (the same pattern, the same gauge, within a few
  per cent: a swatch that disagrees is wrong in one of the two engines); minimum strand distance; the **linking number between loops is unchanged by
  expanding and shrinking** (a mutant with contact switched off must change it); the action list and the pattern text round-trip.
* **What needs eyes:** whether the Vs, the lean and the seams look like real crochet. I will not call a stitch template right on the strength of its
  invariants; the gates above exist for that.

## 9. Risks

1. The loop topology of each stitch (which way the yarn goes, which opening it takes, how it twists) is the hard part and the most likely to be subtly wrong.
   Mitigation: one stitch (sc) first and the user's eye on it before the rest.
2. Friction decides whether loops hold at the right tightness; the result will be plausible, not exact.
3. Tunnelling: a strand can pass through another at a large step. Mitigation: a non-intersecting start, sub-stepping, a cap on the move per step, and the
   minimum-distance check above.
4. Scale: swatches only, until Y6.

## 10. Open questions

* The first three or four seam variants to compare (a line each is enough, e.g. "pick up two orange loops, then pull red through").
* Whether the hook's motion should ever be physical (the plan says cosmetic).
* Yarn defaults for twist: the plies' direction and how tight they are, for cotton worsted and acrylic worsted (starter values until a real strand is measured).
