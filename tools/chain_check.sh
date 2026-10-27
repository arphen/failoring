#!/usr/bin/env bash
# The chain harness: run the process simulator over a fixed grid of settings and show, for every grid point, which stitches the frozen
# stitch judge passes.
#
#   tools/chain_check.sh <run-name> <chains> [rig|slip] [NAME=VALUE ...]
#
# The grid is the baseline (the code's defaults) plus one step either way on each main knob, one knob at a time: HOLD_BELOW 10 mm,
# damping 20, the work slide SIM_SEAT 5 mm. A setting works when all seven points pass every stitch. Each point writes its log and
# renders to build/chain_runs/<run-name>/<grid-point>/. The last three lines of output are the baseline's final front, oblique and top
# renders.
#
# The slip knot is made once per distinct damping (the only grid knob that acts during the knot) and saved to
# build/chain_runs/<run-name>/knots/d<damp>.ckpt; every grid point with that damping loads it. The chain knobs (HOLD_BELOW, SIM_SEAT) act after
# the knot, so they are set on the loading run only. Four points run at a time (SIM_JOBS to change it).
#
# Extra NAME=VALUE pairs go to every run; the grid's three knobs and --damp are set last, so the grid is the same whatever the extras.
set -u

if [ $# -lt 2 ]; then
    echo "usage: tools/chain_check.sh <run-name> <chains> [rig|slip] [NAME=VALUE ...]" >&2
    exit 2
fi

cd "$(dirname "$0")/.." || exit 2

run=$1
chains=$2
shift 2
mode=slip
if [ $# -gt 0 ] && { [ "$1" = "rig" ] || [ "$1" = "slip" ]; }; then
    mode=$1
    shift
fi
# Bash 3.2 errors on an empty array under `set -u`; the marker keeps the array non-empty and is ignored by the simulator.
extra=(CHAIN_CHECK_MODE="$mode")
if [ $# -gt 0 ]; then extra+=("$@"); fi
if [ "$mode" = "rig" ]; then extra+=(SIM_RIG=1); fi

bin=build/yarn_sim
if [ ! -x "$bin" ]; then
    echo "chain_check: $bin is missing; build it first" >&2
    exit 2
fi

outroot=build/chain_runs/$run
mkdir -p "$outroot"

# name, HOLD_BELOW mm, damping, SIM_SEAT mm
points=(
    "base 10 20 5"
    "hold9 9 20 5"
    "hold11 11 20 5"
    "damp17 10 17 5"
    "damp23 10 23 5"
    "seat4 10 20 4"
    "seat6 10 20 6"
)

jobs=${SIM_JOBS:-4}   # the four performance cores
if [ "$jobs" -lt 1 ]; then jobs=1; fi
if [ "$jobs" -gt 16 ]; then jobs=16; fi

# The knot for one damping value: made once, saved, and reused by every grid point with that damping.
knotdir=$outroot/knots
mkdir -p "$knotdir"
make_knot() {
    local damp=$1
    local ckpt=$knotdir/d$damp.ckpt
    if [ -f "$ckpt" ]; then return 0; fi
    mkdir -p "$knotdir/d$damp"
    env "${extra[@]}" "$bin" --save-after knot --save "$ckpt" --damp "$damp" --every 0 --out "$knotdir/d$damp/sim" \
        > "$knotdir/d$damp/log.txt" 2>&1
}

run_point() {
    local name=$1 hold=$2 damp=$3 seat=$4
    local dir=$outroot/$name
    mkdir -p "$dir"
    env "${extra[@]}" HOLD_BELOW="$hold" SIM_SEAT="$seat" \
        "$bin" --load "$knotdir/d$damp.ckpt" --chains "$chains" --damp "$damp" --every 0 --out "$dir/sim" > "$dir/log.txt" 2>&1
}

# One knot per distinct damping, made before any grid point starts.
for damp in $(for p in "${points[@]}"; do echo "$p" | awk '{print $3}'; done | sort -u); do
    make_knot "$damp" || { echo "chain_check: the knot at damping $damp failed; see $knotdir/d$damp/log.txt" >&2; exit 2; }
done

pids=()
for p in "${points[@]}"; do
    # shellcheck disable=SC2086
    set -- $p
    run_point "$1" "$2" "$3" "$4" &
    pids+=($!)
    if [ "${#pids[@]}" -ge "$jobs" ]; then
        wait "${pids[0]}"
        pids=("${pids[@]:1}")
    fi
done
if [ "${#pids[@]}" -gt 0 ]; then
    for pid in "${pids[@]}"; do wait "$pid"; done
fi

# Read one point's judge lines: P/F per stitch, in stitch order.
verdicts_for() {
    local log=$outroot/$1/log.txt
    local i
    for i in $(seq 1 "$chains"); do
        local v="-"
        local line
        line=$(grep -E "^stitch $i (PASS|FAIL) " "$log" 2>/dev/null | tail -1)
        if [ -n "$line" ]; then
            case "$line" in
                "stitch $i PASS "*) v=P ;;
                "stitch $i FAIL "*) v=F ;;
            esac
        fi
        printf '%s ' "$v"
    done
}

all=1
printf '%-8s %5s %5s %5s ' point HOLD damp seat
for i in $(seq 1 "$chains"); do printf ' s%s' "$i"; done
printf '  in-a-row\n'
for p in "${points[@]}"; do
    # shellcheck disable=SC2086
    set -- $p
    row=$(verdicts_for "$1")
    count=0
    for v in $row; do
        if [ "$v" = "P" ]; then count=$((count + 1)); else break; fi
    done
    if [ "$count" -ne "$chains" ]; then all=0; fi
    printf '%-8s %5s %5s %5s ' "$1" "$2" "$3" "$4"
    for v in $row; do printf '  %s' "$v"; done
    printf '   %d/%d\n' "$count" "$chains"
done

base=$outroot/base
echo "baseline renders:"
for view in front obl top; do
    f=$(ls "$base"/sim_*_end_$view.png 2>/dev/null | tail -1)
    if [ -n "$f" ]; then echo "  $f"; else echo "  $base/sim_*_end_$view.png (missing)"; fi
done

if [ "$all" -eq 1 ]; then
    echo "all seven grid points pass"
else
    echo "not all seven grid points pass"
    exit 1
fi
