#!/usr/bin/env python3
"""Checks and comparisons on fabric_bench JSON output (one <id>.json per preset).

  python3 tools/fabric_bench/check_bench.py DIR

Bending check (theory): the cantilever-derived bending rigidity B (elastica relation at 41.5 deg) divided by the
input B, for warp and weft, at the finest mesh level in the run (largest elements_along_overhang).
Stated tolerance: |ratio - 1| <= BEND_TOL. This tolerance is a CHOICE made for this check, not a textile standard.
The headline (middle) level and the spread over levels are printed as well, because they are not converged.

Drape comparison: the bench's drape coefficient at the headline edge length against the values documented in
docs/ARCHITECTURE.md (10 mm mesh). Both numbers are printed; nothing is adjusted.
"""
import json
import os
import sys

BEND_TOL = 0.05
DOC_DRAPE = {  # docs/ARCHITECTURE.md, "Verification (ctest)", 10 mm mesh (percent)
    "silk_charmeuse": 36, "polyester_chiffon": 38, "cotton_jersey": 28, "cotton_poplin": 64, "wool_suiting": 65,
    "denim_12oz": 81, "linen_medium": 82, "canvas_heavy": 93, "lambskin_leather": 91,
}


def load(dirpath):
    rows = []
    for name in sorted(os.listdir(dirpath)):
        if not name.endswith(".json") or name == "summary.json":
            continue
        with open(os.path.join(dirpath, name)) as f:
            rows.append(json.load(f))
    return rows


def bend_row(d, dirname):
    for b in d["bending"]:
        if b["dir"] != dirname:
            continue
        levels = sorted(b["levels"], key=lambda l: l["elements_along_overhang"])
        fin = levels[-1]
        head = levels[b["headline_level"]]
        ratios = [l["ratio"] for l in levels]
        return fin, head, ratios
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    rows = load(sys.argv[1])
    print("| fabric | direction | finest n | ratio finest | check (|r-1| <= %.2f) | headline n | ratio headline | ratios all levels |" % BEND_TOL)
    print("|---|---|---|---|---|---|---|---|")
    failures = 0
    for d in rows:
        for dirname in ("warp", "weft"):
            r = bend_row(d, dirname)
            if r is None:
                continue
            fin, head, ratios = r
            ok = abs(fin["ratio"] - 1) <= BEND_TOL
            failures += 0 if ok else 1
            print("| %s | %s | %d | %.3f | %s | %d | %.3f | %s |" % (
                d["fabric"], dirname, fin["elements_along_overhang"], fin["ratio"], "PASS" if ok else "FAIL",
                head["elements_along_overhang"], head["ratio"], ", ".join("%.3f" % x for x in ratios)))
    print()
    print("| fabric | drape DC % at 10 mm (bench) | docs/ARCHITECTURE.md | difference (points) | folds (10 mm) | KE at last frame (J) |")
    print("|---|---|---|---|---|---|")
    for d in rows:
        dr = d.get("drape", [])
        if not dr:
            continue
        head = dr[d["drape_headline_level"]]
        doc = DOC_DRAPE.get(d["fabric"])
        doc_s = "%d" % doc if doc is not None else "n/a"
        diff = "%+.1f" % (head["drape_coefficient_pct"] - doc) if doc is not None else "n/a"
        print("| %s | %.1f | %s | %s | %d | %s |" % (d["fabric"], head["drape_coefficient_pct"], doc_s, diff, head["folds"],
                                                  "%.1e" % head["kinetic_energy_J"]))
    print()
    print("bending checks failed: %d (tolerance %.2f, finest mesh level)" % (failures, BEND_TOL))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
