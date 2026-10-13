### silk_charmeuse (Silk charmeuse)

Cantilever, chord angle 41.5 deg, strip 25 mm wide. B from the exact elastica relation; ratio = derived / reference.

| direction | B reference (N m) | overhang l* (mm) | B derived (N m) | ratio | spread over mesh levels | Peirce ratio (UNVERIFIED) | l/2 ratio (UNVERIFIED) |
|---|---|---|---|---|---|---|---|
| warp | 2e-06 | 27.5 | 1.78e-06 | 0.889 | 0.776 .. 0.963 | 0.753 | 0.890 |
| weft | 2e-06 | 27.5 | 1.78e-06 | 0.889 | 0.776 .. 0.963 | 0.753 | 0.889 |
| bias (ref = model) | 2e-06 | 27.5 | 1.78e-06 | 0.889 | 0.777 .. 0.962 | 0.753 | 0.890 |

Mesh levels (elements along the overhang -> ratio): warp 6->0.776 10->0.889 16->0.963 ; weft 6->0.776 10->0.889 16->0.963 ; bias 6->0.777 10->0.889 16->0.962 ; 

Hanging strip with weights (50 mm wide, 300 mm long). k = d(tension per width)/d(strain) from a linear fit over the loads.

| direction | reference k (N/m) | derived k (N/m) | ratio | strains (%) | seconds |
|---|---|---|---|---|---|
| warp | 3000 | 2997 | 0.999 | 0.404, 0.804, 1.204 | 3.4 |
| weft | 2500 | 2498 | 0.999 | 0.404, 0.805, 1.205 | 2.8 |
| bias (ref = 4/(1/ku+1/kv+1/G)) | 59.35 | 72.28 | 1.218 | 1.005, 1.837, 2.647 | 20.2 |

Shear from bias extension (naive uniform-stress formula): G = 18.3 N/m/rad vs input 15, ratio 1.221.

Cusick drape (30 cm disc, 18 cm pedestal).

| edge h (mm) | DC (%) | folds | r_min (mm) | r_max (mm) | frames | KE (J) | seconds |
|---|---|---|---|---|---|---|---|
| 14.0 | 39.7 | 7 | 95 | 139 | 300 | 1.7e-07 | 17.0 |
| 10.0 | 46.6 | 7 | 103 | 142 | 300 | 2.2e-07 | 50.3 |
| 7.0 | 41.4 | 8 | 97 | 138 | 300 | 6.3e-07 | 152.7 |

Total bench time: 257 s.
