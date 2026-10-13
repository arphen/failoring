| fabric | B warp in (N m) | B warp derived/in | B weft derived/in | B bias derived/model | k warp derived/in | k weft derived/in | bias-stretch derived/ideal | G naive/in | DC % (spread) | bench s |
|---|---|---|---|---|---|---|---|---|---|---|
| cotton_poplin | 1.2e-05 | 0.877 | 0.881 | 0.879 | 0.999 | 0.999 | 1.184 | 1.19 | 65.0 (57.2..65.5) | 257 |
| denim_12oz | 0.0001 | 0.855 | 0.874 | 0.870 | 0.998 | 0.998 | 1.154 | 1.16 | 80.7 (80.1..82.4) | 248 |
| linen_medium | 4e-05 | 0.860 | 0.873 | 0.871 | 0.998 | 0.998 | 1.178 | 1.18 | 81.2 (81.2..81.8) | 246 |
| wool_suiting | 3e-05 | 0.866 | 0.877 | 0.874 | 0.999 | 0.999 | 1.200 | 1.20 | 64.8 (62.9..67.7) | 139 |
| silk_charmeuse | 2e-06 | 0.889 | 0.889 | 0.889 | 0.999 | 0.999 | 1.218 | 1.22 | 46.6 (39.7..46.6) | 257 |
| polyester_chiffon | 1e-06 | 0.890 | 0.890 | 0.890 | 0.999 | 0.999 | 1.243 | 1.25 | 43.1 (32.0..45.6) | 228 |
| canvas_heavy | 0.0002 | 0.843 | 0.847 | 0.845 | 0.998 | 0.998 | 1.151 | 1.16 | 92.8 (88.7..93.4) | 221 |
| cotton_jersey | 3e-06 | 0.881 | 0.879 | 0.880 | 1.000 | 1.000 | 1.206 | 1.22 | 27.6 (27.6..27.8) | 219 |
| lambskin_leather | 0.0003 | 0.838 | 0.838 | 0.837 | 0.999 | 0.999 | 1.124 | 1.13 | 90.9 (87.1..91.3) | 236 |

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

