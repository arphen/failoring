#!/bin/bash
# fsrun.sh <design> <only|-> <tag> : measures -> FreeSewing export -> plan for every preset; plans at $S/plan_<tag>_<preset>.json (raw at raw_<tag>_<preset>.json)
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
D=$1; ONLY=$2; TAG=$3; [ "$ONLY" = "-" ] && ONLY=""
for P in petite average tall curvy athletic plus; do
  node tools/fs_measures.js $P > $S/meas_$P.json
  (cd tools/freesewing && node --import ./reg.mjs export.mjs $D $S/meas_$P.json $S/raw_${TAG}_$P.json $ONLY | head -3)
  node tools/fs_plan.js $S/raw_${TAG}_$P.json $P $S/plan_${TAG}_$P.json | tail -1
done
