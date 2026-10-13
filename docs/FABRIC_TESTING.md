# Fitting a fabric preset from a real swatch (protocol)

**Status: written, not tested on real cloth.** Nothing in this document has been run against a physical
swatch. The virtual side (`fabric_bench`, `tools/fabric_bench/`) is what has been checked, and only against
theory and against the simulator's own numbers. Treat every step below as a plan to be tried, not as a
validated procedure. No claim of real-world accuracy is made.

## Purpose

`materials/fabrics.json` holds starter presets whose values are estimates (see the file header and
`docs/ARCHITECTURE.md`, "Known limitations"). This protocol describes how to replace one preset with values
measured on a real swatch, and how to check the result on a test that was not used for fitting.

The three tests are:

| # | Test | Used for | Output |
|---|---|---|---|
| 1 | Cantilever (strip pushed over a table edge) at 0, 45 and 90 degrees to the warp | fitting bending | B warp, B weft, B at 45 deg |
| 2 | Hanging strip with known weights | fitting stretch | k warp, k weft (N/m per unit strain) |
| 3 | 30 cm disc shadow (Cusick drape) plus fold count | **held-out check only** | DC %, folds |

Test 3 is never used to set a parameter. If the simulated drape coefficient does not match the measured one,
the preset is wrong or the model is, and the fit is not accepted just because tests 1 and 2 looked right.

## Known limits of the method (read before you start)

* Drape silhouettes alone identify stretch poorly. Two fabrics with different tensile stiffness can give the
  same drape coefficient. This is why stretch is fitted from test 2, not from test 3.
* Drape coefficient is insensitive to anisotropy (warp vs weft bending) and to stretch.
* Fold count is noisy. Use three specimens and report the spread, not one number.
* Bending anisotropy stronger than about 1:5 (warp:weft) is under-represented by the solver's hinge model
  (`docs/ARCHITECTURE.md`). A large warp:weft ratio in test 1 is a warning, not a fit target. See the bias
  strip check (`docs/handoff/T1_PROGRESS.md`).
* Real fabrics have no ground-truth parameters to recover (Ru et al. 2025, cited in the T1 notes), so the
  only valid check is held-out behaviour (test 3 and any extra test you add).
* A 2025 study reported that phone-photographed drape coefficients were statistically indistinguishable
  from the Cusick method, best at about 22 cm camera height and about 980 lux. That study still used a
  commercial Cusick meter, so a fully home-built rig is untested. (Reported figures, not checked here.)
* Moisture, ironing, washing and ageing all change a strip. Record the condition of each specimen.

## Materials

* Swatch: at least 60 cm x 60 cm, free of fraying edges (overlock or tape the cut edges, but do not stiffen them).
* Scale with a resolution of at least 0.1 g, for the areal density.
* Ruler or calipers for thickness and dimensions.
* Hanging rig: a rigid horizontal bar, a clamp that holds the top edge along its full width, and hooks
  for known masses (a set of at least five masses, for example 10, 20, 50, 100 and 200 g; record the
  real masses on a scale, not the labels).
* Cantilever rig: a table with a flat edge, a ruler on the table, a camera on a tripod, and a flat
  background that contrasts with the cloth.
* Disc rig (test 3): a cylindrical pedestal 18 cm in diameter, a 30 cm disc of the swatch, a light source
  roughly overhead, and a camera square to the table. Print a 30 cm reference circle to scale.

## Preparation

1. Condition every specimen for at least 24 hours in the standard atmosphere for testing textiles
   (ISO 139; to be confirmed against the standard text before use, it is not checked here).
2. Measure the areal density `rho` (kg/m^2) on a 100 mm x 100 mm square: `rho = mass / area`.
   Use this value in the simulator, not a value from a table.
3. Measure the thickness with a light pressure foot.
4. Mark the warp (length-wise, along the selvedge or wales) and weft directions on each specimen.
   Knitted fabrics have wales and courses; mark them the same way.

## Test 1: cantilever bending (three directions)

1. Cut three strips 25 mm wide and at least 200 mm long: one with its length along the warp, one along
   the weft, one at 45 degrees to the warp. Use three specimens per direction if possible.
2. Place a strip on the table so that it overhangs the edge, with the strip's length at the chosen angle.
   Slide it out until the free end starts to bend under its own weight.
3. Record the overhang `l` (m) at the moment the free end touches an inclined plane at the cantilever
   angle (41.5 degrees, the angle the bench uses).
