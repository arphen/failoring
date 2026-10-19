# Yarn mode: a stitch-by-stitch crochet simulator (draft, for review)

Status: draft written 2026-10-18 after the first hands-on session; the user said go on 2026-10-19. **Y1 is built** (`docs/crochet/reports/Y1.md`);
**Y2 and a first Y3 are built, waiting for the user's look** (`docs/crochet/reports/Y2.md`); Y4 onward is not. The open questions of section 10 are still open: Y2 does not need them, Y5 does. It adds a second engine beside the cloth engine of
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

## 11. The editor's feel: the user's brief (2026-10-19, said while away from the desk)

Phase Y3 builds to this; it is the point of the whole mode.

* **A lego-simulator feeling.** Click an element, the hook performs the action, the new stitch appears, go again. Clicking "single crochet" must not
  raise further questions: the defaults are what a crocheter means. **Both loops of the target stitch are selected by default**; the user may
  select only one of the loops, or skip a loop by selecting and deselecting it (the loop picking of section 4, but with the sensible choice already made).
* **Turning the work** at the end of a row gets a **designed animation**: the camera rotates round the piece, because the worker turns the work over.
* **The worker's view.** The hook is in the right hand and the workpiece in the left; camera and hook placement follow that.
* **Soft yarn.** The yarn should feel soft and deformable. It is about feel, not physical accuracy; the piece hardly reacts to gravity while it is
  being worked, so gravity is off (or nearly) in this mode.
* **The pattern as an overlay** while crocheting: the pattern's chart or cloth view, semi-transparent, over the work. (The cloth engine already makes the
  layout and the chart; this is that view blended over the yarn.)

## 12. What the first Y2 attempts taught (2026-10-19)

* **CrochetPARADE** has no yarn model (see `docs/crochet/reports/Y1.md`), so its code is not in this.
* **Storck et al., "Topology based modelling of crochet structures", J. Industrial Textiles 2022** (CC BY-NC 4.0; its ideas are used, nothing of it is
  copied): a stitch is a unit cell of key points along the yarn centre line (L stitch length, H height, D depth), shifted in space and joined in the order
  the work is made, then spline interpolated. Its caveat is the one that matters for us: yarn diameter over stitch size must be about 1/10 or less for
  the paths not to interpenetrate; ours is about 1/2, so worsted must be treated as compressible in the contact (radius scale well under 1) and
  built tight, not built roomy and shrunk. Its Figure 3 shows the sc unit cell: two tall parallel posts, a long top loop reaching toward the next stitch,
  and a small foot loop at the bottom.
* **CT2Yarn (arXiv 2609.06950)** renders plies as coaxial helices round the centre line (Zhao et al. 2016) and simulates the recovered yarn as a Discrete
  Elastic Rod: the same two choices as `yarn_render` and the rod solver here.
* **Building the topology by physics did not work.** Fingers made roomy round a hook and then tightened by tension and by shrinking rest lengths gave
  blobs: the slack has nowhere tidy to go, and a long travel strand from the end of the yarn to the next opening wraps the work. The rod solver was not
  the problem once its bend Jacobian was made exact (a regression test now covers it).
* **What worked:** put every loop on an explicit lattice position, run the legs through the eye of the loop pulled through, build it tight, and check it. A
  chain built that way, with no physics, is a recognisable crochet chain. A real photograph of an sc swatch showed that a stitch is a vertical post of two strands
  topped by a flat slanted bar (not a round ring facing the viewer); `yarn_tune` searches the lattice parameters for clean threading and no overlap (strands may
  squeeze to half a diameter). The result, with plies, is in `docs/crochet/reports/Y2.md`: the Y2 gate, the user to look at it.

## 13. Where it stands (2026-10-19)

