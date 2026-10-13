#!/bin/bash
# zz.sh <planprefix> <tag> : standing render for six presets (2 at a time), piece colours, front+side, hips zoom. env EXTRA="A=1 B=2" for walk_sim env
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
pre=$1; tag=$2
run() { P=$1; env $EXTRA STAND=1 FABRIC=${FABRIC:-cotton_poplin} PLANFILE=$S/${pre}_$P.json SETTLE=60 COLORBY=piece VIEWS=${VIEWS:-front,side} YR=${YR:-0.45,1.3} FW=${FW:-300} PICK=${PICK:-70} DUMP=$S/dump_${tag}_$P.json node tools/walk_sim.js file $P 70 $S/out_${tag}_$P > $S/log_${tag}_$P.txt 2>&1; }
for pair in "petite average" "tall curvy" "athletic plus"; do
  set -- $pair
  run $1 &
  run $2 &
  wait
done
python3 "$(dirname "$0")/mont3.py" $tag $S/m_${tag}.png $S 2>&1 | tail -1
echo done > $S/done_${tag}.txt
