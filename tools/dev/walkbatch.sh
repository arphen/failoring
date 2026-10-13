#!/bin/sh
# walkbatch.sh <design> <fabric> <planprefix> <tag> <ramp> [gdelay]: walking sim (130 frames) on all six presets in parallel -> out_<design>_<tag>_<preset>
cd ${REPO:-/home/claude/failoring}; S=$(dirname "$0"); D=$1; F=$2; PP=$3; T=$4; R=$5; GD=${6:-0}
for P in petite average tall curvy athletic plus; do
  (GDELAY=$GD RAMPF=$R FABRIC=$F PLANFILE=$S/${PP}_$P.json SETTLE=60 DUMP=$S/dump_${D}_${T}_$P.json node tools/walk_sim.js file $P 130 $S/out_${D}_${T}_$P > $S/log_${D}_${T}_$P.txt 2>&1) &
done
wait