| phase | state |
|---|---|
| Y1 rod core | done (`docs/crochet/reports/Y1.md`) |
| Y2 chain and sc, plied tubes, sliders | built; **the gate (the user looks at the sc swatch) is open** (`docs/crochet/reports/Y2.md`) |
| Y3 editor | first cut built to the brief of section 11: one click per stitch, hook, animated turn, loop picking, overlay, pattern text both ways, undo, soft pull |
| Y4 more stitches | hdc, dc, sl st, front / back loop, skip: built, not compared with photographs; foundation sc, post stitches, between-stitch openings, increases and decreases: not yet |
| Y5 colours | built: a new yarn or a dropped one picked up, at the last pull-through or from the start of the stitch, extra loops in the last pull-through; **the Compare view for seam variants is not built, and the gate (the user's list of seam variants, and a look at each) is open** |
| Y6 performance | not started: the soft physics costs about 50 ms a step at 4000 particles; the hook picks only the neighbourhood of a grip (freezeOutside) |

The seam variants that can be made today: (1) colour at the last pull-through, (2) colour from the start of the stitch, (3) the last pull-through also takes loops of the other colour
(Shift + click), (4) a seam stitch worked into one loop only. Each is a click or two; the user's own list of what to compare, and which of the real techniques these stand for, is the
open question of section 10.

### 13.x The hook makes the stitch (2026-10-19)

The editor no longer just grows the yarn: for every loop a stitch makes (a chain's loop, an sc's post and its top, a dc's yarn over, post and top) the hook goes in
behind the eye the loop works through, with the yarn over it, and draws the loop out through the eye toward the worker; the loop opens as it comes
(`YarnWork::pullFrame(loop, s)`, `YarnSession.anim_frame`, `yarn_view.gd::_pull_animation`). It is kinematic and driven by the design, so it always ends exactly at the
finished loop (tested: 0 mm off). A yarn over closes into its ring round the hook. Also: yarn ends are rounded (dome caps) and there are yarn balls with the run from the
work to the ball; any loop can be clicked (pick by yarn, with hover highlight).

The bounded physical pre-simulation (`yarn_presim`) was tried: a hook as a moving capsule, a slip loop, the working yarn under tension, a yarn over laid by a guide. The yarn over and
the draw-back run, but pulling the bight through a loop that the work holds makes the solver blow up. It stays as an experiment; the coefficients of the animation
(how far behind the eye the hook starts, how the folded loop opens) were set by eye against the renders.

Still to do for the hook: show the yarn running over the shaft before the pull (the working yarn from the ball laid over the hook), the old loops closing as the new one
comes through, and chains drawn with V-shaped loops.

### 13.y V-shaped chains (2026-10-19)

A chain loop was a round ring; a real chain shows a column of Vs (each loop closes to a V round the next one's legs). Chain loops are now teardrops: elongated along the chain
(`chainA` 6 mm, `chainB` 3.4 mm, pitch = the stitch width 8.46 mm), narrowing toward the tip (`chainTaper` 0.5), and the next loop is threaded through them near the tip
(`chainEye` 0.55 of `chainA`, `YarnLoop::eyeShift`), alternating two layers 1.5 mm apart. Chosen by a grid search over taper, eye, width and layer using the threading
report (the same 32 of 33 clean passes as the round chain). Shots: `docs/crochet-shots/yarn/chain_teardrop_front.png`, `swatch_teardrop_chain_front.png`, `editor_swatch_teardrop.png`.
Not yet right: the yarn is 2.5 mm thick against an 8.5 mm stitch, so the chain still reads as a braid of pointed loops more than the photo's thin Vs.

### 13.z The turn, the hook's motions, fastening off (2026-10-19)

From the user's sc swatch motion spec (docs/YARN_SC_SWATCH_SPEC.md): no rotation after the foundation chain (the first sc starts row 1 by itself); a turn after a finished row is the turning chain
(drawn by the hook) and then half a turn of the fabric about its vertical axis through the loop on the hook, which stays put, the same way every row. Each loop of a stitch is made by insert / yarn over /
pull (`YarnWork::pullFrame(loop, s, wrap)`: the working yarn to the first eye is part of the frame so it can be laid over the shaft). Fastening off is `YarnWork::finishOff()` and the "Finish off" button.
Tests: test_crochet_yarn_work (pull frames end exactly at the design, the wrap lies round the shaft, the tail goes through the loop once), yarn_view_check (70 PASS: the spec's flow, the turn, the animated
stitch and turn, finishing off and undoing it).
