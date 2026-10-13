#!/bin/bash
# fsrun_ease.sh <tag> <ease> : chinos for six presets with the standing cross seam x (1+ease); plans at plan_<tag>_<preset>.json
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
TAG=$1; E=$2
for P in petite average tall curvy athletic plus; do
  CSSTAND=1 node tools/fs_measures.js $P > $S/meas_$P.json
  OVRJ=$(node -e "const m=require('$S/meas_$P.json'); console.log(JSON.stringify({crossSeam: Math.round(m.crossSeam*1000*(1+$E)), crossSeamFront: Math.round(m.crossSeamFront*1000*(1+$E))}))")
  (cd tools/freesewing && OVR="$OVRJ" node --import ./reg.mjs export.mjs charlie $S/meas_$P.json $S/raw_${TAG}_$P.json charlie.front,charlie.back,charlie.waistband | head -1) > /dev/null
  node tools/fs_plan.js $S/raw_${TAG}_$P.json $P $S/plan_${TAG}_$P.json | head -1
done
