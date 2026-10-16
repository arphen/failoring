# Crochet mode: implementation plan

Written 2026-10-16 for an agent who picks this up without the conversation it came from. Read all of it before you
start. Every phase ends with a check that has numbers. **Do not start a phase until the previous phase's check passes.**

> **How this plan is executed:** by one orchestrator agent that never writes feature code itself. It hands tickets to
> implementer subagents, waits for their commits, reviews them, and loops until the board is empty. The protocol, ticket
> list and prompts are in **§14**. If you are the orchestrator, read §14 first, then the rest.

## 0. What we are building, in one paragraph

The user writes a crochet pattern (US terms) as stitch-by-stitch instructions. Failoring compiles it into a **stitch
graph**: one node per stitch, with an edge to the stitch made just before it (along the row) and edges to the stitch or
stitches it was worked into (down the column). The graph becomes a triangle mesh in which **every triangle has its own
rest shape**, taken from the stitch sizes. The mesh is not made from a flat pattern piece. The existing implicit cloth
solver (`cloth_core`) then hangs the result: empty, or with a laptop inside, from a peg or from a hand. Increases and
decreases change the rest shapes, so cups, tubes and flat circles come out of the stitch counts the way they do in real
crochet. Yarn, hook size and stitch type set each triangle's size, mass and stiffness. Back-loop-only, front-loop-only,
slip-stitch rounds and surface slip stitches change the bending of particular edges ("hinges"). Then the user can see
and measure the difference between, for example, a BLO hinge round and a plain round at the base of a bag.

It is a static scene: no walking and no animation. Settling to rest is the whole job, so accuracy matters more than
frame rate.

### Who the user is and what they want to compare

* They crochet bags. The first pattern is in `§11.1`: a flat sc rectangle, folded, side seams joined with sc through
  both layers, and a 5-stitch strap worked off the rim.
* They want to compare **construction choices at the bottom-to-wall transition** of a bag worked in the round: a plain
  round, a BLO round, an FLO round, a slip-stitch round ("a chain around the bottom", see `§11.3`) and a surface slip
  stitch. They also want to compare **yarns** (fibre and weight) and **hook sizes**.
* Fidelity target: two variants that a crocheter can tell apart in real life must look different, and must measure
  differently, in the app. Absolute accuracy comes later, from swatch calibration (`§10`).

### Things the user should hear early (put them in the UI too)

* The pattern in `§11.1` gives no yarn, hook or gauge, so its finished size is unknown until those are chosen: 15 sc
  wide by 25 rows deep (50 rows folded) can be anything from a small pouch (DK or worsted) to a large tote (jumbo or
  t-shirt yarn on a big hook). The app must compute the finished size from the gauge, show it next to the pattern, and
  in the loaded scenario report "fits" or "won't fit, X cm short" (`§7.3`, step 4). Never hard-code a size claim. The
  fixture in `§11.2` is sized for a 13" MacBook Air at cotton-worsted gauge.
* The starter numbers for stitch mechanics (`§4`) are order-of-magnitude values, **not measurements**. The relative
  effects (smaller hook means denser and stiffer, BLO means a softer hinge) are reliable. The absolute sag in mm is not
  reliable until the user calibrates with a swatch (`§10`). Label this in the UI and the docs, the way
  `materials/fabrics.json` does.

---

## 1. Ground rules for the implementer

1. **Branch:** `crochet/main` from `main`. Commit at the end of each phase, and more often if you like. Never rewrite
   history. Keep a running log in `docs/CROCHET_STATUS.md`: date, phase, what passed, the numbers, and what is still
   open. Use the style of `docs/STATUS.md`: say what was checked and what was not.
2. **Do not change existing behaviour.** Every new solver feature is opt-in. Before you touch `cloth_core`, run
   `cmake -S . -B build && cmake --build build -j && ctest --test-dir build` and save the output. After each core
   change, run it again: the same tests must pass. Also add the regression check in `§2.7`, which compares positions
   bit for bit.
3. **Do not edit `godot/editor.gd`** (5000 lines), except for the one entry button in `§8.1`. Crochet gets its own
   scene and scripts under `godot/crochet/`.
4. **Units:** the DSL and the UI use mm, grams and cm. `cloth_core` uses SI (m, kg, N). Convert at exactly one place:
   the crochet compiler's output.
5. **Honest numbers.** If you could not measure something, write "not measured". If you tune a constant, record the
   before and after values and the reason in `CROCHET_STATUS.md`.
6. **Convergence before conclusions** (lesson in `docs/ARCHITECTURE.md`): a run whose Newton solve hits the iteration
   cap is not a result. Turn on `SolverParams::contactDiagnostics` in the headless runs and report `iterCapHits`.
7. When you are stuck for more than about an hour on one thing, write down what you tried in `CROCHET_STATUS.md`,
   choose the simpler fallback this plan names, and move on. Most sections name one.

### Repo facts you need (checked 2026-10-16 on main @ 487d279)

* Solver: `cloth_core/include/failoring/cloth.hpp`, class `fl::ClothSolver`. Pieces are added with
  `addPanel(PanelMesh, FabricMaterial, PanelPlacement, grain, name)`. A `PanelMesh` is a flat 2D mesh: `verts` (Vec2),
  `tris`, and `edgeChains`. The rest shape of each triangle comes from the per-vertex `restUV_`
  (`cloth.cpp: finalize()`, around lines 166-322). The material is **per panel** (`mats_[panels_[...].material]`).
* Triangle energy: `TriC` holds the inverse rest matrix (a, b, c, d) and the compliances `alphaU`, `alphaV`, `alphaS`,
  `comp`. The implicit energy, gradient and Hessian are in `cloth_implicit.cpp: assembleEnergy()` (lines 133-204) and
  `variationalEnergy()` (lines 206-233). The static solve reuses `variationalEnergy`.
* Bending: hinges are built in `finalize()`. `detail::hingeAngle` (`cloth.cpp` line 13) returns an **unsigned** angle
  (0 = flat). The energy is `0.5/alpha * theta^2`. Directional rigidity is mixed from `bendWarp` and `bendWeft`.
* Seams: `SeamC {a, b0, b1, t}`, a point-to-segment spring with zero rest length; `seamRampFrames` fades them in.
* Material convention (`fabrics.json`): **warp = u = wales (up the column), weft = v = courses (along the row).**
  Crochet uses the same convention.
* Contact: the colliders are SDFs (`collider.hpp`). `UnionCollider` exists. A per-vertex normal force from the rigid
  contact hold is kept in `rigidForce_` (`cloth_implicit.cpp` lines 525/537), and the normals in `contactN_`. Both are
  private.
* `restUV_` is also read by `cloth_barrier.cpp:35` (barrier rest positions), by `cloth_implicit.cpp:774` (rest lengths
  for strain limiting), by the seam arc lengths in `finalize()`, and by `cloth_static.cpp:45` (`setRestUV`).
* The GDExtension `FailoringSim` (`gdextension/src/failoring_sim.{h,cpp}`) already has **box props**
  (`add_box_prop(transform, half_extents)`, `set_box_prop`, an `OrientedBoxCollider` with sharp corners) and passes
  prop motion to friction. `set_param(name, value)` maps names to `SolverParams`. The GDExtension CMake
  (`gdextension/CMakeLists.txt` lines 14-22) **lists the core sources by hand**, so new `.cpp` files must be added
  there as well as to the root `CMakeLists.txt`.
* Godot: 4.3. On the user's Mac it runs with `make run` (see `Makefile`). Rebuild the extension with
  `tools/build_gdext.sh`. Headless tests are `godot --headless --path godot res://tests/<name>.tscn`, which print
  `PASS`/`FAIL` lines (`godot/tests/api_smoke.gd` is the template). The editor's worker thread is at `editor.gd` line
  279 (`Thread.new()`). Lights and backdrop come from `studio.gd: Studio.build(parent)`. Camera helpers are in
  `cam_views.gd`.

---

## 2. Phase 1: solver extensions in `cloth_core` (all opt-in)

Goal: the solver can take a mesh whose triangles each have their own rest shape and material, whose hinges have their
own stiffness scale and signed rest angle, with vertex-to-vertex seams, and with strain-stiffening (J-curve) stretch.
It can also report contact forces. No crochet knowledge in this phase.

### 2.1 Intrinsic panels

Add to `mesher.hpp` (or a new `intrinsic_mesh.hpp`):

```cpp
struct HingeMod { int a, b; Real bendScale = 1; Real restAngle = 0; };   // a,b = local vertex ids of the shared edge

struct IntrinsicMesh {
    std::vector<Vec3> x0;                          // initial 3D positions (m), one per vertex
    std::vector<Vec2> layoutUV;                    // any sensible 2D parameterisation (display/texture, seam arc length)
    std::vector<std::array<int,3>> tris;           // consistently oriented (see 5.4)
    std::vector<std::array<Vec2,3>> triRest;       // rest corner positions per triangle, (u = wale, v = course), metres
    std::vector<int> triMaterial;                  // index into the materials vector passed to addIntrinsicPanel
    std::vector<HingeMod> hingeMods;               // optional; edges not listed get bendScale 1, restAngle 0
    std::vector<std::vector<int>> chains;          // optional named vertex lists for seams/pins (like edgeChains)
};
```

Add to `ClothSolver`:

```cpp
int addIntrinsicPanel(const IntrinsicMesh& m, const std::vector<FabricMaterial>& mats, const std::string& name = {});
```

Implementation steps:

1. New per-triangle storage: `std::vector<std::array<Vec2,3>> triRest_; std::vector<int> triMat_;
   std::vector<char> triIntrinsic_;`. In `addPanel`, push `triIntrinsic_ = 0`, `triMat_ = panel material`, and leave
   `triRest_` empty for now. In `addIntrinsicPanel`, push the materials into `mats_`, store `triMat_` offset into
   `mats_`, store `triRest_`, and set `triIntrinsic_ = 1`. `x_` comes from `x0`, `restUV_` from `layoutUV`.
2. In `finalize()`, triangle loop: **for non-intrinsic triangles, fill `triRest_[t]` from `restUV_` exactly as now**,
   so the code that follows reads `triRest_` for every triangle. This keeps `setRestUV` + re-finalize working. Replace
   `mats_[panels_[triPanel_[t]].material]` with `mats_[triMat_[t]]`.
3. Per-vertex material (`damp_`, `muS_`, `muK_`, `radius_`, and the mass guard): for intrinsic vertices, use the
   material of the adjacent triangle with the **largest thickness** (radius) and the mean damping. For panel vertices,
   keep the old code path exactly.
4. Hinges: for each interior edge, compute the hinge direction `phi` **in the rest frame of triangle `s1.tri`**, from
   `triRest_[s1.tri]` corners matching `p1` and `p2`. Use the harmonic mean of the two triangles' `Beff`. Look up the
   `HingeMod` (key: global vertex pair, min/max) and multiply `Beff` by `bendScale`. Store `restAngle` in `BendC`
   (new field `rest`, default 0).
