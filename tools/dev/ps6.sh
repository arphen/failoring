#!/bin/bash
# ps6.sh <planprefix> <tag> [piece idx] : walking sims with PIECESTAT for the six presets (2 at a time); prints the piece lines
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
pre=$1; tag=$2; pi=${3:-4}
run() { P=$1; PIECESTAT=$pi FABRIC=${FABRIC:-cotton_poplin} PLANFILE=$S/${pre}_$P.json SETTLE=60 node tools/walk_sim.js file $P ${FR:-130} $S/out_${tag}_$P > $S/log_${tag}_$P.txt 2>&1; }
for pair in "petite average" "tall curvy" "athletic plus"; do
  set -- $pair
  run $1 &
  run $2 &
  wait
done
for P in petite average tall curvy athletic plus; do echo "== $P"; grep -E "piece|worst|settle:" $S/log_${tag}_$P.txt | cut -c1-700; done > $S/ps_${tag}.txt
echo done > $S/done_${tag}.txt
