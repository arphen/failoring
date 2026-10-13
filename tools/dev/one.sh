#!/bin/bash
# one.sh <design> <only|-> <tag> <preset> [fabric] : measures -> export (env OPTS) -> plan -> standing sim (70 frames) for one preset; prints plan seams and settle line
cd ${REPO:-/home/claude/failoring}
S=${SK:-/tmp/failoring-sk}; mkdir -p $S
D=$1; ONLY=$2; TAG=$3; P=$4; FAB=${5:-cotton_poplin}; [ "$ONLY" = "-" ] && ONLY=""
node tools/fs_measures.js $P > $S/meas_$P.json
(cd tools/freesewing && node --import ./reg.mjs export.mjs $D $S/meas_$P.json $S/raw_${TAG}_$P.json $ONLY | head -1)
node tools/fs_plan.js $S/raw_${TAG}_$P.json $P $S/plan_${TAG}_$P.json | head -1
STAND=1 FABRIC=$FAB PLANFILE=$S/plan_${TAG}_$P.json SETTLE=60 COLORBY=piece VIEWS=${VIEWS:-front,side} YR=${YR:-0.45,1.3} FW=${FW:-300} PICK=${PICK:-70} DUMP=$S/dump_${TAG}_$P.json node tools/walk_sim.js file $P ${FR:-70} $S/out_${TAG}_$P 2>&1 | grep "settle:\|worst"
