### canvas_heavy (Cotton canvas / duck)

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 0.0002 | 74.0 | 0.000169 | 0.843 | 0.734 .. 0.926 | 0.714 | 0.844 |
| weft | 0.00018 | 71.5 | 0.000153 | 0.847 | 0.739 .. 0.930 | 0.718 | 0.848 |
| bias (ref = model) | 0.00019 | 72.7 | 0.00016 | 0.845 | 0.738 .. 0.923 | 0.715 | 0.845 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.734 10->0.843 16->0.926 ; weft 6->0.739 10->0.847 16->0.930 ; bias 6->0.738 10->0.845 16->0.923 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 1.5e+04 | 1.496e+04 | 0.998 | 0.404, 0.805, 1.206 | 3.0 |
| weft | 1.2e+04 | 1.197e+04 | 0.998 | 0.405, 0.806, 1.207 | 3.0 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 776.7 | 894.1 | 1.151 | 0.964, 1.844, 2.701 | 24.4 |

Shear from bias extension (naive uniform-stress formula): G = 231 N/m/rad vs input 200, ratio 1.156.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 88.7 | 3 | 136 | 152 | 105 | 9.4e-09 | 6.0 |
| 10.0 | 92.8 | 5 | 142 | 151 | 179 | 8.4e-09 | 29.1 |
| 7.0 | 93.4 | 5 | 142 | 151 | 300 | 5.5e-08 | 151.1 |

Total bench time: 221 s.
