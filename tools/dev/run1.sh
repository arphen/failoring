#!/bin/sh
# run1.sh <preset> [tag] : plan + standing sim of the FreeSewing pencil skirt, dump positions
cd ${REPO:-/home/claude/failoring}; S=$(dirname "$0"); P=$1; T=${2:-base}
node tools/fs_plan.js app/assets/freesewing/raw_penelope_$P.json $P $S/plan_${T}_$P.json >/dev/null
STAND=1 FABRIC=wool_suiting PLANFILE=$S/plan_${T}_$P.json SETTLE=60 DUMP=$S/dump_${T}_$P.json node tools/walk_sim.js file $P 70 $S/out_${T}_$P > $S/log_${T}_$P.txt 2>&1
echo "$P done exit $?" >> $S/done.txt