5. Mass: `density * restArea / 3` per corner, as now (rest area from `triRest_`).
6. Strain limiting (`cloth_implicit.cpp:774`): for intrinsic edges, take the rest length from `triRest_` (keep a map
   from edge to rest length built in `finalize`). Simplest acceptable fallback: skip strain limiting on intrinsic
   panels and say so in a comment.
7. Barrier contact (`cloth_barrier.cpp:35`) uses `restUV_` as rest positions. Do not support `barrierContact` with
   intrinsic panels in this phase: `FL_THROW` with a clear message if both are on.
8. XPBD and VBD read `TriC` and `BendC`, so they work once those are built. Do not test them for crochet. If `BendC.rest`
   is not 0, ignore it in XPBD and VBD and add a `// TODO`.

### 2.2 Signed hinges with a rest angle

Only hinges with `rest != 0` use the new path. Every existing hinge has `rest == 0` and must still go through the old
code (bit-identical results).

* Signed angle: `theta_s = atan2( dot(cross(n1, n2), e_hat), dot(n1, n2) )`, with **consistently oriented** triangle
  normals (both triangles listed counter-clockwise around the same side). For the existing unsigned code the normals
  are built "opposite" (`atan2(s, -d)`). Read lines 13-40 of `cloth.cpp` carefully before writing the signed version.
* Gradient: use Bridson et al. 2003 ("Simulation of clothing with folds and wrinkles", section 4.1). With their
  indexing (x1, x2 the opposite vertices, x3-x4 the shared edge E = x4 - x3, N1 = (x1-x3)x(x1-x4),
  N2 = (x2-x4)x(x2-x3)):
  `u1 = |E| N1/|N1|^2`, `u2 = |E| N2/|N2|^2`,
  `u3 = ((x1-x4).E/|E|) N1/|N1|^2 + ((x2-x4).E/|E|) N2/|N2|^2`,
  `u4 = -((x1-x3).E/|E|) N1/|N1|^2 - ((x2-x3).E/|E|) N2/|N2|^2`.
  These are dtheta/dx up to one global sign. **Check them with finite differences** (central, step 1e-6), as
  `test_basics` does for the unsigned hinge, and check that the gradients sum to zero. Fix the sign until both
  checks pass.
* Energy `0.5/alpha * (theta_s - rest)^2`. Hessian: Gauss-Newton `g g^T / alpha`, like the existing `add(4, ...)`. Put
  it in `assembleEnergy` and `variationalEnergy`. The static solve picks it up automatically.
* Sign convention, written in `cloth.hpp`: **positive `rest` folds the second triangle toward the side its own normal
  points to.** Section 5.5 turns this into RS/WS terms.

### 2.3 Strain-stiffening (J-curve) stretch

Crochet has a soft region (the loops slide and straighten) and then becomes stiff once the yarn is straight. A strap
carrying a laptop lives in the stiff region. Add to `FabricMaterial` (defaults keep the old behaviour):

```cpp
Real lockStrainWarp = 0, lockStrainWeft = 0;   // 0 = off. Strain at which loops lock (e.g. 0.15)
Real lockStiffnessRatio = 1;                   // stiffness above lock / stiffness below (e.g. 30)
```

In `TriC`, add `lockU`, `lockV`, `ratio`. The energy for C > lock > 0, with k = 1/alpha:
`E = 0.5 k lock^2 + k lock (C - lock) + 0.5 r k (C - lock)^2`. It is C1 at the lock strain. Gradient factor:
`k lock + r k (C - lock)`. Gauss-Newton Hessian factor: `r k`. Below the lock and in compression, nothing changes. The
`tension` term (`addTension`) uses the force `dE/dC`, so pass the right value there. Apply this in **both**
`assembleEnergy` and `variationalEnergy`. Put it in one inline helper used by both, so they cannot drift apart.

Also add the matching keys to `loadMaterialsFromString` (optional keys, so the old JSON still parses).

### 2.4 Vertex-to-vertex seams

`void addPointSeam(int a, int b, Real share);` builds a `SeamC` with `b0 = b1 = b`, `t = 0`,
`alpha = 1/(seamStiffness * share)` (`share` is the seam length the link stands for, in m), plus `noCollide_` entries
like the existing `link` lambda. Store the requests in a vector and build them in `finalize()`.

### 2.5 Contact force readback

```cpp
const std::vector<Real>& rigidContactForces() const { return rigidForce_; }  // N per vertex, last Newton iteration
const std::vector<Vec3>& contactNormals() const { return contactN_; }        // valid where the force > 0
```

Check in `cloth_implicit.cpp` that `contactN_` is written for every vertex with `rigidForce_ > 0`, and write the
answer in a comment.

### 2.6 Rounded oriented box collider in the core

Move the idea of `OrientedBoxCollider` into `collider.hpp` as `RoundedBoxCollider(center, Mat3 rotation, halfExtents,
cornerRadius)`: the box SDF of `half - r`, minus `r`. Add `setPose(center, rotation)`. A laptop needs rounded corners;
sharp corners give jumping normals. Leave the GDExtension's class alone and let it use the new one later (`§7`).

### 2.7 Tests for Phase 1 (new file `cloth_core/tests/test_intrinsic.cpp`, add it to both CMake lists)

1. **Equivalence:** build `meshRect(0.3, 0.3, 0.02)` as a normal panel, and the same mesh as an `IntrinsicMesh` whose
   `triRest` is copied from the vertex UVs. Drop both over a sphere for 60 frames. Max vertex difference
   **< 1e-9 m**.
2. **Regression:** before the change, save the final positions of `test_garment`'s tube (add an env var
   `FL_DUMP=path` to the test, or copy the setup into the new test). After the change, compare: bit-identical (max
   diff 0).
3. **Signed hinge FD:** random configurations, analytic vs central-difference gradient, relative error **< 1e-6**.
4. **Rest angle:** a 2-triangle strip with `rest = +0.5 rad`, no gravity, pin triangle 1. After settling, the measured
   signed angle is **0.5 ± 0.005** and the free vertex lies on the +normal side of triangle 1.
5. **J-curve:** one triangle pulled along u by a pinned force. Check energy and gradient continuity at C = lock
   (finite differences on both sides), and check the static equilibrium strain against the analytic piecewise-linear
   answer (**< 0.5 %**).
6. **Point seam:** two separate squares, `addPointSeam` at 3 vertex pairs 5 cm apart, with seamRampFrames 30. After
   120 frames the pair distance is **< 0.5 mm**.

Done when: all old ctest results are unchanged, and the six checks above pass. Write the numbers to
`CROCHET_STATUS.md`.

---

## 3. Phase 2: data, meaning the yarn database and hook list

New file `materials/yarns.json`. Also copy it to `godot/materials/` the way `fabrics.json` is copied (check how that
copy is made).

```json
{
  "_comment": ["STARTER values, not measured. Calibrate with a swatch (docs/CROCHET_PLAN.md section 10)."],
  "hooks_mm": {"B-1": 2.25, "C-2": 2.75, "D-3": 3.25, "E-4": 3.5, "F-5": 3.75, "G-6": 4.0, "7": 4.5, "H-8": 5.0,
               "I-9": 5.5, "J-10": 6.0, "K-10.5": 6.5, "L-11": 8.0, "M/N-13": 9.0, "N/P-15": 10.0},
  "fibres": {
    "acrylic": {"bend": 1.0, "tensile": 1.0, "lock": 1.0, "friction": 0.5, "sheen": 0.3},
    "cotton":  {"bend": 1.6, "tensile": 1.4, "lock": 0.7, "friction": 0.6, "sheen": 0.1},
    "wool":    {"bend": 0.8, "tensile": 0.8, "lock": 1.3, "friction": 0.7, "sheen": 0.05},
    "jute":    {"bend": 3.0, "tensile": 2.5, "lock": 0.5, "friction": 0.8, "sheen": 0.0},
    "tshirt":  {"bend": 0.6, "tensile": 0.5, "lock": 2.0, "friction": 0.6, "sheen": 0.1},
    "polyester_cord": {"bend": 1.2, "tensile": 1.6, "lock": 0.6, "friction": 0.4, "sheen": 0.5}
  },
  "yarns": [
    {"id": "acrylic_worsted", "name": "Acrylic worsted (CYC 4)", "fibre": "acrylic", "weight_class": 4,
     "grams": 100, "metres": 180, "diameter_mm": 2.5, "hook_mm": 5.0, "colour": "#b5523b"},
    {"id": "cotton_worsted", "name": "Cotton worsted (CYC 4)", "fibre": "cotton", "weight_class": 4,
     "grams": 50, "metres": 78, "diameter_mm": 2.6, "hook_mm": 4.5, "colour": "#e8dcc4"},
    {"id": "cotton_dk", "name": "Cotton DK (CYC 3)", "fibre": "cotton", "weight_class": 3,
     "grams": 50, "metres": 115, "diameter_mm": 2.0, "hook_mm": 4.0, "colour": "#6f8fa6"},
    {"id": "wool_worsted", "name": "Wool worsted (CYC 4)", "fibre": "wool", "weight_class": 4,
     "grams": 100, "metres": 200, "diameter_mm": 2.6, "hook_mm": 5.0, "colour": "#4f6b3a"},
    {"id": "cotton_cord_bulky", "name": "Cotton macrame cord 3 mm", "fibre": "cotton", "weight_class": 5,
     "grams": 250, "metres": 120, "diameter_mm": 3.2, "hook_mm": 6.0, "colour": "#d9cbb0"},
    {"id": "tshirt_yarn", "name": "T-shirt yarn (CYC 7)", "fibre": "tshirt", "weight_class": 7,
     "grams": 350, "metres": 120, "diameter_mm": 7.0, "hook_mm": 10.0, "colour": "#2b2b2b"}
  ],
  "swatches": []
}
```

Linear density `lambda = grams/metres` (g/m). If only WPI is known: `diameter_mm = 25.4 / WPI`.

The parser goes in `crochet_core` (Phase 3). Test: it loads, the ids are unique, and every yarn's fibre exists.

---

## 4. The stitch mechanics model (`crochet_core/src/stitch_model.cpp`)

This converts (yarn, hook, tension, stitch type, insertion) into the size and stiffness of one stitch. Everything
in this section is a **starter model**: the exponents and constants are tunables kept in one struct, so that
calibration (`§10`) can replace them.

### 4.1 Inputs and outputs

```cpp
enum class StitchType { Ch, SlSt, Sc, Hdc, Dc, Tr, MagicRing };
enum class Insertion  { Both, BLO, FLO, Space, RowEnd };   // RS-relative after compile (see 5.5)

struct StitchDims  { Real w, h, yarnLen, thickness; };       // metres; w along the row, h up the column
struct StitchMech  {                                         // homogenised, per unit width (SI)
    Real density;                       // kg/m^2
    Real kWale, kCourse, shear;         // N/m, low-strain region
    Real lockWale, lockCourse, lockRatio;
    Real bendWale, bendCourse;          // N*m (bendWale: curvature along the column)
    Real thickness, friction, damping;
};
StitchDims  stitchDims(const Yarn&, Real hook_m, Real tension, StitchType, Insertion, const Calibration*);
StitchMech  stitchMech(const Yarn&, Real hook_m, Real tension, StitchType, Insertion, const Calibration*);
FabricMaterial toFabric(const StitchMech&, const std::string& id);   // warp = wale, weft = course
```

