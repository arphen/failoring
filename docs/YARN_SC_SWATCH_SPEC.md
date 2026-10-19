# Single crochet swatch, 5 stitches wide: a motion spec (from the user, 2026-10-19)

Pasted by the user as the reference for how the hook and the work move in yarn mode ("this should look good, work and be easy"). US terms; right-handed crocheter, hook in the
right hand, working yarn tensioned over the left index finger. Frame: +x to the crocheter's right (rows are worked right to left), +y up (rows stack), +z toward the crocheter
(the front face of the fabric). Between stitches there is exactly one loop on the hook.

Primitive motions: YO (yarn over the shaft from back to front, then the throat turns down); INSERT(target) (tip through the opening along -z, throat up, under the strands);
PULL_UP (draw the hook back along +z with the yarn caught, lift the loop about 1.5 hook diameters); PULL_THROUGH(n) (draw the caught yarn through n loops); CH = YO + PULL_THROUGH(1).

Flow: slip knot; chain 6 (5 stitches plus the turning chain; the loop on the hook is chain 0, never counted); sc into the 2nd chain from the hook and across (no turn after the
foundation chain); sc = INSERT, YO, PULL_UP, YO, PULL_THROUGH(2). The turn: CH 1 (lifts the loop one stitch height), then rotate the work 180 deg about the vertical axis through
the hook, the hook and loop staying in place, the same direction every row (the right edge comes toward you); before the turn the fabric lies to the right of the hook, after it to
the left. Row 2 on: sc under both loops of each V-top of the previous row, the first one right next to the turning chain, the last one being the previous row's first stitch (do not
work into the turning chain). Finish: cut the yarn, 15 cm tail, pull it through the working loop.

Error checks: one loop on the hook after every stitch; every row has exactly 5 V-tops (4: first or last stitch skipped; 6: worked into the turning chain); straight vertical
edges; every pull-up the same height.

What yarn mode does about it (see yarn_view_check.gd): row 1 starts by itself with the first single crochet (no Turn to press, no rotation), the turn after a row makes the turning
chain with the hook and then swings the work half a turn round the loop on the hook, and the run to the ball keeps its ball.
Not done yet from this spec: the yarn over laid across the shaft with the throat turning down, the INSERT motion under the strands, the pull-up lift, and finishing off (cutting the yarn).
