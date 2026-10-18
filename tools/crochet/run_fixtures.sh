#!/bin/bash
# Run every crochet fixture through crochet_cli and write docs/crochet-shots/<name>.{json,obj,png}, <name>_rest.{obj,png}.
#   tools/crochet/run_fixtures.sh [path/to/crochet_cli] [load] [names...]
# Bags use cotton worsted on a 4.5 mm hook (plan section 11.2), everything else acrylic worsted on 5 mm. The pieces without a strap
# (circles, tube, facing swatch) stop after the relaxation; the pouch and the bags hang by a pin and, with a load such as mba13, take
# it. Slow: a bag is a few minutes of relaxation plus the hang (and the load).
set -u
cli=${1:-build/crochet_cli}
load=${2:-none}
shift 2 2>/dev/null || shift $# 2>/dev/null
names=("$@")
[ ${#names[@]} -eq 0 ] && names=(circle_plus5 circle_plus6 circle_plus8 tube40 facing pouch bag_round_plain bag_round_blo bag_round_flo bag_round_slst bag_round_surface)
outdir=docs/crochet-shots
mkdir -p "$outdir"
status=0
for name in "${names[@]}"; do
    yarn=acrylic_worsted; hook=5
    case "$name" in bag_*) yarn=cotton_worsted; hook=4.5 ;; esac
    thisLoad=none
    case "$name" in bag_*|pouch) thisLoad=$load ;; esac
    echo "== $name (yarn $yarn, hook $hook mm, load $thisLoad)"
    "$cli" "patterns/cp/$name.txt" --yarn "$yarn" --hook "$hook" --scene auto --load "$thisLoad" --out "$outdir/$name"
    code=$?
    echo "   exit $code"
    [ $code -ne 0 ] && status=$code
done
exit $status