### 4.2 Geometry (gauge)

Let `H` = hook diameter, `d` = yarn diameter, `T` = tension factor (0.9 tight, 1.0 normal, 1.1 loose).

| stitch | width w | height h | yarn per stitch |
| --- | --- | --- | --- |
| sc | `T (1.00 H + 0.60 d)` | `T (0.90 H + 0.60 d)` | `1.30 pi (H + d)` |
| hdc | w_sc | 1.45 h_sc | 1.35 L_sc |
| dc | w_sc | 1.90 h_sc | 1.75 L_sc |
| tr | w_sc | 2.50 h_sc | 2.20 L_sc |
| sl st (worked as a stitch) | w_sc | 0.35 h_sc | 0.60 L_sc |
| ch | 0.85 w_sc (its length) | 0.5 h_sc (its thickness, used for faces) | 0.45 L_sc |
| BLO/FLO variant | as base | as base | as base + 0 (the free loop is already counted) |

Sanity check: acrylic worsted (d 2.5 mm) on 5 mm: sc w 6.5 mm and h 6.0 mm, which is 15.4 sc and 16.7 rows per 10 cm.
Real worsted sc is about 14-16 sts and 16-18 rows. **Write this check as a unit test.**

**Gauge override:** if the user enters a measured gauge (sts and rows per 10 cm for sc, or for the stitch the
calibration was made with), use `w = 100 mm / sts` and `h = 100 mm / rows` for that stitch, and scale the other stitch
types by the ratios in the table.

Thickness: `t = 1.9 d` for sc/hdc/dc, `1.6 d` for sl st fabric. Turning chains that do not count add their yarn
(`ch` yarn x count) to the **mass** of the first stitch of the row, but create no node.

### 4.3 Mass

`density = yarnLen * lambda / (w * h)`, with lambda converted to kg/m. Sanity check: acrylic worsted sc gives about
0.42 kg/m^2, so a 10 x 10 cm swatch weighs about 4.2 g. This is the best-founded number in the model; calibration
replaces it with a weighed swatch.

### 4.4 Stiffness (starter constants, acrylic worsted sc on 5 mm = the reference)

Reference values (`R`): `kWale 400 N/m`, `kCourse 300 N/m`, `shear 60 N/m`, `lockWale 0.15`, `lockCourse 0.25`,
`lockRatio 30`, `bendWale 3.0e-4 N*m`, `bendCourse 2.5e-4 N*m`, `damping 1.0 1/s`, `compressionRatio 0.2`.

(The bending reference matches a Peirce cantilever bending length of about 4 cm for a sc swatch of 0.42 kg/m^2:
B = rho g c^3. Calibration replaces it.)

Scaling, with tightness `tau = d/H` (reference `tau0 = 0.5`) and `d0 = 2.5 mm`:

* `bend = R.bend * fibre.bend * (d/d0)^3 * (tau/tau0)^2 / T^2`
* `k = R.k * fibre.tensile * (d/d0)^1 * (tau/tau0)^3 / T^3`
* `lock = R.lock * fibre.lock * (tau0/tau) * T`
* `shear = R.shear * fibre.tensile * (tau/tau0)^2`

Stitch-type multipliers (relative to sc; W = wale, C = course):

| type | k W | k C | lock W | bend W | bend C | shear |
| --- | --- | --- | --- | --- | --- | --- |
| hdc | 0.70 | 0.95 | 1.30 | 0.70 | 0.90 | 0.80 |
| dc | 0.50 | 0.90 | 1.60 | 0.50 | 0.85 | 0.60 |
| tr | 0.35 | 0.85 | 1.90 | 0.35 | 0.80 | 0.45 |
| sl st | 2.00 | 1.60 | 0.40 | 1.40 | 1.30 | 1.50 |
| sc BLO/FLO (the faces of that row) | 0.60 | 1.00 | 1.30 | 0.50 | 1.00 | 0.90 |

Hinge modifiers (applied to **the course edges of the row that was worked into**, see `§5.6`):

| construction | bendScale | restAngle (RS-relative, `§5.5`) |
| --- | --- | --- |
| BLO round/row | 0.30 | +25 deg (folds toward the WS: the free front loop stays outside on the RS) |
| FLO round/row | 0.35 | -25 deg (folds toward the RS) |
| sl st round (`§11.3`) | 1.50 on the edge below and the edge above | 0, plus the sl st faces get the sl st material |
| surface sl st along a row | 0.70 | 0, plus course k x3 and lock x0.5 on the faces above and below that edge, plus mass of the sl st yarn |

The angle signs and sizes are guesses based on how these constructions behave. **Mark them UNVERIFIED in code and UI.**
The hinge swatch test (`§10`) measures them.

### 4.5 Tests (`test_crochet_model.cpp`)

* Worsted sc gauge in the range above.
* Monotonicity: with a smaller hook (same yarn), w and h get smaller, density and bend get larger, and lock gets
  smaller. With a thicker yarn on the same hook, everything gets stiffer. Cotton bends more stiffly than acrylic, and
  acrylic more than wool.
* `toFabric` maps warp = wale.

---

## 5. Phase 3: the pattern language and the stitch graph (`crochet_core`)

### 5.0 First: try an existing pattern language (CrochetPARADE), ticket T4.0

Before writing our own language, check whether **CrochetPARADE** can be the front end. It is an open-source crochet
pattern language with a parser, error checking, standard-symbol charts and a 3D model.

* Source: <https://github.com/stassev/CrochetPARADE> (its README says the primary home is Codeberg; use whichever is
  newer, and record which). GPLv3 code; the user manual (`Manual.md`, which also describes the grammar) is
  CC BY-NC-SA 4.0. JavaScript (`parse64.js`, `mesh64.js`, ...) plus C++ compiled to WebAssembly (`graph.cpp`,
  `periphery.cpp`).
* What it has that we need: back-loop/front-loop stitches (`scbl`, `scfl`), post stitches, custom stitch definitions,
  rows and rounds (a newline starts a new row/round), joining separate pieces, charts.
* What it lacks: physics. Its 3D shape only matches edge lengths (no gravity, load or material), and its docs say it
  can't tell which side is the inside. "Through both layers" is not mentioned in its feature list. So it can only be
  the **front end and the starting shape**; Failoring stays the physics.
* **Licence:** Failoring is AGPL-3.0, and GPLv3 code may be combined into an AGPL-3.0 work. Vendor the code under
  `third_party/crochetparade/` at a pinned commit, add its licence to `LICENSES/`, and add an entry to
  `THIRD_PARTY.md` like the existing ones. **Do not copy text from the manual** into our docs (non-commercial
  licence): describe the grammar in our own words and link to the manual.

**T4.0 spike: steps (one implementer, about one session):**

1. Vendor CrochetPARADE as above; record the commit and source in `THIRD_PARTY.md`.
2. Read its manual. Write every §11 fixture in its language: the pouch (`§11.1`) and all five bag variants
   (`§11.2`). Save them as `patterns/cp/*.txt`. For each fixture, list which constructs needed a workaround, and how.
   The hard ones: sc **through both layers** (the pouch's side seams); the **fold** (two layers of one piece); a strap
   worked off **a subset of a rim**; the strap's end **joined** with sl sts to the far rim; a **surface sl st** along a
   round; a **BLO round**.
3. Run its parser headless with node (`node` is already used by `app/build.js`). Write
   `tools/crochet/cp_export.mjs`: pattern text in, JSON out (schema below). Find in its code where the parsed
   stitches and their connections live, and dump them.
4. Check the dump: for every stitch, can we get the stitch type, the stitch(es) it was worked into, which loop
   (both/BL/FL/post), the stitch made just before it, its row/round, and the source line? Can we tell rows that were
   turned from rounds?
5. Run its 3D layout on the test circles (`mr 6 sc` with +6, +5 and +8 per round, 8 rounds) and a 40-stitch tube.
   Export the node positions. Does +6 come out flat, +5 cup-shaped and +8 ruffled?
6. Time the parser and the layout on the pouch and the bag.
7. Write `docs/crochet/T4.0_crochetparade.md`: findings per step, the fixtures, timings, and the decision below.

The JSON the exporter must produce (also the import format of `crochet_core`, whatever the decision):

```json
{"stitches": [{"id": 0, "type": "ch|slst|sc|hdc|dc|tr", "piece": "body", "row": 0, "index": 0,
               "loop": "both|blo|flo|fpost|bpost|space", "targets": [ids], "prev": -1, "line": 3}],
 "rows":     [{"id": 0, "piece": "body", "kind": "row|round", "turned": true}],
 "links":    [{"a": 120, "b": 845, "kind": "both_layers|join"}],
 "positions":[[x, y, z], ...],
 "errors":   [{"line": 7, "msg": "..."}]}
```

(`positions` = CrochetPARADE's layout, one per stitch, optional. `links` = the second target of a through-both-layers
stitch, or a join, which become point seams, §6.7.)

**Decision rule** (the orchestrator decides from the report, with numbers, and records it in BOARD.md):

* **A — adopt** if: all fixtures are expressible (workarounds are fine if the resulting stitch graph is right),
  step 4 gives type, targets, loop, prev and row for every stitch, and parsing the bag takes under 2 s. Then:
  * The user writes patterns in CrochetPARADE's language. Our own DSL (§5.2) is **not built**; keep §5.2 only as the
    specification of what the graph must contain. The `.fcro` fixtures in §11 are replaced by `patterns/cp/*.txt`.
  * T4.1 becomes the bridge (exporter + C++ importer into `StitchGraph`), and T4.2 adds what CrochetPARADE does not
    have: RS/WS facing (§5.5), point seams from `links`, count checks if it has none, and the §5.4 tests on the new
    fixtures.
  * If step 5 looks right, T5.3 uses CrochetPARADE's positions as the **initial layout** (rescaled so its edge lengths
    match our stitch sizes, §4.2), and keeps the §6.8 rules only as a fallback for pieces where its layout is
    inside-out or tangled. Relaxation (§6.9) stays.
  * Godot (T7.1): `CrochetPattern.compile` runs the exporter with `OS.execute("node", ...)`. If node is missing, show
    a clear error. A C++ or wasm port of the parser is a later ticket, only if this turns out too slow or awkward.
* **B — partial**: the grammar works, but something essential is missing (most likely through-both-layers). Adopt as
  in A and add the missing piece as a Failoring extension line in the pattern file (for example a
  `#! seam body.left(...) & body.left(...)` comment line that CrochetPARADE ignores and our importer reads). Write the
  exact syntax into `docs/CROCHET_DSL.md`.
* **C — reject** if the stitch graph cannot be extracted, or the fixtures can't be written even with workarounds.
  Then build our own DSL exactly as §5.2-5.5 say. Fixtures stay `.fcro`.

### 5.1 Module layout

