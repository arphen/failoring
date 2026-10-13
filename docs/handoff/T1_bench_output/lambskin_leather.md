### lambskin_leather (Lambskin garment leather)

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 0.0003 | 69.9 | 0.000252 | 0.838 | 0.733 .. 0.921 | 0.710 | 0.839 |
| weft | 0.0003 | 69.9 | 0.000252 | 0.838 | 0.733 .. 0.921 | 0.710 | 0.839 |
| bias (ref = model) | 0.0003 | 69.9 | 0.000251 | 0.837 | 0.734 .. 0.916 | 0.709 | 0.837 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.733 10->0.838 16->0.921 ; weft 6->0.733 10->0.838 16->0.921 ; bias 6->0.734 10->0.837 16->0.916 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 7000 | 6990 | 0.999 | 0.413, 0.814, 1.214 | 2.0 |
| weft | 7000 | 6990 | 0.999 | 0.413, 0.814, 1.214 | 2.0 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 933.3 | 1049 | 1.124 | 1.016, 1.918, 2.795 | 14.3 |

Shear from bias extension (naive uniform-stress formula): G = 283 N/m/rad vs input 250, ratio 1.134.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 87.1 | 3 | 135 | 152 | 300 | 5.8e-08 | 17.1 |
| 10.0 | 90.9 | 5 | 140 | 150 | 300 | 5.9e-08 | 47.3 |
| 7.0 | 91.3 | 5 | 140 | 152 | 300 | 2.3e-07 | 149.1 |

Total bench time: 236 s.
