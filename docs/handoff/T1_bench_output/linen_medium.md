### linen_medium (Linen (medium weight))

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 4e-05 | 53.8 | 3.44e-05 | 0.860 | 0.756 .. 0.943 | 0.729 | 0.861 |
| weft | 3.5e-05 | 51.7 | 3.06e-05 | 0.873 | 0.759 .. 0.946 | 0.739 | 0.873 |
| bias (ref = model) | 3.74e-05 | 52.9 | 3.26e-05 | 0.871 | 0.759 .. 0.941 | 0.738 | 0.872 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.756 10->0.860 16->0.943 ; weft 6->0.759 10->0.873 16->0.946 ; bias 6->0.759 10->0.871 16->0.941 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 8000 | 7986 | 0.998 | 0.404, 0.805, 1.205 | 3.0 |
| weft | 7000 | 6989 | 0.998 | 0.404, 0.805, 1.206 | 3.1 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 274.8 | 323.9 | 1.178 | 0.970, 1.829, 2.667 | 14.4 |

Shear from bias extension (naive uniform-stress formula): G = 82.8 N/m/rad vs input 70, ratio 1.182.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 81.6 | 6 | 131 | 150 | 300 | 8.4e-08 | 18.3 |
| 10.0 | 81.2 | 6 | 133 | 151 | 300 | 8.1e-08 | 49.8 |
| 7.0 | 81.8 | 5 | 130 | 149 | 300 | 6.6e-08 | 152.1 |

Total bench time: 246 s.