```
crochet_core/
  include/failoring/crochet/  yarn.hpp  stitch_model.hpp  pattern.hpp  graph.hpp  crochet_mesh.hpp  scene.hpp  metrics.hpp
  src/                        yarn.cpp  stitch_model.cpp  parse.cpp    graph.cpp  crochet_mesh.cpp  scene.cpp  metrics.cpp
  tests/                      test_crochet_model.cpp  test_crochet_parse.cpp  test_crochet_mesh.cpp  test_crochet_scene.cpp
  tools/                      crochet_cli.cpp
patterns/                     the .fcro fixtures of section 11 (repo root, also copied to godot/crochet/patterns/)
```

`crochet_core` is a static library that links `cloth_core`, with no Godot dependency. Add it to the root
`CMakeLists.txt` with its tests in ctest, and to `gdextension/CMakeLists.txt`.

### 5.2 DSL: `.fcro` files, line-based, US terms

Lexical rules: `#` starts a comment. Keywords are case-insensitive. A line ending with `\` continues on the next line.
Numbers use `.` as the decimal point. Stitch counts in square brackets `[15]` are **assertions**: if the count does
not match, compiling fails with an error, line number and the actual count. This is a feature for the user.

Header statements:

```
pattern "Name"
yarn <yarn_id>                   # from yarns.json
hook <mm> | hook <US letter>     # "hook 5.0" or "hook H-8"
tension tight|normal|loose|<factor>
gauge <sts> sts <rows> rows per 10cm [stitch sc]   # optional override
```

Piece statements:

```
piece <name>
  ch <N>                                     # foundation chain (only right after 'piece' or 'join')
  mr <N> <stitch>                            # magic ring with N stitches, e.g. "mr 6 sc"
  join <target>                              # attach new yarn at a target loop (borders, straps)
  on <targetlist>                            # set the source sequence explicitly (straps off a rim)
  row[ <n>]: <instr>, <instr>, ... [, turn] [<count>]
  rows <a>-<b>: ...                          # repeat the row statement
  rnd[ <n>]: <instr>, ... [, join] [<count>] # 'join' = sl st to the first stitch of this round
  rnds <a>-<b>: ...
  mark <name>                                # name the row/round just completed: <piece>.<name>
  fasten off
layout <piece> flat | revolve | fold rows <r>|<r+1> | arc to <targetlist>
override <piece>.<row>[ sts <a>-<b>] <modifier>     # the "lego" edits from the chart (section 9)
surface slst on <piece>.<row>                # surface slip stitch along the top of that row (RS)
```

Instructions (inside a row or round):

```
<stitch> [in <where>]                 # one stitch into the next target (or the named one)
<stitch> <N>                          # N stitches into the next N targets   ("sc 48")
<stitch> across | around              # one into each remaining target
<N> <stitch> in next | in same        # N stitches into one target (increase, corner)
<stitch>2tog | <stitch>3tog           # one stitch into the next 2/3 targets (decrease)
inc | dec                             # shorthands: "2 sc in next" / "sc2tog" for the current base stitch
sk <N>                                # skip N targets
ch <N> [(counts as <stitch>)]         # chains inside a row. The first ch after a turn/join is a turning chain and is
                                      # not counted (no node) unless "(counts as ...)".
blo <instr> | flo <instr>             # loop modifier (worker-relative, as written in patterns)
( <instr>, ... ) x<N>  |  * <instr>, ... * x<N>   # repeats
working on opposite side of chain     # the source becomes the foundation chain's free loops, in made order
<stitch> evenly <N>                   # distribute N stitches over the remaining targets (Bresenham)
... through <targetlist> & <targetlist>   # each stitch takes one target from each list: joins two layers
```

`<stitch>` is one of `ch slst sc hdc dc tr`.

`<where>`: `ch <k> from hook`, `next`, `same`, `first`, `last`, `ch-sp`.

`<targetlist>`: `<piece>.<rowname|row(n)|rnd(n)>[ sts <a>-<b>]`, `<piece>.left(rows a-b)` / `<piece>.right(rows a-b)`
(row ends, in the given order, so `rows 25-1` runs downward), `<piece>.foundation` (the free bottom loops of the
foundation chain), and `<piece>.<label>` for labelled segments. A label is written `label <name>: <instr>, ...`
inside a round, and names the stitches those instructions make.

You may simplify the grammar if you keep every fixture in `§11` compiling. Write the grammar you actually implemented
into `docs/CROCHET_DSL.md`, with an example for every construct.

### 5.3 Compiler semantics (the source cursor)

The compiler keeps `src` (an ordered list of target loops) and `pos` (an index into it):

* After `ch N` as a foundation: `src` = the chains in **reverse** made order (you work back along the chain), `pos = 0`.
  `in ch 2 from hook` means `pos = 1`.
* After a row that ends with `turn`: `src` = the stitches of that row in reverse made order. Turning chains that do not
  count are excluded.
* After a round that ends with `join` (or continues as a spiral, without join): `src` = the stitches of that round in
  made order.
* `working on opposite side of chain`: `src` = foundation chains in made order, insertion = the chain's free bottom
  loop.
* `on <targetlist>` and `through A & B` set `src` (two parallel sources for `through`).
* Every stitch made gets: id, piece, row index, index in row, type, insertion (worker-relative and RS-relative), a list
  of **targets** (stitch ids), `prev` (the stitch made just before it in the same working sequence, or -1 after a
  `join`, `fasten off` or new piece), and `facing` (RS or WS, `§5.5`).
* `[N]` assertions count stitches made in that row, excluding non-counting turning chains.
* Errors (never crash): unknown stitch, running past the end of `src`, an assertion mismatch, a target that does not
  exist, `turn` inside a round. Every error carries a line number and a message a crocheter understands, e.g.
  `line 7: row 3 has 14 stitches, the pattern says [15] (did you skip the last stitch?)`.

Output: a `StitchGraph { std::vector<Stitch> st; std::vector<Row> rows; std::map<std::string, std::vector<int>>
names; std::vector<SurfaceSlst> surface; std::vector<Override> overrides; std::vector<LayoutHint> layout; }`.

### 5.4 Tests (`test_crochet_parse.cpp`)

* `§11.1` (the pouch) compiles to: 16 ch + 15 x 50 sc in the body, 80 sc in the border (25 + 15 + 25 + 15), and the
  strap 5 x N plus 5 sl st joins. List the counts and assert them.
* `§11.2` (the round bag) compiles in all five variants, with the counts in the comments.
* A deliberately wrong count (`[16]`) gives an error on the right line.
* A flat circle (`mr 6 sc`, then rounds of +6) gives rounds of 6, 12, 18, ...
* A row worked `blo` on a WS row reports RS-relative `FLO` (see 5.5).

### 5.5 Facing (RS/WS) and loop insertion: get this right

In flat rows you turn the work, so every other row is worked with the **wrong side (WS)** facing you. "Back loop" in a
pattern always means the loop away from the person crocheting. So:

* Row 1 is RS by default (`rs is row 1` can be changed with a header line `rs row 2`). Rows alternate after each
  `turn`. In rounds without turning, every round has the same facing as round 1 (RS).
* RS-relative insertion: if `facing == RS`, BLO stays BLO and FLO stays FLO. If `facing == WS`, BLO becomes FLO and
  FLO becomes BLO.
* **All mechanics use the RS-relative value.** Positive `restAngle` folds toward the WS (`§4.4`). In the mesh
  (`§6.4`), all triangle normals point to the RS.

---

## 6. Phase 4: from the stitch graph to a simulation mesh (`crochet_mesh.cpp`)

### 6.1 Nodes

There is one mesh vertex per stitch node: the top of the stitch, where the next row's stitches go. A magic ring adds
one centre node. Chains are nodes. Nodes that end up in no face (the unused "first ch from hook" of a foundation, for
example) are dropped, and their yarn mass is added to the nearest node that remains (pass that as extra lumped mass:
an `extraMass` vector in `IntrinsicMesh`, added in `finalize`, is a two-line extension to Phase 1).

### 6.2 Faces (incremental, while walking the stitches in made order)

For each stitch `s` with `prev = p` (same working sequence) and **primary targets** `T(s)` (the targets from the
first layer only; for `through A & B` the B targets become point seams, `§6.7`):

1. **Between `p` and `s`:** the top polyline is `p, [intermediate chain nodes made between p and s without targets],
   s`. The base polyline is the path in the existing graph from `last(T(p))` to `first(T(s))`. Use **BFS over edges
   that currently belong to fewer than 2 faces** (boundary edges: course edges, wale edges along a piece's side, chain
   edges), with a limit of 6 edges. If `last(T(p)) == first(T(s))`, the base is a single node (an increase). If no
   path is found, make no face and add a warning ("hole between stitch X and Y").
2. **Within `s` (decreases):** for consecutive targets `t_i`, `t_i+1` of `s`, fill the polygon `t_i ... t_i+1, s` the
   same way.
3. Triangulate each polygon with a **zipper** between the top polyline A and the base polyline B. Lay both out in 2D
   (6.3), then repeatedly add the triangle that advances A or B with the shorter new diagonal (the standard lofting
   algorithm). A quad gives 2 triangles, an increase 1, a decrease 1.
4. Every edge records how many faces use it. **Each triangle records its "source" stitch `s`**, which decides its
   material.

Stitches that are only chains (the foundation, ch-spaces) give faces only when something is worked into them, so a
foundation chain becomes part of row 1's faces.

### 6.3 Rest shape of each triangle

For a face polygon, put the base polyline on `y = 0`, centred, with segment lengths = the rest lengths of the base
edges: course edges use the width of the stitch at their right end, side edges (row ends) use that row's height, and
chain edges use the ch length. Put the top polyline on `y = Hf`, centred, with the course widths of its stitches.
`Hf = sqrt(max(h_s^2 - ((Wb - Wt)/2)^2, (0.3 h_s)^2))`, where `h_s` is the height of stitch `s` and `Wb`, `Wt` are the
total lengths of the polylines. Then the legs have about the stitch height. Each triangle's `triRest` corners are these
2D positions with **(u, v) = (y, x)** (u = wale up, v = course), to match warp = wale.

This is where the crochet happens: an increase triangle has a base of one point and a top of one stitch width, so
increases add rest area along the row. When there is more rest area than a flat sheet can hold, the solver makes it
ruffle. When there is less, it cups.

### 6.4 Orientation

After the faces are built, run a BFS over triangles. Flip a neighbour when it traverses the shared edge in the same
direction (manifold edges only). Then choose the global sign so that row 1's (or round 1's) RS normal is +n: the RS of
row 1 is the side facing the worker when row 1 is made. For a flat layout in the xy plane with row 1 RS toward +z, the
normal is +z. If a piece cannot be oriented, report an error with the stitch ids.

### 6.5 Materials

The material of each triangle is `stitchMech(type(s), RS-insertion(s))`, after applying any `override`. Deduplicate:
key = (type, insertion, override flags), giving a few `FabricMaterial`s. Surface sl st and sl st round modifiers adjust
copies (`§4.4`).

### 6.6 Hinge modifiers

* For a stitch `s` worked BLO/FLO (RS-relative) into targets `t`, the course edges of the **target row** between
  consecutive targets of the stitches of that row get a `HingeMod` (`§4.4`).
* Turn the RS-relative fold direction into the solver's sign with the triangle normals: compute the signed angle at
  rest layout and check the result with `§6.10`, test 5.
* Surface sl st: the course edges of the named row.

### 6.7 Seams from multi-target and join stitches

* `through A & B`: the stitch's B target becomes `addPointSeam(s, bTarget, share = w_s)`.
* A sl st that ends a joined round (`join`) is **not** a node. It closes the round: it makes a course edge between the
  last and the first stitch, so a face is built between them like any other pair.
* A `slst ... through strap.row(N) & border.backrim(...)`, which attaches the strap end: each sl st is a node worked into
  the strap's last row (primary), with a point seam to the rim stitch.

### 6.8 Initial 3D positions (layout)

Lay out each piece in its own frame, then place the frames:

* **`flat`** (the default for pieces worked in rows): row by row. A stitch's x is the mean x of its targets, its y is
  the targets' y + h_s, and z = 0. Chains continue along +x with their length. After each row, do a 1D least-squares
  smoothing along the row (keep the order; minimise `sum (x_i - x_{i-1} - w)^2 + 0.1 sum (x_i - xbar_targets)^2`, a
  tridiagonal solve). Row ends with turns need nothing special.
* **`revolve`** (the default for pieces started with `mr` or worked around both sides of a chain): a centre path P (a
  point for `mr`, the foundation chain's segment of length L for an oval). Round k has stitch count N_k and perimeter
  C_k = sum of widths. Offset radius `r_k = max((C_k - 2L)/(2 pi), r_{k-1} * 0.5)`. Height
  `z_k = z_{k-1} + sqrt(max(h_k^2 - (r_k - r_{k-1})^2, 0))`. Each stitch sits on the offset curve of P at radius r_k,
  at the arc-length parameter of the mean of its targets' parameters (round 1: spread evenly), at height z_k. Check:
  sc rounds with +6 per round give dr = 6w/2pi, about 0.95 w, about h, so the result is flat (z stays about 0), which
  is the real rule of thumb. Rounds without increases rise as a tube.
* **`fold rows r|r+1`**: after the flat layout, reflect rows > r about the line `y = y_fold` and set their
  `z = -(thickness + 1 mm)`. The two layers then lie on top of each other.
* **Pieces worked off another piece** (border, strap): stitch position = mean of its primary targets + h_s x the outward
  direction. The outward direction is the normalised (mean target - mean of the targets' own targets), or, for row-end
  targets, the in-plane normal of the edge pointing away from the piece.
* **`arc to <targetlist>`** (straps): place the strap's rows on a circular arc in the vertical plane through its start
  (on the rim) and the centroid of the join targets, with arc length = rows x h, bulging upward.

Finally, the whole assembly is placed in the scene (7.2).

### 6.9 Relaxation (zero-g pre-settle)

The layout is only a guess, so the triangles' rest shapes disagree with it, and the joining seams start open. Before
any loading:

* gravity 0, no colliders, `seamRampFrames = 60`, self collision on, damping x5 (add `SolverParams::dampingScale`,
  default 1, multiplied into `damp_`, a two-line change).
* Step until kinetic energy < 1e-9 J or 400 frames. Record the frames used and the largest seam gap.
* Fallback if folds tangle (the layers pass through each other): turn `barrierContact` on for the relaxation only. That
  requires the Phase 1 barrier + intrinsic support; either implement it, or reduce the layout error instead (better fold
  spacing, a better arc).

### 6.10 Tests (`test_crochet_mesh.cpp`)

1. **Swatch size:** a 20 sc x 20 row swatch, relaxed with zero g: width 20 w ± 2 %, height 20 h ± 2 %.
2. **Flat circle:** `mr 6 sc`, then +6 per round for 8 rounds, relaxed: out-of-plane deviation **< 5 % of the
   radius**. With +5 per round (under-increased) it is a **cup**: deviation > 25 % of the radius, all on one side. With
   +8 per round it **ruffles**: the sign of the deviation alternates around the rim at least 4 times. This test shows
   that the whole crochet geometry idea works. Do not move on until it passes.
3. **Tube:** 40 sts x 20 rounds without increases: circumference 40 w ± 3 %.
4. **Mesh hygiene:** no degenerate triangles (rest area > 1 % of w x h), every edge in at most 2 faces, one connected
   orientable component per piece (before seams), no warnings for the fixtures.
5. **Hinge sign:** 10 sc x 20 rows with row 11 BLO (on a RS row), zero g, the bottom 10 rows pinned flat. The top part
   rotates toward the WS by 25 ± 3 deg. FLO turns the other way.
6. **Mass:** the sum of vertex masses equals the yarn model's (total yarn length x lambda) within 0.5 %.

---

## 7. Phase 5: scenes, load and metrics, headless (`scene.cpp`, `crochet_cli`)

### 7.1 CLI

```
crochet_cli patterns/pouch.fcro --scene hang_pin|hang_peg|hand --load none|mba13|mbp14|mbp16|box:LxWxH_mm:kg \
    [--yarn id] [--hook mm] [--frames N] [--out out/run1] [--json]
