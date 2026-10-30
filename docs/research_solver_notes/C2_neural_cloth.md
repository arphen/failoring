# Neural / learned cloth and garment simulation as a live preview in front of a physical solver

Scope note: research was budget-limited (about 20 tool calls). Only sources actually fetched or returned by search are cited. Items I could not verify are in Gaps. Effort sizes (S/M/L/XL) assume a small team with a C++ FEM cloth core and are my inferences.

## Self-supervised physics-based nets (HOOD, SNUG, PBNS, ContourCraft, MGN, 2026 follow-ups): what they do and cannot do

### Takeaway
The self-supervised line (PBNS, SNUG, HOOD, ContourCraft, then PhySkin/HyperBones in 2026) trains garment networks with physics energies as the loss and no simulated ground truth. They are fast and need no data generation. They are built for body-driven dynamics (worn garment following motion), not for gathers, ruffles, seams pulled together, or friction, and nothing found reports Apple-Silicon numbers. HOOD is the only one whose properties (rest-geometry input, per-edge material parameters, resizing) fit arbitrary user patterns.

### Cited Findings
- **HOOD** (Grigorev, Thomaszewski, Black et al., CVPR 2023, arXiv 2212.07242). Hierarchical GNN, trained self-supervised with a physics loss on 52 AMASS sequences. Samples SMPL shapes U(-2,2), six garment templates, size perturbation U(0.9,1.1), log-uniform material parameters. Runs 7.27 fps on a ~12K-vertex dress (13.6 fps in the all-garments comparison); hardware not specified in the paper text I read. Rest-mesh edge lengths are an input, so it generalises to unseen garments, resizing, topology edits (buttons/zippers as edge toggles) and per-region materials. Limits: no garment-garment interaction, degrades beyond training velocities, fails on severely self-intersecting bodies. Training starts from skinned, non-energy-optimal garments and a scaled-down inertia term lets them relax. Too few message-passing steps gives over-stretched, rubbery cloth. — [alphaXiv summary of arXiv 2212.07242](https://www.alphaxiv.org/abs/2212.07242.md); [CVPR paper PDF](https://openaccess.thecvf.com/content/CVPR2023/papers/Grigorev_HOOD_Hierarchical_Graphs_for_Generalized_Modelling_of_Clothing_Dynamics_CVPR_2023_paper.pdf)
- HOOD code: MIT licence on GitHub. Needs conda, Python 3.9, PyTorch with CUDA 11.7, PyG, PyTorch3D, and SMPL files downloaded separately from the SMPL site (SMPL licence terms not stated on the page I read). Weights listed: cvpr_submission.pth, postcvpr.pth, fine15.pth, fine48.pth. The paper text says "available for research purposes", so a licence mismatch with the repo's MIT label is possible; verify before commercial use. — [HOOD GitHub](https://github.com/Dolorousrtur/HOOD)
- **ContourCraft** (Grigorev, Becherini, Black, Hilliges, Thomaszewski, SIGGRAPH 2024, arXiv 2405.09522). HOOD plus an Intersection-Contour loss and a repulsion loss, for multi-layer outfits. RTX 4090, dt = 1/30 s: 12,030 nodes at 10.5 fps, 17,745 at 9.3 fps, 36,515 nodes (5 garments) at 3.7 fps. About 150 intersecting triangle pairs per frame vs about 8,796 for the HOOD baseline. In a perceptual study CLO3D scored 4.29 and ContourCraft 4.15 (1-5), and participants preferred CLO3D head-to-head. Fails if layer order cannot be inferred from the initial geometry; small pieces such as pockets can pop out. — [arXiv HTML 2405.09522v2](https://arxiv.org/html/2405.09522v2)
- ContourCraft source: the arXiv text I read said the URL would be disclosed upon acceptance. The ChatGarment repo states it uses "ContourCraft-CG", a refined ContourCraft, so a code release exists in some form; I did not locate or check its licence. — [ChatGarment GitHub](https://github.com/biansy000/ChatGarment)
- **SNUG** (Santesteban, Otaduy, Casas, CVPR 2022, arXiv 2204.02219). Uses the backward-Euler objective (inertia plus strain, bending, gravity, collision) as the training loss. A GRU predicts per-vertex offsets on a skinned template. Reported ~2 h training, 2.2 ms inference, 19 MB model. Converges to simpler solutions than a real simulator, needs post-processing for collisions, no self-collision. It is per-garment-template, with no pattern or fabric generalisation in the paper. — [arXiv 2204.02219](https://arxiv.org/abs/2204.02219v1)
- **PBNS** (Bertiche, Madadi, Escalera, 2020/2022; arXiv 2012.11310) and **Neural Cloth Simulation** (Bertiche, Madadi, Escalera, SIGGRAPH Asia 2022) were located by search only; I did not fetch them, so I give no speed or licence numbers. — [arXiv 2012.11310](https://arxiv.org/abs/2012.11310v3); [SIGGRAPH history entry](https://history.siggraph.org/?p=215698)
- **MeshGraphNets** (Pfaff et al., ICLR 2021, arXiv 2010.03409) located by search only; not fetched. — [ICLR page](https://iclr.cc/virtual/2021/poster/2837)
- **PhySkin** (Srivastava, Chen, Goldade, Herholz, Jiang, Lin, Yang, Sarafianos, Stuyck, Larionov; arXiv 2603.27013, Mar 2026). Reduced-space, quasi-static, self-supervised bone-handle model. Claims microseconds per frame on single-thread CPU, zero-shot and mesh-topology independence. No code link on the page; quantitative benchmarks not shown on the page I read. — [HF papers page](https://huggingface.co/papers/2603.27013)
- **HyperBones** (Srivastava, Sharma et al., arXiv 2605.20460, May 2026). Coarse bone-net plus a UV-space CNN for wrinkles, conditioned by FiLM on a per-body-garment shape code; a HOOD-style MeshGraphNet is a training-time teacher only. 1.05-1.31 ms per frame (A6000), ~30x faster than HOOD; 4.8-5.5 ms on an RTX 3060; mostly independent of mesh resolution. One model for six garments, unseen SMPL shapes. No self-collision, no multilayer, new garments not yet automatic, no code/weights statement. — [alphaXiv summary](https://www.alphaxiv.org/abs/2605.20460.md)
- **FNOpt** (Chen, Tran, Parashar; arXiv 2512.05762, Dec 2025). Self-supervised Fourier-neural-operator "neural optimiser" for time integration, trained on a coarse grid, claimed to transfer to finer meshes without retraining. Abstract only; no runtimes given. — [arXiv 2512.05762](https://arxiv.org/abs/2512.05762)
- **NeuralClothSim** (Kairanda, Habermann, Theobalt, Golyanik; arXiv 2308.12970, v3 Nov 2024). Quasi-static thin-shell (Kirchhoff-Love) neural deformation fields; queryable at arbitrary resolution; supports material interpolation. Speed and hardware not on the abstract page. Quasi-static and resolution-independent is the closest match to a "settled drape" preview, but it is optimised per scene (my reading of "stored in network weights"). — [arXiv 2308.12970](https://arxiv.org/abs/2308.12970v3)

### Inferences
- All of the above learn body-driven (skinned) garments; the output is an offset or dynamics from a body-attached initialisation. User-edited flat patterns with gathers, ruffles and seams that pull panels from far apart are out of their training distribution (inferred; none of the papers tests it).
- Gathers and ~1.5 cm triangle detail: HOOD-class meshes are ~12K-36K nodes; ruffle detail at 1.5 cm over a full garment would be far more nodes than any reported. Nothing located shows neural nets resolving gathers. Friction is explicitly missing in GDSR (below) and is not modelled in the HOOD family either.
- "Never comes to rest" (failure 1): HOOD/SNUG are trained for motion, not for convergence to a static equilibrium. The quasi-static models (NeuralClothSim, PhySkin) are the relevant ones, but they are not shown on arbitrary patterns.
- Apple Silicon: HOOD depends on PyG and PyTorch3D and the repo pins CUDA 11.7. Porting to MPS is plausible but I found no report of it (unverified).

### Gaps
- No Apple MPS/MLX/CoreML timings for any of these methods were found.
- ContourCraft code location and licence not confirmed; PBNS/Bertiche/MGN details not fetched.
- HOOD vs SMPL licence interplay (SMPL is research-restricted in general knowledge, not verified in a fetched source here).

## Garment-pattern-to-3D learning and datasets (GarmentCode, GarmentCodeData, NeuralTailor, Sewformer, AIpparel, ChatGarment, Design2GarmentCode, DressCode)

### Takeaway
GarmentCode/pygarment (MIT) is the best-fitting open pattern DSL, and GarmentCodeData supplies 115K patterns with XPBD-simulated drapes, but its drapes are fixed-stiffness, three-softness, and generated by a GPU XPBD simulator with a ~30 s per-garment cost, so it is a weak teacher for a high-fidelity FEM product. LLM pattern generators (AIpparel, ChatGarment, Design2GarmentCode) produce patterns, not drapes, and are relevant to the LLM-agent editing story rather than to simulation speed.

### Cited Findings
- **GarmentCode** (Korosteleva, Sorkine-Hornung, SIGGRAPH Asia 2023); pygarment package is MIT, Copyright 2024 Maria Korosteleva; v2.0.0 implements GarmentCodeData; repository github.com/maria-korosteleva/GarmentCode; body-measurement presets via mbotsch/GarmentMeasurements. — [PyPI pygarment 2.0.0](https://pypi.org/project/pygarment/2.0.0); [SIGGRAPH Asia listing](https://asia.siggraph.org/2023/index.html%3Fpost_type=page&p=14494&id=papers_393&sess=sess209.html)
- **GarmentCodeData** (Korosteleva, Kesdogan, Kemper, Wenninger, Koller, Zhang, Botsch, Sorkine-Hornung, ECCV 2024, arXiv 2405.17609). 115,000 designs, each fitted to a random body and to a neutral body, with sewing pattern and 3D drape; 5,000 sampled body shapes from a custom PCA model on CAESAR (European subset, 1,700 scans). Simulator: NVIDIA Warp GPU XPBD with added self-collision prevention. ~30 s per garment on an RTX 3090 (~44 h per 5,000 drapes), capped at 5 min / 2,400 frames. Stiffness fixed to minimise stretching; softness varied via bending in three materials. The paper does not state a dataset licence (check the ETH research-collection record). — [arXiv 2405.17609](https://arxiv.org/pdf/2405.17609); [ETH Research Collection](https://www.research-collection.ethz.ch/entities/researchdata/d9df8fe3-2add-49a2-bb58-21750ca7970c)
- **NeuralTailor** (Korosteleva, Lee; 2022, arXiv 2201.13063) reconstructs sewing pattern structure from garment point clouds; code at github.com/maria-korosteleva/Garment-Pattern-Estimation (licence not checked). **Sewformer** was not located directly in these searches. — [arXiv 2201.13063](https://arxiv.org/abs/2201.13063v2); [GitHub](https://github.com/maria-korosteleva/Garment-Pattern-Estimation)
- **AIpparel** (Nakayama, Ackermann, Kesdogan, Zheng, Korosteleva, Sorkine-Hornung, Guibas, Yang, Wetzstein; CVPR 2025, arXiv 2412.03937). LLaVA-1.5-7B fine-tuned on GCD-MM (~120K garments with text and editing pairs). ~2.1 s per garment on an A6000 (vs ~52.2 s for DressCode); ~250 tokens per pattern (DressCode >30K). Trained on 8xH100, ~312 H100 GPU hours. Manifold garments only (no pockets), no physical/fabrication constraints, bias toward European/North American adult sizing. HF weights card: MIT licence; the checkpoint omits LLaVA vision encoder and projector, which must be fetched separately (LLaVA-1.5 has its own terms). — [arXiv 2412.03937](https://arxiv.org/pdf/2412.03937); [HF model card](https://huggingface.co/georgeNakayama/AIpparel)
- **ChatGarment** (Bian et al., CVPR 2025, arXiv 2412.08603). Apache-2.0 repo, built on LLaVA and LISA; uses GarmentCodeRC for patterns and ContourCraft-CG to drape (script run_garmentcode_sim.py); dataset ChatGarmentDataset on Hugging Face. Weights not mentioned on the repo page. — [GitHub](https://github.com/biansy000/ChatGarment)
- **Design2GarmentCode** (Zhou, Liu, Liu, He, Li, Jin, Wang; CVPR 2025). LMMs generate parametric pattern programs from images/text/sketches, outputting size-precise patterns with correct stitches. Code and data stated "will be publicly available"; I did not confirm release. — [CVPR poster page](https://cvpr.thecvf.com/virtual/2025/poster/33416)
- **DressCode**: located only as a comparison baseline in the AIpparel paper (52.2 s per garment); not researched directly.

### Inferences
- For an LLM-agent editing patterns, the useful asset is the program representation (GarmentCode DSL, MIT) and the idea of tokenising patterns compactly (~250 tokens in AIpparel), not the draping.
- GCD drapes use XPBD with fixed stiffness; training a preview net on them would inherit XPBD behaviour and limited fabric diversity. For a product calibrated to real fabrics (fabrics.json), you would regenerate drapes with your own FEM core.
- Both HF weights (AIpparel MIT) and repo licences look permissive, but base-model (LLaVA/Llama 2) and body-model (SMPL/CAESAR-derived) terms may restrict commercial use: unverified.

### Gaps
- Sewformer, DressCode, DiffAvatar/other 2025-2026 pattern-aware drape work: not researched.
- Licence of GarmentCodeData itself and of NeuralTailor repo not confirmed.
- Design2GarmentCode release status unknown.

## Learned layers on top of a physical solver (upsampling/detail, preconditioners, warm starts, subspace)

### Takeaway
Learned wrinkle upsampling exists and works on coarse physical sims (GDSR: 5K to 43K vertices in 32 ms). I found no located source for neural preconditioners or learned Newton/VBD warm starts for cloth; that part of the question is a literature gap in my search, not evidence of absence.

### Cited Findings
- **Neural Garment Dynamic Super-Resolution (GDSR)** (Zhang, Li; SIGGRAPH Asia 2024, DOI 10.1145/3680528.3687610). Takes a low-res simulation plus body motion, MeshGraphNet encoder (6 message-passing steps) plus a per-triangle hypernetwork implicit function giving wrinkle residuals. 5,010-vertex input to 43,257-vertex output: 0.115 s per frame total, of which 0.083 s is the Marvelous Designer low-res sim and 0.032 s the network (RTX 3090, unoptimised PyTorch, ~65 MB model). Generalises to unseen motions, body shapes and new garments, but trained on a single fabric (silk chamuse), cannot reproduce friction wrinkles, and collisions at low resolution can remain unresolved in multi-layer garments. Code: github.com/MengZephyr/Neural-Garment-Dynamic-Super-resolution (licence not checked). — [arXiv 2412.06285](https://arxiv.org/html/2412.06285v1)
- Related earlier work surfaced by search (not fetched): Deep Detail Enhancement for Any Garment (arXiv 2008.04367) and Deep Deformation Detail Synthesis for Thin Shell Models (arXiv 2102.11541). — [2008.04367](https://arxiv.org/pdf/2008.04367); [2102.11541](https://arxiv.org/pdf/2102.11541)
- Real-time latent-space garment deformation for games (Lee, Perepichka, Ghorbani, Mudur, Paquette, Popa; ECCV 2024 workshops): a latent predictor replaces a GNN at inference. No speeds in the page I read. — [ETS record](https://pure.etsmtl.ca/en/publications/real-time-neural-cloth-deformation-using-acompact-latent-space-an/)
- Solver-side context: Vertex Block Descent (VBD) is vertex-level Gauss-Seidel on the implicit-Euler energy; Offset Geometric Contact gives intersection-free cloth contact. The talk abstract (A. H. Chen, NVIDIA) says nothing about learned components. — [CMIS 2026 abstract](https://cmis2026.usi.ch/media/com_papers/abstracts/5_chen_CMIS2026_231036.pdf)

### Inferences
- Upsampling is the most compatible hybrid for failure 2 (resolution vs speed): run the physical solver at a coarse mesh and add wrinkle detail via a net. But GDSR is motion-driven wrinkling of a coarse sim, not gather/ruffle geometry, which is driven by seam length mismatch (inferred), and it cannot do friction wrinkles.
- A learned warm start for Newton/VBD would be a direct fit for "faster first guess" and could be trained with the FEM core as teacher (cheap labels, since the solver is yours), but no published recipe was found; this is an engineering proposal, not a cited result.

### Gaps
- No neural preconditioner, learned-Newton-initialiser or neural subspace/PCA-cloth paper was confirmed. The one search returned only a yarn-level homogenisation paper and a thesis on modified PCG.
- No 2025-2026 upsampling follow-ups were checked beyond GDSR.

## Neural model as fast preview in front of a physical solver (architecture, data, cost, Apple Silicon)

### Takeaway
A hybrid is plausible, but nothing published does it for arbitrary user-edited flat patterns; the nearest components are HOOD (pattern-agnostic via rest-geometry input), quasi-static/reduced-space nets (NeuralClothSim, PhySkin), and a physical refinement pass. Recommended path is a learned warm start trained with your own solver as teacher, not a HOOD port.

### Cited Findings
- HOOD takes rest-edge geometry and per-edge material as inputs, so unseen patterns, resizing and seam-topology toggles need no retraining (per the paper). — [alphaXiv 2212.07242](https://www.alphaxiv.org/abs/2212.07242.md)
- HOOD training is self-supervised and needs no simulated data; ContourCraft added multi-layer handling and recovers from initial intersections. — [ContourCraft](https://arxiv.org/html/2405.09522v2)
- ChatGarment already chains pattern generation to ContourCraft-CG to drape patterns stitched by `run_garmentcode_sim.py`, i.e. a neural drape of GarmentCode patterns exists as a pipeline. — [ChatGarment GitHub](https://github.com/biansy000/ChatGarment)
- Reported speeds: HOOD 7-14 fps on a 12K-vertex dress; ContourCraft 3.7-10.5 fps on RTX 4090 for 12K-36K nodes; HyperBones ~1.1 ms on A6000 and ~5 ms on RTX 3060 (but only for body-skinned garments). — sources above.
- Data cost anchor: GarmentCodeData took ~30 s per drape on an RTX 3090 for a GPU XPBD solver (~44 h per 5,000). — [arXiv 2405.17609](https://arxiv.org/pdf/2405.17609)

### Inferences (all my estimates, not sourced)
- **Candidate A, HOOD/ContourCraft-style GNN as first guess, then FEM settle.** Needs garments placed and seams closed as a body-attached start; handles failure 4 only if seam closure is part of the start state. Effort M for inference port plus L for retraining on flat-pattern-to-3D with seam forces (self-supervised, no labels). Risk: unproven on gathers; message-passing depth; mesh size 12K-36K nodes at 4-14 fps on a 4090-class GPU suggests an M-series chip would be no faster than interactive-slow (estimate, no measurement).
- **Candidate B, learned warm start for the FEM/VBD solver (teacher = your own solver).** Train on your own converged drapes (pattern parameters, body measurements, fabric to vertex positions or offsets from a seam-closed start). Even a mediocre guess shortens Newton/VBD iterations and reduces the never-settles risk by starting near equilibrium. Effort L; data generation cost = your solver time times N; with the GCD benchmark of ~30 s per drape for a Warp XPBD, 50K drapes would be ~420 h of one GPU (arithmetic from the cited number; your solver may differ).
- **Candidate C, coarse sim plus GDSR-style detail net.** Effort L-XL, requires multi-fabric training data and does not cover gathers/friction.
- **Apple Silicon estimate:** a ~65 MB GNN/hypernet in PyTorch MPS could plausibly run tens of ms per frame (unverified guess); MLX or CoreML conversion of PyG-style scatter ops needs custom work. Mark all as estimates; no source found.
- Licence blockers to check before shipping weights: SMPL (HOOD, ContourCraft training bodies), LLaVA/Llama 2 (AIpparel), CAESAR-derived body model (GCD), AMASS/BEDLAM data. For a MakeHuman-derived body (user's stack), any net trained on SMPL shapes would need retraining on MakeHuman bodies (inferred).

### Gaps
- No source evaluates any neural drape on user-edited flat patterns with gathers or tight seams.
- No on-device (MPS/MLX/CoreML) latency found for any cloth network.
- Training-data cost for a custom hybrid is an extrapolation only.
