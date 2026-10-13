### denim_12oz (Denim 12 oz)

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 0.0001 | 55.9 | 8.55e-05 | 0.855 | 0.752 .. 0.937 | 0.724 | 0.855 |
| weft | 7e-05 | 50.0 | 6.11e-05 | 0.874 | 0.761 .. 0.951 | 0.740 | 0.874 |
| bias (ref = model) | 8.3e-05 | 52.8 | 7.22e-05 | 0.870 | 0.758 .. 0.939 | 0.737 | 0.870 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.752 10->0.855 16->0.937 ; weft 6->0.761 10->0.874 16->0.951 ; bias 6->0.758 10->0.870 16->0.939 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 1.2e+04 | 1.197e+04 | 0.998 | 0.406, 0.807, 1.207 | 3.1 |
| weft | 8000 | 7987 | 0.998 | 0.408, 0.809, 1.209 | 3.4 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 581.8 | 671.6 | 1.154 | 0.994, 1.872, 2.727 | 27.5 |

Shear from bias extension (naive uniform-stress formula): G = 174 N/m/rad vs input 150, ratio 1.160.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 80.1 | 6 | 131 | 149 | 300 | 1.0e-07 | 19.3 |
| 10.0 | 80.7 | 6 | 131 | 151 | 214 | 9.3e-09 | 36.2 |
| 7.0 | 82.4 | 6 | 133 | 149 | 300 | 1.0e-07 | 152.2 |

Total bench time: 248 s.
