### wool_suiting (Wool suiting (worsted))

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 3e-05 | 43.9 | 2.6e-05 | 0.866 | 0.753 .. 0.948 | 0.734 | 0.867 |
| weft | 2.5e-05 | 41.5 | 2.19e-05 | 0.877 | 0.758 .. 0.952 | 0.743 | 0.877 |
| bias (ref = model) | 2.73e-05 | 42.7 | 2.39e-05 | 0.874 | 0.757 .. 0.946 | 0.740 | 0.875 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.753 10->0.866 16->0.948 ; weft 6->0.758 10->0.877 16->0.952 ; bias 6->0.757 10->0.874 16->0.946 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 4000 | 3996 | 0.999 | 0.410, 0.810, 1.210 | 2.1 |
| weft | 3500 | 3497 | 0.999 | 0.411, 0.811, 1.212 | 2.8 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 118.1 | 141.7 | 1.200 | 1.140, 1.985, 2.807 | 19.1 |

Shear from bias extension (naive uniform-stress formula): G = 36.1 N/m/rad vs input 30, ratio 1.204.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 62.9 | 6 | 111 | 148 | 300 | 3.5e-07 | 17.9 |
| 10.0 | 64.8 | 6 | 121 | 146 | 125 | 8.8e-09 | 20.6 |
| 7.0 | 67.7 | 6 | 122 | 146 | 137 | 9.6e-09 | 69.6 |

Total bench time: 139 s.