```

It writes `out/run1.json` (metrics, `§7.5`), `out/run1.obj` (the final mesh, with stitch ids as a vertex attribute
comment), and `out/run1_rest.obj`. Exit code 0 means settled, 2 means not settled, 1 means error. Add a small PNG
snapshot by reusing the existing z-buffer renderer approach (`tools/render_lib.js` reads dumps; write a matching dump,
or call a tiny Python script with matplotlib). The user and you should be able to **look** at a result without Godot.

### 7.2 Scene setup

* Orientation: after relaxation, "up" for a bag = from the centroid of the bottom (the fold, or the base) to the
  centroid of the opening rim. Rotate the assembly so up = +y. The strap is centred over the origin.
* **hang_pin:** pin the strap's middle 2 rows (all their stitches) at their positions (the bag hangs from a hand).
* **hang_peg:** a horizontal `CapsuleCollider` (radius 1.2 cm, along x, length 10 cm) 2 cm above the strap's top. Drop.
  Turn on friction mode 2 (`bodyFrictionMode = 2`) and say in the status file whether the bag stays centred.
* **hand / shoulder:** later (`§12`).
* Gravity on after relaxation. Settle: step until kinetic energy < 1e-8 J for 30 frames, or a frame cap. Self
  collision on, with `selfThickness = min(0.8 t, 0.4 min(w, h))`. Record this and test it: with too large a value,
  neighbouring stitches fight.

### 7.3 Laptop and rigid load (`RigidLoad` in `scene.cpp`)

Presets: MacBook Air 13 (304.1 x 215 x 11.3 mm, 1.24 kg), MacBook Pro 14 (312.6 x 221.2 x 15.5 mm, 1.55 kg),
MacBook Pro 16 (355.7 x 248.1 x 16.8 mm, 2.14 kg). Corner radius 10 mm. **Check these figures against apple.com before
publishing them in the UI.**

Sequence:

1. Settle the empty bag hanging (`hang_pin` or `hang_peg`).
2. **Insert:** the box is a kinematic `RoundedBoxCollider` in a `UnionCollider` with the peg. Orient it with its long
   side horizontal along the bag's width and its thickness along the front-back axis. Start with its bottom face 1 cm
   above the opening, centred, and move it down 5 cm/s (pass the motion to `setColliderMotion` for friction). Stop when
   the bottom face is 1 cm above the lowest cloth point inside the bag, or the vertical contact force exceeds the box
   weight.
3. **Release:** two-way coupling, explicit and damped, every frame:
   * Force on the box: `F = m g + sum_i ( -f_i n_i )` over cloth vertices `i` with `rigidContactForces()[i] > 0` whose
     distance to the box SDF is below their contact radius + 1 mm (attribute force to the box, not the peg). Torque
     `tau = sum (p_i - c) x (-f_i n_i)`.
   * Translation: `v += dt F/m; v *= exp(-8 dt); c += dt v`, with |dc| at most 2 mm per frame.
   * Rotation, **stage B only** (stage A = translation only, which ships first): `omega += dt I^-1 tau`, damped
     the same way, then integrate the rotation (normalise).
   * `setPose`, and `setColliderMotion` = box displacement at each vertex.
   * Done when `|F_net| < 0.02 m g` and `|v| < 1 mm/s` for 30 frames. Cap at 1500 frames, then report "not settled".
4. **Fit check:** before inserting, compare the box's width and height with the bag's inner rest width
   (circumference/2) and depth. If it is bigger than 1.05 x the inner size: result `"fits": false` with the
   shortfall in cm, and the box is not inserted (the UI says "the laptop is X cm wider than the bag"). If it is between
   0.95 and 1.05: insert, and warn "tight fit".

### 7.4 Tests (`test_crochet_scene.cpp`)

1. **Strap stretch, analytic:** one sc strip of 5 sts x 60 rows, top row pinned, a 1 kg box hung from the bottom row
   by point seams to a rigid plate (or pin the bottom row to a moving kinematic plate and weigh it with the contact
   force). Check the line tension per width `T = m g / w_strip`, and the strain from the J-curve (`§2.3`). Simulated
   elongation within 3 % of the analytic value.
2. **Force balance:** the pouch in `hang_pin` with a 0.3 kg box (the pouch is too small for a laptop: this is a phone).
   Sum of pinned-vertex reaction forces = (bag mass + 0.3 kg) g within 2 %. To get the pinned-vertex reactions, solve
   one extra step and read the force from the residual, or replace the pins with stiff point seams to fixed anchors and
   read their spring forces.
3. **Settles:** all `§11` fixtures, empty, `hang_pin`: settle within the frame cap, Newton cap hits < 1 % of substeps,
   no self intersections (`selfIntersections() == 0`), min clearance to colliders > -0.5 mm.
4. **Fit check:** the pouch with the MBA13 preset gives `fits: false`. The `§11.2` bag gives `fits: true`.

### 7.5 Metrics (`metrics.cpp`, all in the JSON)

* `yarn_m`, `mass_g`, `skeins` (from the yarn's metres per skein): the user can check against a real bag on a kitchen
  scale.
* Finished flat size (from the relaxed zero-g shape): width and height in cm.
* Hanging: `drop_cm` (pin/peg to the lowest point), `bag_height_cm`, `opening_gape_cm` (front rim centre to back rim
  centre), `strap_length_cm` (rest and loaded, and elongation %).
* Base: `base_sag_mm` (largest deviation of the base faces from their best-fit plane) and `corner_angle_deg` (mean
  signed dihedral along the base-to-wall row: about 90 means a crisp box corner, about 150 a rounded slump). **This is
  the number that should separate the BLO, FLO, plain and sl st variants.**
* Strain: max and 95th percentile of course and wale strain, and where they occur (row and stitch).
* With a load: `load_tilt_deg`, `load_top_above_rim_cm` (negative = inside), `max_contact_pressure_kPa`, `fits`.
* Solver health: frames, Newton iterations, cap hits, line-search failures, final kinetic energy.

---

## 8. Phase 6: Godot bindings and the crochet scene

### 8.1 GDExtension

* New class `CrochetPattern` (RefCounted): `compile(text: String, yarns_path: String) -> bool`, `get_errors() ->
  Array[Dictionary{line, msg}]`, `get_summary() -> Dictionary` (stitch count, rows, yarn_m, mass_g, flat size),
  `get_chart() -> Dictionary` (per stitch: id, piece, row, index, type, insertion, facing, targets, layout x/y),
  `set_yarn(id)`, `set_hook(mm)`.
* New class `CrochetScene` (RefCounted). It wraps a `fl::ClothSolver` plus `scene.cpp`, so all the logic of `§7` is
  shared with the CLI and **not** reimplemented in GDScript: `build(pattern, scene: String, load: String) -> bool`,
  `relax_step(n)`, `step(n)`, `phase() -> String` ("relax", "hang", "insert", "release", "done"), `get_positions()`,
  `get_indices()`, `get_normals()`, `get_stitch_of_vertex()`, `get_metrics() -> Dictionary`, `get_load_transform() ->
  Transform3D`, `get_peg() -> Dictionary`.
* Register both in `register_types.cpp`, add the sources to `gdextension/CMakeLists.txt`, and rebuild with
  `tools/build_gdext.sh`.
* Headless test `godot/tests/crochet_api_check.gd/.tscn`: compile the pouch, check the counts, build hang_pin, run 200
  frames, metrics present, no NaN.

### 8.2 Scene `godot/crochet/crochet.tscn` + `crochet_main.gd`

Layout: three resizable panes, the same feel as the editor; look at `layout.gd` and reuse what you can.

* **Left, Pattern:** a `CodeEdit` with the `.fcro` text. Syntax highlighting with `CodeHighlighter` (keywords, stitch
  names, numbers, `[counts]`). Compile 400 ms after the last keystroke. Errors appear as red line markers with a
  tooltip, and a summary line under the editor ("766 stitches, 41 m of yarn, 23 g, flat 10.0 x 29.4 cm"). File
  buttons: Open, Save, Save as (`user://crochet/`), and a dropdown of the bundled fixtures.
