### polyester_chiffon (Polyester chiffon)

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 1e-06 | 26.3 | 8.9e-07 | 0.890 | 0.777 .. 0.965 | 0.754 | 0.890 |
| weft | 1e-06 | 26.3 | 8.9e-07 | 0.890 | 0.777 .. 0.965 | 0.753 | 0.890 |
| bias (ref = model) | 1e-06 | 26.3 | 8.9e-07 | 0.890 | 0.778 .. 0.964 | 0.754 | 0.891 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.777 10->0.890 16->0.965 ; weft 6->0.777 10->0.890 16->0.965 ; bias 6->0.778 10->0.890 16->0.964 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 2500 | 2498 | 0.999 | 0.403, 0.803, 1.203 | 2.6 |
| weft | 2000 | 1999 | 0.999 | 0.403, 0.803, 1.204 | 3.5 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 31.77 | 39.5 | 1.243 | 0.994, 1.810, 2.603 | 6.8 |

Shear from bias extension (naive uniform-stress formula): G = 9.96 N/m/rad vs input 8, ratio 1.245.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 32.0 | 6 | 94 | 141 | 101 | 3.3e-09 | 6.0 |
| 10.0 | 43.1 | 8 | 100 | 142 | 300 | 1.9e-07 | 47.6 |
| 7.0 | 45.6 | 8 | 104 | 141 | 300 | 1.5e-05 | 150.8 |

Total bench time: 228 s.