4. Compute the bending rigidity per unit width from the elastica relation used in `fabric_bench`:

   `B = rho * g * l^3 / lambda(theta)`, where `lambda(theta)` is the nondimensional load of an
   inextensible elastica cantilever whose chord angle is `theta`. The bench computes `lambda` numerically
   (`elasticaLambdaForChord` in `tools/fabric_bench`). Use that function, not a rounded constant.

   Do **not** use the Peirce-style `c = l (cos t / (8 tan t))^(1/3)` formula. The exact elastica relation
   at 41.5 degrees gives `c = l/2` to about 0.1 percent (see the progress log), and the Peirce-style
   expression differs from it by about 15 percent in `B`.
5. Report the mean and the spread (standard deviation) over the specimens, for each direction.
6. Warp and weft values go into `bending_warp_Nm` and `bending_weft_Nm`. The 45-degree value is a check:
   the model predicts it from the two principal values (see the bias-angle check in the progress log).

## Test 2: hanging strip with weights (stretch)

1. Cut strips 50 mm wide and 300 mm long, warp and weft. Use three specimens per direction.
2. Hang each strip from the top edge, clamped along its full width. Mark two points 200 mm apart near the top
   (a gauge length that is not affected by the clamp).
3. Hang a series of masses (at least five). Choose them so the strain stays within about 0.5 to 3 percent: the
   tension per width is `T = m g / W`, so a 50 mm strip carrying 100 N/m needs about 0.5 kg. Start light and
   increase. For each load, wait until the marks stop moving, then record the gauge length.
4. Tension per unit width is `T = m g / W` (N/m), where `W` is the strip width. Strain is the gauge elongation over
   the gauge length.
5. The stretch modulus `k` (N/m) is the slope of tension against strain over the linear part, from a
   least-squares fit. The bench reports `k` the same way (`TensileDir` in `tools/fabric_bench`).
6. Warp and weft values go into `tensile_warp_N_m` and `tensile_weft_N_m`.
7. Shear is not measured by this protocol. Bias-extension shear is possible but the bench's naive formula
   (`G` from the bias-extension modulus) ignores clamp zones. Leave `shear_N_m_rad` as it is and say so.

## Test 3: drape coefficient and fold count (held out)

**Do not use these numbers to set any parameter.** They are the check.

1. Cut a disc 30 cm in diameter from the swatch. Use a template, not freehand.
2. Lay the disc over the 18 cm pedestal, centred, and let it settle for at least 30 seconds.
3. Photograph from directly above, with the light roughly overhead. Include the scale circle in the frame.
4. Measure the shadow: `DC = 100 * (A_shadow - A_pedestal) / (A_disc - A_pedestal)`, where areas are in
   the image plane and the pedestal and disc areas come from the scale circle.
5. Count folds: the number of local maxima of the outline radius around the circle that stand out by at least
   3 mm (the same threshold the bench uses) or 15 percent of the radius range, whichever is larger. Count by
   eye too and report both; they disagree on noisy specimens.
6. Repeat for three discs. Report the mean and the range.

The simulator runs the same rig (`fabric_bench`, `simulateDrape`) for the preset. Compare the simulated
DC and folds with the measured ones. Report the difference; do not adjust the preset to close it.

## Fitting and acceptance

1. Enter the measured density, thickness, bending values and stretch values into a copy of the preset
   (not into the shared `materials/fabrics.json`). Give the copy a new id and a note that it is a fit.
2. Run `fabric_bench --fabric <id>` on the copy and read the bending, tensile and drape tables.
3. Compare the bench's derived bending (`B derived / B input`) and stretch (`k derived / k input`) with the inputs.
   Ratios far from 1 mean the mesh or the test geometry is off, not the fabric. Check the mesh-level spread first.
4. Compare the simulated drape coefficient and folds with the held-out measurements from test 3.
5. **Acceptance thresholds are not set in this document.** Choose them before the first real swatch is
   measured, write them into the lab notes with the date, and do not change them afterwards. Report the
   measured difference whether or not it passes.
6. Only after a preset passes its acceptance check may its entry in `materials/fabrics.json` be marked
   `validated`. This is a planned field (see the T1 idea entry); it does not exist yet.

## What to report back

For each fabric: the measurements (with specimen counts and spread), the fitted preset values, the bench
output, the held-out comparison, and the acceptance decision. Note the conditioning, the camera and light
setup for test 3, and any specimen that was damaged or looked different from the others.