* Under it, **Yarn and hook:** a yarn dropdown (from `yarns.json` + user swatches), hook dropdown (mm and US), tension,
  and gauge override fields. Changing any of them recompiles. These override the pattern's header lines, and the UI
  shows when they do.
* **Centre, 3D view:** the studio (`Studio.build(self)`), an orbit camera (`cam_views.gd`), the cloth mesh, the peg or
  hand marker, and the laptop as a rounded box (aluminium grey, a dark keyboard deck face). Camera buttons: Front, Side,
  3/4, Below.
* **Right, Chart and Metrics:** tabs. Chart in `§9`. Metrics: the table of `§7.5`, and the "UNVERIFIED starter
  values" notice whenever no calibration is used.
* **Top bar:** Scene (Hang from hook / Hang from hand), Load (Empty / MacBook Air 13 / Pro 14 / Pro 16 / Custom box...),
  **Run** (relax, hang, load, as one flow with a progress label showing the phase), Stop, **Compare** (`§9.3`), Back to
  editor.
* Run the simulation on a worker thread, as `editor.gd` does (line 279). Copy the pattern, don't share it: never touch
  nodes from the thread.
* Entry: add one button "Crochet" to the editor's top bar next to "Demo" (`editor.gd` around line 583), calling
  `get_tree().change_scene_to_file("res://crochet/crochet.tscn")`. That is the only change to `editor.gd`.

### 8.3 Look, phase 1 (procedural stitches on the surface)

New shader `godot/crochet/crochet_surface.gdshader`:

* UV = stitch coordinates: u = index along the row (continuous over the faces), v = row index. Use the compiler's
  layout coordinates divided by w and h.
* Per stitch cell, a procedural normal and height pattern for a sc "V" on top and a post below: two tilted ellipses for
  the V, a vertical bar for the post, the yarn twist as fine diagonal stripes. Shade with the yarn colour. Use a
  vertex attribute (`CUSTOM0` or `COLOR.a`) for the stitch type and the RS-insertion. For BLO rows, add a raised
  horizontal ridge on the RS (the free front loop) along the target row. For FLO rows, a ridge on the WS (visible
  inside the bag).
* Thickness: render the cloth as a double-sided shell offset by +-t/2 along the normals (two surfaces), so the bag looks
  4-5 mm thick, not paper thin.
* Fibre look: cotton matte (roughness 0.9), acrylic with slight sheen (0.6), wool fuzzy (a rim-light term + roughness
  1.0).
* Godot test: a screenshot via the existing shot helpers, if they work headless. Otherwise just check that the shader
  compiles.

---

## 9. Phase 7: chart, "lego" edits and compare

### 9.1 Chart view (`crochet_chart.gd`, a Control with `_draw`)

* Rows as horizontal lines (row 1 at the bottom). Rounds as concentric rings, or ovals from the centre path for
  `revolve` pieces. Positions come from `get_chart()` layout x/y.
* Standard US symbols: ch = small oval, sl st = filled dot, sc = "x" (or "+"), hdc = "T", dc = "T" with one slash,
  tr = two slashes, inc = two symbols sharing a base point, dec = two legs joined at the top. BLO = an arc "‿" under the
  symbol, FLO = "⁀". Joins and through-both-layers get a distinct colour.
* Hover shows the stitch id, row, index, type, insertion (worker and RS) and line number in the pattern. Click selects
  the stitch: highlight it in 3D (vertex colour) and the pattern line in the editor. The reverse direction too: click
  in 3D, raycast against the cloth triangles, then the nearest vertex gives the stitch.

### 9.2 Lego edits

Right-click a stitch or a whole row, or drag-select a range. Menu: change type (sc/hdc/dc/tr/sl st), BLO, FLO, both
loops, surface sl st on this row, and "make the whole row like this". Each edit **appends an `override` line to the
pattern text** (`override body.row(12) sts 3-7 blo`), which then recompiles. The text stays the single source of truth,
and undo is the CodeEdit's undo. Overrides change type and insertion only, never stitch counts. If an override
changes counts, the compiler rejects it with a message.

### 9.3 Compare (A/B)

* Compare loads a second pattern (or the same pattern with different yarn/hook settings) into slot B. Both are
  simulated with the same scene and load, and drawn side by side 50 cm apart in the same studio, with labels.
* The Metrics tab shows A, B and the difference per row, with the differences that matter in bold: corner angle, sag,
  drop, strap elongation, mass.
* A "variants" shortcut for `§11.2`: a dropdown that swaps the hinge round between plain, BLO, FLO, sl st round and
  surface sl st, and runs all five in one go (sequentially, on the worker). Result: a table and a row of five bags.
  This is the user's actual question, so make this flow pleasant.

---

## 10. Phase 8: calibration wizard (swatches)

A dialog that turns four simple kitchen-table measurements into a calibration record saved in `user://crochet/
swatches.json`, keyed by yarn, hook and stitch. The stitch model uses a record instead of the starter values whenever
one matches; the UI then shows "calibrated" instead of "UNVERIFIED".

1. **Gauge:** a 12 x 12 cm swatch. Count stitches and rows in 10 cm, giving w and h.
2. **Weight:** weigh the swatch (g), and enter its size or its stitch and row counts. That gives the density and the
   yarn per stitch (`density / lambda * w * h`).
3. **Cantilever (Peirce), both directions:** a strip 3-4 cm wide and at least 20 cm long, once worked as a narrow tall
   strip (long axis = wale) and once as a short wide strip (long axis = course). Slide it over a table edge until the
   tip touches a 41.5 deg line (a printed card with the angle to hold against it). Overhang L. Bending length c = L/2,
   and `B = rho g c^3`. Show a diagram.
4. **Stretch:** hang a 10 cm wide strip, mark 10 cm, hang weight W1 (e.g. 250 g) and W2 (e.g. 1 kg) from a clip bar
   across the full width. Measure L1 and L2. Line tension `T = W g / width`, strain `e = (L - L0)/L0`. Fit the J-curve:
   `k0 = T1/e1`, `ratio = ((T2 - T1)/(e2 - e1)) / k0`, `lock = e1` if the second slope is clearly steeper, otherwise
   say "no lock reached, use a heavier W2". Do it for wale and course.
5. **Hinge (optional, per construction):** a swatch of 10 rows, the hinge row (BLO/FLO/...), 10 rows. (a) Lay it free
   on a table, RS up, and measure the angle the upper part settles at, which is the rest angle. (b) Clamp the lower half
   flat with the hinge at the table edge, and measure the droop angle of the upper half. Fit `bendScale` by running the
   same setup in the simulator (an automatic 1D bisection on bendScale, about 6 runs) until the droop matches. This
   reuses the solver, so it is consistent with the simulation.

Record format: `{yarn, hook_mm, stitch, w, h, density, bendWale, bendCourse, kWale, kCourse, lockWale, lockCourse,
lockRatio, hinge: {blo: {bendScale, restAngle}, ...}, date, notes}`.

---

## 11. Fixture patterns (put them in `patterns/` and keep them compiling)

The counts in comments are what the compiler must report. If a fixture does not compile, fix the fixture or the
compiler, and write down which one and why.

### 11.1 `pouch.fcro` (the user's pattern, as given)

```
pattern "Simple pouch (user pattern)"
yarn acrylic_worsted
hook 5.0

piece body
  ch 16
  row 1: sc in ch 2 from hook, sc across, turn [15]
  rows 2-50: ch 1, sc across, turn [15]
  fasten off
layout body fold rows 25|26          # row 1 meets row 50; the fold is the bottom

piece border                          # seam side 1, front rim, seam side 2, back rim, as one round
  join body.left(rows 25-25)
  rnd 1: ch 1, \
    label side1: sc across through body.left(rows 25-1) & body.left(rows 26-50), \
    label frontrim: sc across body.foundation, \
    label side2: sc across through body.right(rows 1-25) & body.right(rows 50-26), \
    label backrim: sc across body.row(50), \
    join [80]
  # no fasten off: the strap continues from here in the real pattern; yarn continuity is not simulated

piece strap
  on border.frontrim sts 6-10
  row 1: sc across, turn [5]
  rows 2-80: ch 1, sc across, turn [5]     # "desired length": 80 rows, about 48 cm
  row 81: slst across through strap.row(80) & border.backrim sts 10-6 [5]
  fasten off
layout strap arc to border.backrim sts 10-6
```

(In this fixture, the join stitch `through A & B` on `side1`/`side2` makes the seam. Check which of `body.left`
and `body.right` is which against your row-end definition, and fix the fixture rather than the definition.)

### 11.2 `bag_round_<variant>.fcro` (an oval base plus walls in rounds, for a 13" MacBook Air)

The counts below were worked out by hand. Verify them.

```
pattern "Laptop bag in the round"
yarn cotton_worsted
hook 4.5

piece bag
  ch 51
  rnd 1: sc in ch 2 from hook, sc 48, 3 sc in next, working on opposite side of chain, sc 48, 2 sc in next, join [102]
  rnd 2: ch 1, 2 sc in next, sc 48, (2 sc in next) x3, sc 48, (2 sc in next) x2, join [108]
  rnd 3: ch 1, sc, 2 sc in next, sc 48, (sc, 2 sc in next) x3, sc 48, (sc, 2 sc in next) x2, join [114]
  mark base
  # ---- hinge round: the variant line goes here (see below) ----
  rnds 5-44: ch 1, sc around, join [114]
  mark top
  fasten off
layout bag revolve

piece strap
  on bag.top sts 26-31
  row 1: sc across, turn [6]
  rows 2-100: ch 1, sc across, turn [6]
  row 101: slst across through strap.row(100) & bag.top sts 88-83 [6]
  fasten off
layout strap arc to bag.top sts 88-83
```

