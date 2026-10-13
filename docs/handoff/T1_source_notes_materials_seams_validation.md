# Fabric materials, measured data, construction details and validation: ideas for Failoring (2018 to Oct 2026)

Research notes, 13 Oct 2026. Verified = I fetched the page this session. "Unverified" = from memory or search snippet only. Older foundational work is marked (older). Coverage is limited (about 30 tool calls); I did not read full PDFs, so equations and numbers beyond abstracts are not verified.

## 1. Measured fabric datasets and fitting: which approach fits "photograph a swatch at home"?

### Takeaway
The best match is the drape-silhouette family (Ju et al. CGF 2024; Gong et al. CVPR 2024), which needs only a Cusick-style top-down photo, but it constrains bending far better than stretch, so Failoring should pair it with a hanging-strip/cantilever test and treat stretch as a prior (fabric class + a simple pull test). The only freely downloadable measured datasets I could confirm are old (Wang 2011, 10 fabrics) or just announced (Fabric-101, Sep 2026); none gives KES/FAST numbers in an open licence.

### Cited Findings
- Wang, O'Brien, Ramamoorthi, "Data-driven elastic models for cloth: modeling and measurement", SIGGRAPH/ACM TOG 30(4), 2011 (older). Biaxial rig, 400x400 mm samples at 0/45/90 deg bias, 105 tests per material; bending from a 4 cm strip drooping under its own weight, photographed from the side. Stretch is an orthotropic 4-parameter stiffness matrix tabulated over principal strain and strain angle (24 params); bending is piecewise-linear curvature-force at 5 points x 3 bias angles (15 params). 10 fabrics (e.g. 11oz black denim, interlock, rib knit). Fit by BFGS to photographed shapes; residuals 1-3 mm. Authors say linear isotropic is not sufficient; model ignores plasticity and edge curl. Raw measurements and fits are said to be on a Berkeley site; no licence stated. — [paper PDF](https://escholarship.org/content/qt7g35q2x7/qt7g35q2x7.pdf)
- Miguel et al., "Data-Driven Estimation of Cloth Simulation Models", Computer Graphics Forum 31, 2012 (older). Records 3D deformation of a sheet rather than 1D force-displacement curves; validated on motions different from the fitting ones. Data availability not stated on the page. — [Eurographics DL](https://diglib.eg.org/items/47cce290-0a8f-4daf-8e2a-1ff6a2c23d2b/full)
- Clyde, Teran, Tamstorf, "Modeling and Data-Driven Parameter Estimation for Woven Fabrics", SCA 2017 (older). New orthotropic hyperelastic model for wovens; protocol uses commercially available tests; inverse problem on Kirchhoff-Love subdivision FEM; works on plain triangle meshes; 5 fabrics. Data/code availability not stated on the page. — [Eurographics DL](https://diglib.eg.org/items/2c5664f7-8797-4691-a3a9-a6559aad207a/full)
- Ju et al., "Estimating Cloth Simulation Parameters From Tag Information and Cusick Drape Test", Computer Graphics Forum 2024. Input is only an ISO Cusick drape silhouette plus tag info (fabric type, fibre composition). Two-stage neural cascade: stretch parameters from tag info, then bending from drape plus those stretch values. Explicitly because prior silhouette methods estimate stretch poorly. — [Eurographics DL](https://diglib.eg.org/handle/10.1111/cgf15027)
- Gong, Mao, Wang, "Bayesian Differentiable Physics for Cloth Digitalization", CVPR 2024. One silhouette image per sample, differentiable implicit-Euler sim with per-element stretch (C) and bending (B) matrices depending on bias angle, DIR-B silhouette renderer, variational posterior over stiffness (gives uncertainty). Dataset of 25 fabric types, 660 drape images and meshes said to be released; I could not confirm availability. Code: github.com/realcrane/Bayesian-Differentiable-Physics-for-Cloth-Digitalization. — [CVPR paper](https://openaccess.thecvf.com/content/CVPR2024/papers/Gong_Bayesian_Differentiable_Physics_for_Cloth_Digitalization_CVPR_2024_paper.pdf), summary via [arXiv 2402.17664](https://arxiv.org/html/2402.17664v1)
- Choi group, "Fast Constrained Optimization for Cloth Simulation Parameters from Static Drapes", Computer Animation and Virtual Worlds 35(3), 2024: neural surrogate replaces the sim in the optimisation loop so the user can re-optimise under range constraints. No code mentioned. — [project page](https://cg.catholic.ac.kr/~mgchoi/publications/FastOpt/)
- Zhang, Lin, Bode, Chen, Stuyck, Larionov, "Estimating Cloth Elasticity Parameters From Homogenized Yarn-Level Models", arXiv 2401.15169, Jan 2024 (venue not given on page). Derives yarn model from info on the fabric, homogenises to shell hyperelastic energies with uncoupled membrane and bending, validates with stretch tests and drapes. No code/data link. — [arXiv](https://arxiv.org/abs/2401.15169)
- Ru et al., "Can Real-to-Sim Approaches Capture Dynamic Fabric Behavior for Robotic Fabric Manipulation?", arXiv 2503.16310, 2025. Compares DiffCloud, DiffCP, PhysNet and a PINN on 5 fabrics (45x45 cm). Finding: result depends strongly on simulator and training scenario; no ground-truth parameters exist for real fabrics, so accuracy is hard to define; quasi-static fits do not transfer well to fling/shake. — [arXiv](https://arxiv.org/html/2503.16310v1)
- Can et al. (Stanford/Google), "Image2Garment: Simulation-ready Garment Generation from a Single Image", arXiv 2601.09658 (2026). Vision-language model predicts fibre composition, knit/woven, areal density, thickness; a light predictor maps those to bending/shear/stretch/buckling stiffness. Two new datasets (FTAG, T2P); no code link seen. — [arXiv](https://arxiv.org/pdf/2601.09658v4)
- Toshikj and Mladenovikj, "Smartphone-Based Digital Image Processing for Fabric Drape Assessment", Textiles 5(4), Dec 2025. Phone photo at 22/32/42 cm height of a Cusick-meter shadow, ImageJ segmentation, drape coefficient = shadow area / ring area x 100. Statistically indistinguishable from the Cusick method (p > 0.05); 22 cm most stable; controlled daylight about 980 lux; still used a commercial Cusick meter, so a home rig was not tested. — [MDPI](https://www.mdpi.com/2673-7248/5/4/63)
- Gong et al., "Learnable Persistent Wrinkle Formation in Cloth Simulation", arXiv 2609.13707, 12 Sep 2026. Introduces Fabric-101: 101+ fabrics measured with cyclic loading/unloading, separating elastic, friction-driven and plastic parts. Code/data: github.com/GongDeshan/Fabric_101_for_Wrinkles (licence not checked). — [arXiv](https://arxiv.org/abs/2609.13707)
- Sperl, Narain, Wojtan, "Homogenized Yarn-Level Cloth", ACM TOG 39(4), SIGGRAPH 2020. Data and fitting code (zip), ARCSim-HYLC and pattern-optimisation repos at git.ist.ac.at/gsperl; "data-driven models of several woven and knitted fabrics" for an off-the-shelf solver. Licence not stated. Sperl's thesis (ISTA 2022/23) compiles a database from physical tests of industrial knits and fits yarn parameters. — [project page](https://visualcomputing.ist.ac.at/publications/2020/HYLC/), [thesis record](https://research-explorer.ista.ac.at/record/12358)
- Real-to-sim differentiable cloth: DiffCloth (Li, Du, Wu, Aanjaneya, Bargteil? author list unverified), "DiffCloth: Differentiable Cloth Simulation with Dry Frictional Contact", ACM TOG 2022. — [arXiv 2106.05306](https://arxiv.org/pdf/2106.05306). Gong/Wang group also has "Differentiable Cloth Parameter Identification and State Estimation in Manipulation" — [arXiv 2311.05141](https://arxiv.org/pdf/2311.05141) (details not read).
- Zhang et al., SIGGRAPH 2025 ("PIE" page): this paper does NOT estimate B/E from data. It estimates the optimal mesh resolution without trial simulations, from material stiffness and boundary conditions (shirring, folds, stitches, down-fill, collision compression), applying Cerda-Mahadevan wrinkle-wavelength scaling and Vandeparre et al. 2011 for transition distances, then builds a sizing map (Poisson sampling plus Delaunay). The project page gives no formula tying E/B to resolution and links no code. — [project page](https://wanghmin.github.io/publication/zhang-2025-pie/) (the PDF itself was not read).
- Style3D/CLO libraries: no open, documented structure found; CLO support threads exist but are community posts. Treat as closed. (searched, nothing citable)

### Inferences
Idea 1. Photographed-shadow drape coefficient as an acceptance test and one fitting constraint.
- Outline: Cut a 30 cm disc (24 or 36 cm also in the study), lay on an 18 cm support, photograph vertically from fixed height under diffuse light, threshold in ImageJ or a small C++/stb tool, drape coefficient = shadow area / (annulus area) x 100. Run the same geometry in Failoring and compare DC plus the number of nodes (folds) and lobe amplitude. Use as a go/no-go gate per preset and to calibrate one scalar (a bending multiplier).
- Changes: new test harness; adds a "reality gap" column to fabrics.json; enables the shadow-mask loss.
- Payoff: high. Difficulty: days. Risks: DC is insensitive to anisotropy and stretch; self-weight/areal density must be measured (weigh the disc); fold count is noisy (use 3 specimens and report spread). Also a shadow is not a pure silhouette if the paper tilts.
- Sources: Toshikj 2025 (above); Cusick drape standard (BS 5058 / ISO test; standard text not read, unverified).
- Data/licence: none needed; self-generated.

Idea 2. Silhouette-driven optimiser for bending only, with stretch from a prior, plus a posterior width.
- Outline: Fit 2-3 parameters (Bu, Bv, a global stretch scale) by minimising silhouette IoU between photo and sim, using derivative-free search (Nelder-Mead / CMA-ES) over the existing fast core instead of a differentiable renderer. Initialise from a fabric-class prior (tag info, as in Ju 2024). Repeat with perturbed initial states and report parameter spread as an error bar, echoing the Bayesian approach.
- Changes: new "fit" tool over cloth_core; fabrics.json gains `source: measured` and `uncertainty` fields.
- Payoff: high. Difficulty: weeks. Risks: non-uniqueness (a silhouette fixes the stiffness/weight ratio, not each separately); stretch weakly identified (Ju 2024 says so explicitly); the Failoring mesh-dependent fold pattern may make the objective jagged. Ru 2025 shows fits transfer poorly from quasi-static to dynamic.
- Sources: Ju 2024; Gong 2024; Choi 2024; Ru 2025.
- Data/licence: generate your own; the Gong dataset (if released) could validate the optimiser blind (check licence).

Idea 3. Bias-direction measurements with a hanging strip, giving an orthotropic bending fit directly.
- Outline: Following Wang 2011, cut 4 cm strips at 0/45/90 deg, push over a table edge, photograph side view, extract the centreline (ImageJ or manual points) and fit Bu, Bv and the 45 deg value, which pins the mixing-compliance cross term. A free 45 deg value tests whether the Failoring cos^4/sin^4 interpolation is adequate for each fabric.
- Changes: bending calibrate tool; likely adds a per-preset bias-45 correction factor if the formula misses.
- Payoff: high for wovens. Difficulty: days. Risks: Wang fits a nonlinear curvature-force curve, Failoring is linear, so large-curvature strips will mismatch; moisture/ironing/ageing history changes strips.
- Sources: Wang 2011.
- Data/licence: Wang 2011 database as a cross-check (licence unknown).

Idea 4. Use published measured data to seed presets, but only as priors.
- Outline: Pull per-fabric values from Wang 2011 (10 fabrics incl. denim) and Fabric-101, Sperl's industrial knit database; map to N/m and N·m with the same conventions as fabrics.json and replace guessed values where a type matches.
- Changes: `fabrics.json` provenance; at least denim, jersey (interlock/rib) rows get measured anchors.
- Payoff: med. Difficulty: weeks (unit conversion, mapping nonlinear curves to linear moduli at a chosen strain). Risks: licences unclear; Fabric-101 is brand new and I did not read the data format.

Idea 5 (low priority). Tag-information prior (fibre %, weave/knit, areal density, thickness) following Ju 2024 and Image2Garment 2026, as a table lookup rather than a neural net, to avoid asking the user for stretch.
- Payoff: low-med. Difficulty: days. Risks: tag info is weakly predictive.

### Gaps
- Licences: Wang 2011 database, Fabric-101, Gong's 25-fabric dataset and Sperl's database licences were not confirmed.
- Whether DiffCloth-style differentiable code can be used without a differentiable renderer for photographed shadows was not tested.
- No peer-reviewed home protocol with error bars for a hand-built cantilever/Cusick rig was found; only Toshikj 2025 (commercial Cusick meter).
- Zhang 2025 PDF not read; whether its resolution rule has an explicit B or E dependence is not confirmed.

## 2. Nonlinear and anisotropic membrane/bending, plasticity and creases

### Takeaway
Measured data show real cloth is nonlinear and anisotropic, so the linear warp/weft/shear membrane is the main fidelity cap beyond drape; recent work (Clyde 2017, Zhang 2024) shows a small-parameter orthotropic hyperelastic model is enough. Persistent creases are now a well-defined module (Gong CGF 2025, arXiv 2026) with open code.

### Cited Findings
- Wang 2011 concluded the linear isotropic model is insufficient; the model ignores memory (plasticity) and edge curling. — [paper](https://escholarship.org/content/qt7g35q2x7/qt7g35q2x7.pdf)
- Clyde 2017: orthotropic hyperelastic woven model with few parameters that matches fully nonlinear measured data and works on plain triangle meshes. — [Eurographics DL](https://diglib.eg.org/items/2c5664f7-8797-4691-a3a9-a6559aad207a/full)
- Zhang et al. 2024 (arXiv 2401.15169): shell model with uncoupled membrane and bending reproduces nonlinear anisotropic behaviour after homogenisation, including diagonal bending. — [arXiv](https://arxiv.org/abs/2401.15169)
- Wong, Leach, Zambetta, "Modelling Bending Behaviour in Cloth Simulation Using Hysteresis", CGF 2013 (older): approximates an ideal hysteresis loop for retained curvature and creases, adds fatigue weakening under repeated bending, small cost. — [Eurographics DL](https://diglib.eg.org/handle/10.1111/v32i8pp183-194)
- Gong, Yang, Shao, Wang, "Cloth Animation with Time-dependent Persistent Wrinkles", CGF (Eurographics) 2025. Bending strain split into elastic and plastic parts with yield strain that hardens with dwell time, plus a stick-slip friction anchor; about eight added parameters; code at github.com/realcrane/Cloth-Animation-with-Time-dependent-Persistent-Wrinkles. — [arXiv 2502.13491](https://arxiv.org/html/2502.13491v1)
- Gong et al. 2026 (arXiv 2609.13707) fits elasto-plastic plus friction parameters from Fabric-101 cyclic data with an adjoint method. — [arXiv](https://arxiv.org/abs/2609.13707)
- Bolt (NVIDIA, arXiv 2504.17614, 2025): panel-based anisotropic stretch (credited to Kim 2020, unverified) and bending from a shape operator in warp/weft/shear directions; a practical reference for production-style directional models. — [arXiv](https://arxiv.org/pdf/2504.17614)
- Tension-field/wrinkling search returned only mechanics/aerospace literature (e.g. Samper et al.); no cloth-specific citation verified. Failoring's reduced compression stiffness is the standard shell-sim substitute; I did not verify a cloth paper for it.

### Inferences
Idea 6. Nonlinear (J-curve) uniaxial stress-strain via a strain-dependent stiffness table.
- Outline: Replace each of the three linear stiffnesses by a piecewise-linear secant/tangent stiffness k(strain) in warp, weft and shear, with a soft toe region then a stiff region (crimp straightening) and a shear lock angle. Keep it per-triangle and energy-consistent by integrating the table (so it stays conservative). Parameters from a hanging-weight test of strips (4 weights is enough for a toe and slope).
- Changes: membrane energy function in cloth_core and implicit Hessian (use tangent stiffness, clamp positive); new fields in fabrics.json.
- Payoff: high for jersey, leather, denim; med for wovens. Difficulty: weeks. Risks: implicit solver convergence with changing stiffness; sparse Cholesky matrix needs positive-definite tangent, hence clamping; fitting from few points is under-determined. Unvalidated for coupling between warp and weft (Poisson effects are ignored in the current decoupled model; Wang 2011 reports interlock is clearly anisotropic and rib knit has minimal Poisson effects).
- Sources: Wang 2011; Clyde 2017; Zhang 2024.

Idea 7. Bending plasticity (persistent creases) as an extra hinge state.
- Outline: Add per-hinge plastic rest angle. Hinge angle = elastic + plastic; when |elastic strain| exceeds a yield threshold, plastic rest angle moves toward the current angle (return mapping). Add a dwell-time hardening option from Gong 2025 later. For pressed creases, expose an "iron" tool that sets the plastic rest angle on selected hinges to a target.
- Changes: per-edge state vector; reset/serialize; a pattern-level `crease` attribute for pressed pleats, hems, collars.
- Payoff: high for tailoring (pressed pleats, hems). Difficulty: weeks. Risks: mesh-dependent crease width; energy not conservative (fine, but test damping); parameters (yield strain) need a crease-recovery test (fold, load, release, measure angle) which I did not find as a standard home protocol (textile "crease recovery angle" tests exist, not verified here).
- Sources: Wong 2013; Gong 2025; Gong 2026 (Fabric-101).
- Data/licence: Gong code and Fabric-101 on GitHub; check licences before copying code. Implement from the papers.

Idea 8 (watch item). Stronger orthotropic bending than 1:5.
- Outline: Test the mixing-compliance formula against Wang-style 0/45/90 strips at extreme anisotropy (Idea 3). If it fails, replace the isotropic-by-hinge model with a shape-operator (curvature tensor) bending energy that supports a full 2x2 stiffness in the principal-direction basis, as in Bolt's shape-operator approach. No evidence yet whether hinge error is large for real wovens; run the experiment before the rewrite.
- Payoff: med. Difficulty: weeks-months. Risks: shape-operator energy in a triangle mesh is more complex and mesh-sensitive.
- Sources: Bolt 2025.

### Gaps
- Stretch-dependent bending and tension-field theory for cloth: no direct cloth-graphics source verified.
- No citable discussion of Cosserat shells for cloth found.
- Did not find the original Kim 2020 anisotropic stretch paper to verify.

## 3. Seam and construction fidelity

### Takeaway
There is exactly one clearly relevant paper treating seams as geometry: Rodriguez and Cirio, "True seams" (SIGGRAPH 2022); others (Bolt 2025) treat seams as vertex pairs. A tailor-first tool can get most of the benefit with cheap parametric tricks (stiffness strips, thickness offsets, layer-aware collision).

### Cited Findings
- Rodriguez and Cirio, "True seams: modeling seams in digital garments", ACM SIGGRAPH 2022 technical paper (author given by surname only on the page). Overlaps, folds and stitches per real seam type, solved as 2D constrained optimisation problems lifted to 3D; claims better garment appearance. — [SIGGRAPH history archive](https://history.siggraph.org/?p=117836). A patent exists with a similar title ("Modeling true seams in virtual garment applications", Justia) — [patent page](https://patents.justia.com/patent/20230334772): check freedom to operate before copying the method (I did not read claims).
- Bolt (NVIDIA 2025): seams are lists of vertex pairs, re-synced each step by averaging position/velocity; collisions skip triangles connected through seams; a spring-based seam bending term compares dihedral angle across a seam to its rest angle; layered garments simulated progressively bottom-up with lower layers frozen into an SDF; thickness enters only via collision offsets. — [arXiv](https://arxiv.org/pdf/2504.17614)
- Zhang et al. SIGGRAPH 2025: lists shirring, folding, stitching and down-filling as stationary boundary conditions that determine required mesh resolution. — [project page](https://wanghmin.github.io/publication/zhang-2025-pie/)
- CLO community threads confirm users hit layer overlap/collision problems (anecdotal). — [CLO support](https://support.clo3d.com/hc/es/community/posts/360033350674-Material-overlap)
- No verified cloth-sim papers found for: topstitch, flat-felled/French seams, button plackets, fusible interfacing as composite laminate, ribbing/elastic, bias-cut as a special case.

### Inferences
Idea 9. Seam as a stiffened strip with allowance (parametric seam types).
- Outline: For each sewn edge pair, mark triangles within the seam allowance width as a "seam region" with multiplied bending stiffness (extra layers: 2x for plain, 3-4x for flat-felled, higher for topstitched), added areal density, and a collision/rest thickness offset. Apply the allowance fold direction (press open vs to one side) as a plastic rest angle (links to Idea 7). Keeps your spring seam for connectivity.
- Changes: pattern format gets `seam_type`, allowance width; mesher must align mesh with the allowance line; collision thickness per region.
- Payoff: med-high (visible stiff seam lines, correct hem hang). Difficulty: weeks. Risks: mesh conforming to the allowance line (you have mesh_sizing, so feasible); stiffness multipliers are guesses until measured (cantilever on a seamed strip is a clean test: cut strip across a seam and compare bending length).
- Sources: Rodriguez and Cirio 2022 (concept); Bolt 2025 (seam bending term).

Idea 10. Multi-layer laminates via effective stiffness (interfacing, hems, waistbands, collars).
- Outline: Treat two or more bonded layers as one shell with composite properties: stretch adds, bending via parallel-axis-like sum. An interlayer-spaced estimate is B_total ≈ sum(B_i) + sum(E_i·t_i·d_i^2) for a bonded stack and approaches sum(B_i) for unbonded layers. Provide "fused" and "free" modes. This is standard laminate mechanics; I have not verified a cloth-specific paper for it.
- Changes: new layer-stack material builder; local regions on a pattern piece get a different material.
- Payoff: high for tailoring (collars, cuffs, waistbands, hems). Difficulty: days to weeks. Risks: real fusible interfacing is partly bonded, so measure by cantilever of a fused swatch and calibrate the bond efficiency.
- Sources: laminate mechanics (textbook, unverified); Bolt 2025 for layered collision ordering.

Idea 11. Hem as folded region.
- Outline: Hem = a two-layer strip (Idea 10) with a stitch line and a plastic crease at the fold (Idea 7), a cheap way to get proper hem weight and swing. Payoff med, difficulty days after 7 and 10.

Idea 12. Gathers and shirring as boundary conditions plus resolution.
- Outline: Gathers already work through unequal edge length; the issue is mesh resolution. Use a Cerda-Mahadevan-style wavelength estimate (as Zhang 2025 does) to set mesh_sizing near gathers, rather than a user guess. Payoff med, difficulty days-weeks. Risk: I could not read the PDF to confirm the formula; derive from first principles and test with your own cantilever/drape.
- Sources: Zhang 2025; Cerda-Mahadevan 2003 (cited by Zhang, not read).

### Gaps
- No open dataset of measured seam stiffness.
- Whether "True seams" released code: unknown.
- Buttons/plackets, ribbing, elastic: no relevant paper verified.

## 4. Knit-based fabric models at shell level

### Takeaway
For jersey/rib, the credible route is homogenisation: fit a shell energy to yarn-level simulations, which gives strongly nonlinear, anisotropic response (Sperl 2020; Zhang 2024). Failoring's linear membrane cannot reproduce it, but Idea 6 (tabulated nonlinear stretch with a very soft toe) is a pragmatic stand-in.

### Cited Findings
- Sperl, Narain, Wojtan, ACM TOG 2020: thin-shell solver driven by an energy density fitted to many periodic yarn simulations, for woven and knit; reproduces high deformability and anisotropy of knits; data, fitting code and ARCSim-HYLC are available from ISTA git. — [project page](https://visualcomputing.ist.ac.at/publications/2020/HYLC/)
- Sperl thesis: also adds mechanics-aware yarn detail onto shell sims (loops tighten under stretch) and a database from physical tests of industrial knits; says yarn interactions cause "anisotropy, curling, and more" but the abstract does not say how curl is modelled. — [thesis record](https://research-explorer.ista.ac.at/record/12358)
- Wang 2011 database includes interlock, rib knit and a sweatshirt-type knit with obvious anisotropy; notes edge curling is not modelled. — [paper](https://escholarship.org/content/qt7g35q2x7/qt7g35q2x7.pdf)
- Zhang 2024 estimates shell elasticity from homogenised yarn models. — [arXiv](https://arxiv.org/abs/2401.15169)

### Inferences
Idea 13. Fit a knit preset from a small number of uniaxial tests, in the spirit of homogenisation but without yarn sim.
- Outline: Hang three weights on a 5 cm strip for course and wale, record elongation by photo with a ruler, fit the toe/slope table of Idea 6; shear via a 45 deg strip. Record Poisson contraction by marking width. Use Sperl's published knit database as sanity targets for the shape of the curve.
- Changes: knit preset type with high-strain table; lower compression cut-off.
- Payoff: med-high for jersey and crochet-adjacent work. Difficulty: weeks. Risks: knits show hysteresis and creep; weights on a strip neck in (Poisson) so a uniaxial strip is not the same as in-plane strain in a garment.

Idea 14 (research-grade, low payoff now). Edge curl: add a pre-strain/residual bending rest curvature at free edges of stockinette. Not found as a ready shell technique; mark speculative. Difficulty: months.

### Gaps
- No verified shell-level method for stockinette curl.
- Licences for ISTA data not stated.
- I did not verify the "Estimation of yarn-level simulation models for production fabrics" paper's details (listed on ISTA thesis page).

## 5. Validation protocols and how tests map to the solver

### Takeaway
Cusick drape (shadow area) is cheap, repeatable and compatible with a phone; cantilever/hanging strip maps directly to the bending parameters; a pull test maps to stretch. Error bars should be reported from repeated specimens and from fit posteriors.

### Cited Findings
- Phone-photo drape coefficient matched Cusick method statistically; 22 cm camera height most stable; controlled lighting; tested with commercial meter. — [Toshikj 2025](https://www.mdpi.com/2673-7248/5/4/63)
- ISO 9073-7 (1995) covers nonwoven bending length (cantilever); preview and catalogue exist (the standard text itself not read). — [ANSI preview PDF](https://webstore.ansi.org/preview-pages/ISO/preview_ISO+9073-7-1995.pdf)
- Wang 2011 strip bending and biaxial stretch measurement protocol, with 1-5 mm feature error against the simulator. — [paper](https://escholarship.org/content/qt7g35q2x7/qt7g35q2x7.pdf)
- Gong 2024 and Ju 2024 use the ISO Cusick drape test as the only measurement; Gong reports a posterior (mean and variance) instead of a point estimate. — [CVPR](https://openaccess.thecvf.com/content/CVPR2024/papers/Gong_Bayesian_Differentiable_Physics_for_Cloth_Digitalization_CVPR_2024_paper.pdf), [CGF](https://diglib.eg.org/handle/10.1111/cgf15027)
- KES versus FAST comparisons exist (shirting study at PolyU), showing instruments do not give identical numbers for the same cloth. — [PolyU record](https://research.polyu.edu.hk/en/publications/comparison-of-mechanical-properties-of-shirting-materials-measure/) (content not read, only the title)
- Ru 2025 stresses that real fabrics have no ground-truth parameters, so only held-out behaviour (a different test) is a valid check. — [arXiv](https://arxiv.org/html/2503.16310v1)

### Inferences
Idea 15. A three-test protocol with a held-out check.
- Outline: Fit on (a) hanging strip/cantilever bend length (bending, 3 bias angles), (b) a hanging-weight strip (stretch table), then validate on (c) the Cusick/phone drape coefficient and fold count, never fitted. Report: bend length mean ± standard deviation across 3 specimens, DC ± SD, sim DC; accept if sim DC within the specimen spread. Cantilever bending length c relates to bending rigidity G = w·c^3 (standard Peirce relation; I did not re-verify the exact form against ISO 9073-7), so Bu, Bv map directly, but Failoring's B must be compared at the same mesh and curvature level, since Failoring's calibration vs beam theory already handles this.
- Changes: tests/ regression suite with real numbers per preset; "validated" flag in fabrics.json.
- Payoff: high. Difficulty: days. Risks: humidity and handling; edge fray; sample cutting direction error (bias angle).

Idea 16. Mesh-resolution convergence study per preset, using Zhang 2025's idea.
- Outline: For each preset, run drape at 3 resolutions and report DC drift. Pick the sizing map accordingly; this is the only fair way to separate mesh error from material error before any measured comparison.
- Payoff: med. Difficulty: days.

### Gaps
- No verified standard text for Cusick (BS 5058) or KES/FAST mappings read; mappings above come from general textile knowledge and should be checked.
- No peer-reviewed home-rig uncertainty numbers.

## Ranking (for the report writer)
1. Idea 1 + 15 (shadow DC and three-test protocol): high payoff, days.
2. Idea 3 (bias strips for orthotropic bending) and 8 (check the hinge model's limit).
3. Idea 7 (plastic creases) and 10 (laminates/interfacing): tailoring-specific, weeks.
4. Idea 6/13 (J-curve stretch, knit presets): high but invasive.
5. Idea 9, 11, 12 (seams, hems, gathers) built on 7 and 10.
6. Idea 2 (silhouette optimiser with uncertainty) once 1 and 3 exist.
