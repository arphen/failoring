#!/bin/bash
# walkc.sh <planprefix> <tag> <fabric> : walking sims (130 frames) for six presets, 2 at a time
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
pre=$1; tag=$2; fab=$3
run() { P=$1; FABRIC=$fab PLANFILE=$S/${pre}_$P.json SETTLE=60 COLORBY=piece VIEWS=side,front DUMP=$S/dump_${tag}_$P.json node tools/walk_sim.js file $P 130 $S/out_${tag}_$P > $S/log_${tag}_$P.txt 2>&1; }
for pair in "petite average" "tall curvy" "athletic plus"; do
  set -- $pair
  run $1 &
  run $2 &
  wait
done
echo done > $S/done_${tag}.txt