Variants (round 4):

* `plain`: `rnd 4: ch 1, sc around, join [114]`
* `blo`: `rnd 4: ch 1, blo sc around, join [114]`
* `flo`: `rnd 4: ch 1, flo sc around, join [114]`
* `slst`: `rnd 4: ch 1, blo slst around, join [114]`, then `rnd 5` is worked into those sl sts (the sl st round, which
  gives a firm, non-stretching edge)
* `surface`: `rnd 4` as plain, plus `surface slst on bag.base`

Expected size at the model's gauge (cotton worsted on 4.5 mm: sc about 6.1 x 5.6 mm): 114 x 6.1 mm is about 69 cm
around, so about 35 cm wide flat, and 41 rounds of wall is about 23 cm. The MacBook Air (30.4 x 21.5 cm) fits, with its
top about 1.5-2 cm below the rim. If the cotton gauge comes out very different, adjust the chain
length and the strap positions, and write down the numbers.

### 11.3 What "a chain around the bottom" means

The user mentioned "a chain around the bottom before going up the column". This is ambiguous. The plan covers the two
likely meanings: (a) a round of sl st (the `slst` variant) and (b) surface sl st / surface chain on the outside along
the base edge (the `surface` variant). If the user meant a round of chains worked as stitches (e.g. `(sc, ch 1, sk 1)
around`), the DSL already supports it: chains in a round become nodes, and the next round works into the ch-spaces.
Add it as a 6th variant if asked.

---

## 12. Later (not in this plan's scope, write down ideas as you go)

* **Yarn-level look:** sweep yarn tubes (radius d/2, 6 sides) along canonical per-stitch yarn paths (sc: the V of front
  and back loop on top, the post down to the target; BLO: the post goes into the back loop only, and the target's
  front loop stays as a visible free ridge), embedded in the deformed triangles (barycentric coordinates + normal offset
  x t). Build it once after settling. About 300k triangles for the pouch, millions for the laptop bag, so use LOD.
  This is the biggest visual fidelity step after the physics. Do it after Compare works.
* Shoulder carry on the mannequin (the body SDF and the box props already exist in `FailoringSim`).
* Coarsening for large bags (one node per 2 x 2 stitches with homogenised material) if 5000+ stitch bags are too slow.
  Measure first.
* A static solve (`solveStatic`) for faster settling, once self contact works there (it does not: see
  `StaticParams::selfCollision`).
* Post stitches (FPdc/BPdc ribbing), puff/bobble, colourwork per stitch (that one is cheap: a colour per stitch in the
  DSL).
* Amigurumi with stuffing (an internal pressure term).

---

## 13. Phase checklist (copy into `CROCHET_STATUS.md` and tick off)

| # | Phase | Done when |
| --- | --- | --- |
| 1 | Solver extensions (`§2`) | old ctest unchanged, bit-identical regression, 6 new checks pass |
| 2 | Yarn data (`§3`) | yarns.json loads, unit tests |
| 3 | Stitch model (`§4`) | gauge sanity and monotonicity tests |
| 4 | DSL + graph (`§5`) | all fixtures compile with the stated counts, error-message tests |
| 5 | Mesh + layout + relax (`§6`) | flat/cup/ruffle circle, tube, swatch size, hinge sign, mass |
| 6 | Scenes + load + metrics + CLI (`§7`) | strap analytic, force balance, settle, fit checks; PNG snapshots of all fixtures in `docs/crochet-shots/` |
| 7 | Godot bindings + scene (`§8`) | crochet_api_check passes; the user can open, edit, run, and see the pouch hang on their Mac |
| 8 | Chart, lego, compare (`§9`) | five-variant comparison runs from one click; the table shows different corner angles |
| 9 | Calibration (`§10`) | a swatch record changes the result and the UI says "calibrated" |
| 10 | Yarn-level look (`§12`) | optional |

After phase 6, before any Godot work, **write a short report for the user** in `docs/CROCHET_STATUS.md`: the five
variants' metrics table (corner angle, sag, drop, strap elongation, mass) and the snapshots. That is the first useful
answer to their question, and it tells you whether the hinge model separates the variants at all. If all five corner
angles fall within 5 deg of each other, the hinge model is too weak. Say so instead of building UI on top of it.

---

## 14. Execution protocol: orchestrator + subagents

This section is about **how** the work gets done. The agents running it tend to stop early, declare things done that
are not, and wait for a human who is not there. Every rule below exists to prevent one of those. Follow it literally.

### 14.1 Roles

| Role | Who | Does | Never does |
| --- | --- | --- | --- |
| **Orchestrator** | one long-running agent (suggested: the stronger reasoner of the available models, e.g. Spark Muse 1.3 at xhigh) | owns `docs/crochet/BOARD.md`; picks the next ticket; writes the subagent prompt; waits for the commit; **re-runs the tests itself**; reviews the diff; accepts or sends back; merges; loops | writes feature code; trusts a subagent's report without re-running the commands; ends its turn while a ticket is open |
| **Implementer** | a fresh subagent per ticket  | exactly one ticket; writes code and tests; runs them; commits; returns the report block (§14.6) | touches files outside the ticket's scope; edits tests or acceptance numbers to make them pass; leaves TODO/stub code; returns without a commit |
| **Verifier** (optional, for tickets marked ★) | a fresh subagent that did not write the code (either model) | checks the acceptance claims from scratch: rebuilds, runs, and does the mutation check (§14.7 step 5) | fixes code (it only reports) |

The model assignment is a suggestion: put whichever model follows long instructions most reliably in the orchestrator
seat. If an implementer fails the same ticket twice, the orchestrator re-sends it to **the other model** (§14.8).

"Subagent" means whatever the harness uses to start a separate agent with its own context (a Task/Agent tool, a new
session in a worktree, etc.). Each subagent starts **cold**: it knows only what the prompt says, plus the repo.

### 14.2 Files the orchestrator owns

* `docs/crochet/BOARD.md` — the ticket table (format below). The single source of truth for progress. Updated after
  every state change, and committed (`board: T1.3 -> review`), so a new orchestrator can resume from it.
* `docs/crochet/reports/<ticket>.md` — the implementer's report and the orchestrator's review notes, per round.
* `docs/CROCHET_STATUS.md` — the human-readable log of §1 (numbers per phase).

BOARD.md format:

```
| Ticket | Title | Depends on | Status | Owner model | Round | Commit | Notes |
| T1.1 | triRest_/triMat_ refactor | T0.1 | done | glm | 1 | a1b2c3d | regression bit-identical |
| T1.2 | addIntrinsicPanel | T1.1 | changes_requested | glm | 2 | e4f5a6b | extraMass missing |
```

Statuses: `todo` → `in_progress` → `review` → (`changes_requested` → `in_progress` …) → `done`, or `blocked` (with
the reason and what would unblock it). Nothing else.

### 14.3 Tickets

Each ticket is small enough for one subagent session. "Acceptance" points to the numbers in the plan; the orchestrator
checks exactly those. ★ = run the Verifier too.

| Ticket | Title | Plan § | Depends on | Acceptance |
| --- | --- | --- | --- | --- |
| T0.1 | Branch `crochet/main`, baseline ctest output saved to `docs/crochet/baseline_ctest.txt`, BOARD.md, CROCHET_STATUS.md, regression dump of test_garment positions | 1, 2.7.2 | – | files exist; baseline committed |
| T1.1 ★ | Refactor finalize() to per-triangle `triRest_`/`triMat_` (no new API) | 2.1 steps 1-3 | T0.1 | all old tests pass; test_garment positions **bit-identical** to the T0.1 dump |
| T1.2 ★ | `IntrinsicMesh`, `addIntrinsicPanel`, `extraMass`, strain-limit edge rest lengths, barrier guard | 2.1 steps 4-8, 6.1 | T1.1 | §2.7 test 1 (< 1e-9 m) |
| T1.3 ★ | `HingeMod`, signed hinge with rest angle | 2.2 | T1.2 | §2.7 tests 3, 4; old tests bit-identical |
| T1.4 ★ | J-curve stretch (+ JSON keys) | 2.3 | T1.1 | §2.7 test 5 |
| T1.5 | `addPointSeam`, `SolverParams::dampingScale` | 2.4, 6.9 | T1.1 | §2.7 test 6 |
| T1.6 | Contact force accessors, `RoundedBoxCollider` | 2.5, 2.6 | T1.1 | unit test: box SDF values at 10 points vs hand-computed; a cloth patch resting on a box: sum of forces = weight ± 2 % |
| T2.1 | `materials/yarns.json` + loader (in `crochet_core`, CMake target created here) | 3 | T0.1 | loads; ids unique; fibres exist |
| T3.1 | Stitch model | 4 | T2.1 | §4.5 tests |
| T4.0 | **CrochetPARADE spike** + decision A/B/C | 5.0 | T0.1 | `docs/crochet/T4.0_crochetparade.md` with all 7 steps answered with evidence; fixtures in `patterns/cp/` (or a written reason why one is impossible); the orchestrator's decision recorded in BOARD.md |
| T4.1 | **A/B:** bridge: `tools/crochet/cp_export.mjs` + C++ JSON importer into `StitchGraph`, error passthrough with line numbers. **C:** DSL lexer + parser → AST; `docs/CROCHET_DSL.md` | 5.0 / 5.2 | T4.0 | all fixtures import (A/B) or parse (C); error with the right line number for 5 malformed inputs |
| T4.2 ★ | **A/B:** facing/RS insertion, point seams from `links`, count checks, extension lines (B). **C:** compiler: cursor semantics, graph, count assertions, facing/RS insertion | 5.0, 5.3, 5.5 | T4.1 | §5.4 tests (on the fixtures of whichever format was chosen) |
| T5.1 ★ | Faces (BFS base path, zipper), rest shapes, orientation | 6.2-6.4 | T4.2, T1.2 | §6.10 test 4 (hygiene) on all fixtures |
| T5.2 | Materials, hinge mods, seams → `IntrinsicMesh` | 6.5-6.7 | T5.1, T3.1, T1.3, T1.5 | §6.10 test 6 (mass) |
| T5.3 | Layouts. **A/B with a good T4.0 step 5:** CrochetPARADE positions rescaled to our stitch sizes, §6.8 rules only as a per-piece fallback (inside-out/tangled). **Otherwise:** flat, fold, revolve, attached, arc | 5.0, 6.8 | T5.2 | snapshot PNGs of the initial layout for every fixture committed under `docs/crochet-shots/layout/` |
| T5.4 ★ | Relaxation + mesh tests | 6.9, 6.10 | T5.3 | §6.10 tests 1, 2, 3, 5 (circle flat/cup/ruffle is the key one) |
| T6.1 | Scenes `hang_pin`, `hang_peg`, settle loop | 7.2 | T5.4 | §7.4 test 3 for the pouch |
| T6.2 ★ | `RigidLoad`: insert, release, fit check | 7.3 | T6.1, T1.6, T1.4 | §7.4 tests 1, 2, 4 |
| T6.3 | Metrics | 7.5 | T6.1 | JSON contains every key of §7.5; corner angle of a hand-built 90° fold reads 90 ± 1 |
| T6.4 | `crochet_cli` + PNG snapshots | 7.1 | T6.3 | all fixtures run from the CLI; PNGs in `docs/crochet-shots/` |
| T6.5 | **Five-variant report** (USER GATE, §14.9) | 13 | T6.2, T6.4 | table + snapshots in CROCHET_STATUS.md |
| T6.6 | Real-bag fixture: the user's finished bag (measurements from the user) as a calibration + validation case | 10 | T6.5, user data | simulated flat size within 5 % of the measured bag; mass within 10 % |
| T7.1 | GDExtension `CrochetPattern`, `CrochetScene` (A/B: `compile` runs `cp_export.mjs` through `OS.execute("node", ...)`, see 5.0) | 8.1, 5.0 | T6.4 | `crochet_api_check` passes headless |
| T7.2 | Crochet scene skeleton: panes, pattern CodeEdit with live compile + error markers, run on a worker thread | 8.2 | T7.1 | headless test: load pouch, compile, run 200 frames, no errors |
| T7.3 | Yarn/hook panel, metrics tab, scene/load menus, editor entry button | 8.2 | T7.2 | headless test drives each menu once |
| T7.4 | Surface shader, double-sided thickness | 8.3 | T7.2 | shader compiles; screenshot if the harness can |
| T8.1 | Chart view | 9.1 | T7.2 | headless test: chart has one symbol per stitch for the pouch |
| T8.2 | Selection chart ↔ 3D ↔ pattern line | 9.1 | T8.1 | test: select stitch 100 in the chart → vertex highlighted, line number right |
| T8.3 | Lego overrides | 9.2 | T8.2 | test: override row 12 BLO → pattern text gains the line, recompile, hinge mods present |
| T8.4 | Compare + five-variant run | 9.3 | T8.3 | test: five runs, table with five corner angles |
| T9.1 | Calibration wizard + records | 10 | T7.3 | entering the example numbers of §10 changes `stitchMech` output; UI says "calibrated" |
| T9.2 | Hinge auto-fit (bisection) | 10.5 | T9.1 | recovers a known bendScale from a simulated droop within 5 % |

