### cotton_poplin (Cotton poplin (shirting))

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 1.2e-05 | 41.5 | 1.05e-05 | 0.877 | 0.755 .. 0.951 | 0.742 | 0.877 |
| weft | 9e-06 | 37.8 | 7.93e-06 | 0.881 | 0.771 .. 0.958 | 0.746 | 0.881 |
| bias (ref = model) | 1.03e-05 | 39.5 | 9.09e-06 | 0.879 | 0.771 .. 0.954 | 0.744 | 0.879 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.755 10->0.877 16->0.951 ; weft 6->0.771 10->0.881 16->0.958 ; bias 6->0.771 10->0.879 16->0.954 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 6000 | 5991 | 0.999 | 0.404, 0.804, 1.205 | 2.9 |
| weft | 4000 | 3996 | 0.999 | 0.405, 0.805, 1.206 | 3.0 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 176.7 | 209.1 | 1.184 | 0.969, 1.825, 2.658 | 16.1 |

Shear from bias extension (naive uniform-stress formula): G = 53.4 N/m/rad vs input 45, ratio 1.188.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 57.2 | 5 | 105 | 147 | 300 | 1.3e-07 | 19.0 |
| 10.0 | 65.0 | 7 | 116 | 147 | 300 | 3.6e-07 | 50.7 |
| 7.0 | 65.5 | 6 | 119 | 146 | 300 | 3.7e-07 | 157.6 |

Total bench time: 257 s.
