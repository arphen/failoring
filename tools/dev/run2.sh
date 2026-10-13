#!/bin/sh
# run2.sh <design> <fabric> <preset> <tag> [walk]: plan + sim (standing unless 'walk'), dump
cd ${REPO:-/home/claude/failoring}; S=$(dirname "$0"); D=$1; F=$2; P=$3; T=$4
node tools/fs_plan.js app/assets/freesewing/raw_${D}_$P.json $P $S/plan_${D}_${T}_$P.json >/dev/null
if [ "$5" = walk ]; then ST=""; FR=130; else ST=1; FR=70; fi
STAND=$ST FABRIC=$F PLANFILE=$S/plan_${D}_${T}_$P.json SETTLE=60 DUMP=$S/dump_${D}_${T}_$P.json node tools/walk_sim.js file $P $FR $S/out_${D}_${T}_$P > $S/log_${D}_${T}_$P.txt 2>&1
echo "$D $P done exit $?" >> $S/done2.txt