Tickets whose dependencies are done and that share no files may run **in parallel** (e.g. T1.4, T1.5, T1.6, T2.1,
T4.0 after T1.1; T4.0 can start right after T0.1). The T4.0 decision changes the content of T4.1, T4.2, T5.3 and
T7.1: before sending any of them, the orchestrator writes the chosen option's version into the prompt. Each parallel implementer works in its own git worktree on its own branch (`crochet/T1.4` etc.); the
orchestrator merges into `crochet/main` after review. At most 3 in parallel.

Godot tickets (T7.x-T9.x) need a Godot 4.3 binary. If the environment has none: mark them `blocked: needs Godot 4.3
binary or the user's Mac`, and finish everything else first.

### 14.4 The orchestrator's loop (do exactly this)

```
on start (or resume):
    read docs/CROCHET_PLAN.md (all), docs/crochet/BOARD.md, the last 3 reports
    if BOARD.md does not exist: run T0.1 yourself (it is setup, not feature code), then continue

loop:
    ready = tickets with status todo/changes_requested whose dependencies are all done
    if ready is empty and nothing is in_progress/review:
        if every ticket is done or blocked: go to FINISH
        else: a dependency is blocked -> write why in BOARD.md, go to FINISH
    for each ticket in ready (max 3 in flight, no shared files):
        write the prompt from the template (14.5), including the ticket's acceptance and any review findings
        start the implementer subagent; set status in_progress; commit BOARD.md
    for each subagent that returns:
        if no report block or no commit hash: send it back immediately: "You returned without a commit/report.
            Continue the ticket until the acceptance passes, commit, then return the report block." (counts as a round)
        else: set status review; run REVIEW (14.7)
            accepted -> merge, status done, update CROCHET_STATUS.md with the numbers, commit
            rejected -> status changes_requested, round += 1, write findings into reports/<ticket>.md
                        if round > 3: ESCALATE (14.8)
    if a USER GATE ticket is done: do the gate (14.9)
    goto loop        # never end your turn here

FINISH:
    write the final summary into CROCHET_STATUS.md: done tickets, blocked tickets with reasons, open risks
    only now may you end your turn
```

Rules that make the loop hold:

1. **You may not end your turn while any ticket is `in_progress` or `review`, or while any ticket is `todo` and
   ready.** "I'll continue later", "next steps would be", or a summary before the board is empty are failures.
2. **Waiting for a subagent is not the end of your work.** If the harness lets you wait, wait; if subagents run in the
   background, review whichever returned first, then start the next ready ticket.
3. If your context gets long: make sure BOARD.md and the reports are committed, then continue. A fresh orchestrator
   started with the resume prompt (§14.10) must be able to pick up from the files alone.
4. Do not ask the user anything except at a USER GATE (§14.9) or for data only they have (T6.6 measurements). For
   everything else, choose the plan's fallback, write down the choice, continue.

### 14.5 Implementer prompt template (fill every `<...>`)

```
You are implementing ONE ticket of the Failoring crochet plan. You work alone; nobody will answer questions.
Repo: <path>. Branch: create crochet/<ticket> from crochet/main (or work in the worktree <path>).

TICKET <id>: <title>
Read first, in full: docs/CROCHET_PLAN.md sections <list>, and §1 (ground rules). Also read these source files:
<list of files the ticket touches, from the plan's "Repo facts" and the ticket's sections>.

What to build: <2-6 sentences, copied/condensed from the plan section; name the functions, files and signatures>.
Scope: you may create/modify ONLY: <file list>. Touching anything else is a rejection.

Acceptance (all must pass, with the numbers printed by the test):
<copied from the ticket table and the plan section, as concrete commands and thresholds>

Previous review findings to fix (round <n>): <findings, or "none">

Rules:
- Finish the ticket. Do not stop at a plan, a partial implementation, or "next steps". No TODO, stub, placeholder,
  "not implemented", commented-out tests or skipped assertions in your diff.
- Never change an acceptance threshold or an existing test's expected value. If you believe a threshold is wrong,
  make it pass anyway if possible; if impossible, explain with measured numbers in the report (status BLOCKED).
- Build and run: cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
  (plus the ticket's own commands). All old tests must still pass.
- Commit on your branch with message "<id>: <title>" and the trailer lines the repo uses. Then return.
- Your final message must be exactly the report block below, filled in. Nothing after it.

REPORT
ticket: <id>
status: DONE | BLOCKED
commit: <full hash>
files: <list>
commands_run: <each command, one per line>
results: <each acceptance item: PASS/FAIL with the measured number>
old_tests: <n passed / n total>
deviations: <anything that differs from the plan, and why; or "none">
open_issues: <or "none">
```

### 14.6 What counts as a valid return

The orchestrator rejects without review (send straight back) if any is true:

* no `REPORT` block, or `commit` missing, or the hash is not on the branch (`git cat-file -e <hash>`);
* `git status` in the worktree shows uncommitted changes;
* any acceptance line says FAIL while `status: DONE`;
* `BLOCKED` without measured numbers explaining why.

### 14.7 Review (the orchestrator does every step itself)

1. **Check out the commit** in a clean worktree. Build from scratch (`rm -rf build`).
2. **Re-run** every command in `commands_run` and the full ctest. Compare numbers with the report. If they differ,
   reject: "your report says X, I measured Y".
3. **Diff scope:** `git diff crochet/main...<hash> --stat`. Files outside the ticket's scope → reject (unless a tiny,
   justified build-file change).
4. **Laziness scan** on the diff: `grep -nE "TODO|FIXME|stub|placeholder|not implemented|NotImplemented|return 0; *//|#if 0|DISABLED_|skip"`.
   Any hit in new code → reject. Check that no existing test file lost assertions or had expectations changed
   (`git diff -- '*test*'` on old files must be empty except added lines).
5. **Mutation check** (for ★ tickets, done by the Verifier; for others by you on at least one test): break the feature
   on purpose (e.g. return 0 from the new function, flip a sign), run the new test, it must FAIL; then revert. A test
   that passes with the feature broken is not a test → reject.
6. **Read the code** against the plan section: right formulas (units, signs, warp = wale), right defaults (opt-in,
   old behaviour unchanged), no silent behaviour change for existing garments.
7. **Write the verdict** into `docs/crochet/reports/<ticket>.md`:

```
## Round <n> review — <date>
verdict: ACCEPT | CHANGES_REQUESTED
measured: <the numbers you got>
findings:
1. <file:line> <what is wrong> -> <what to do>   (concrete enough to act on without asking)
```

Findings go into the next round's prompt verbatim.

### 14.8 Escalation

* Round 3 fails → re-send the ticket to the **other model**, with all review findings, as round 4.
* Round 5 fails → split the ticket into two smaller tickets (write them into BOARD.md with their own acceptance), or
  take the plan's named fallback (e.g. §2.1 step 6, §6.9 barrier fallback). Write the decision into
  CROCHET_STATUS.md.
* A ticket that is impossible as specified (e.g. a test threshold that measurements show is wrong): mark `blocked`
  with the numbers, change dependents only if they can proceed without it, and raise it at the next user gate.

### 14.9 User gates (the only times to stop and wait for the user)

1. **After T6.5** (five-variant report): post the table and snapshots, ask whether the variants look and measure
   plausibly different and which "chain around the bottom" they meant (§11.3). While waiting, continue with tickets
   that do not depend on the answer (T7.1, T7.2, T8.1).
2. **T6.6:** needs the user's real-bag measurements (width, depth lying flat; sts and rows per 10 cm; strap length;
   weight; yarn; hook). Ask once at gate 1; if they are not there yet, keep T6.6 `blocked: waiting for user data`.
3. **Before merging `crochet/main` into `main`**: never merge into `main` without the user saying so.

### 14.10 Prompts to start the orchestrator

Kickoff:

```
You are the ORCHESTRATOR for the Failoring crochet mode. Repo: <path>.
Read docs/CROCHET_PLAN.md completely, starting with §14, then §1. You do not write feature code: you run the loop in
§14.4 with implementer subagents (prompt template §14.5), review every return with §14.7, and keep
docs/crochet/BOARD.md current and committed. Start with T0.1. Do not end your turn until §14.4 reaches FINISH.
Model for implementers: <model A>; for escalations: <model B>.
```

Resume (after a context reset or a new session):

```
You are the ORCHESTRATOR for the Failoring crochet mode, resuming. Repo: <path>, branch crochet/main.
Read docs/CROCHET_PLAN.md §14 and §1, then docs/crochet/BOARD.md and the latest report of every ticket that is not
done. Reconcile: for any ticket marked in_progress, check whether its branch has a commit with a REPORT; if yes,
review it; if not, re-send it. Then continue the §14.4 loop. Do not end your turn until FINISH.
```
