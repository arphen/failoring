### cotton_jersey (Cotton single jersey (knit))

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 3e-06 | 24.3 | 2.64e-06 | 0.881 | 0.770 .. 0.956 | 0.746 | 0.882 |
| weft | 3e-06 | 24.3 | 2.64e-06 | 0.879 | 0.768 .. 0.954 | 0.745 | 0.880 |
| bias (ref = model) | 3e-06 | 24.3 | 2.64e-06 | 0.880 | 0.770 .. 0.954 | 0.745 | 0.880 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.770 10->0.881 16->0.956 ; weft 6->0.768 10->0.879 16->0.954 ; bias 6->0.770 10->0.880 16->0.954 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 250 | 250 | 1.000 | 0.488, 0.888, 1.288 | 2.6 |
| weft | 100 | 100 | 1.000 | 0.621, 1.021, 1.421 | 1.4 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 13.35 | 16.09 | 1.206 | 2.331, 3.172, 3.990 | 16.3 |

Shear from bias extension (naive uniform-stress formula): G = 4.26 N/m/rad vs input 3.5, ratio 1.218.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 27.7 | 9 | 94 | 133 | 247 | 9.8e-09 | 14.7 |
| 10.0 | 27.6 | 9 | 94 | 130 | 223 | 8.9e-09 | 35.1 |
| 7.0 | 27.8 | 10 | 95 | 129 | 268 | 9.9e-09 | 135.4 |

Total bench time: 219 s.
