#!/bin/sh
# tools/fs_batch.sh <outdir> [walk]  : FreeSewing tee, skirt and chinos on every preset (standing; walking if 2nd arg is 'walk')
OUT=${1:-/tmp/fs_batch}; mkdir -p $OUT; cd "$(dirname "$0")/.."
for P in $(node -e "console.log(Object.keys(require('./app/src/shared.js').PRESETS).join(' '))"); do
  node tools/fs_measures.js $P > $OUT/m_$P.json
  for D in teagan:cotton_jersey: penelope:wool_suiting: charlie:cotton_poplin:charlie.front,charlie.back,charlie.waistband; do
    N=${D%%:*}; R=${D#*:}; F=${R%%:*}; ONLY=${R#*:}
    (cd tools/freesewing && node --import ./reg.mjs export.mjs $N $OUT/m_$P.json $OUT/raw_${N}_$P.json $ONLY >/dev/null)
    node tools/fs_plan.js $OUT/raw_${N}_$P.json $P $OUT/plan_${N}_$P.json >/dev/null
    if [ "$2" = walk ]; then ST=""; FR=130; else ST=1; FR=70; fi
    STAND=$ST FABRIC=$F PLANFILE=$OUT/plan_${N}_$P.json SETTLE=60 node tools/walk_sim.js file $P $FR $OUT/sim_${N}_$P >/dev/null 2>&1
    cp $OUT/sim_${N}_$P/walk_file_$P.png $OUT/${N}_$P.png 2>/dev/null
    echo "$N $P $?"
  done
done
