#!/bin/sh
# rampbatch.sh <design> <fabric> <planprefix> <ramp> <frames> : standing sim on all six presets in parallel; dumps to dump_<design>_r<ramp>_<preset>.json
cd ${REPO:-/home/claude/failoring}; S=$(dirname "$0"); D=$1; F=$2; PP=$3; R=$4; FR=$5
for P in petite average tall curvy athletic plus; do
  (STAND=1 GDELAY=${GD:-0} RAMPF=$R FABRIC=$F PLANFILE=$S/${PP}_$P.json SETTLE=$((FR-10)) DUMP=$S/dump_${D}_r${R}g${GD:-0}_$P.json COLORBY=piece VIEWS=front,back FW=300 node tools/walk_sim.js file $P $FR $S/out_${D}_r${R}g${GD:-0}_$P > $S/log_${D}_r${R}g${GD:-0}_$P.txt 2>&1) &
done
wait
