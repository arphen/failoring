#!/usr/bin/env python3
"""Markdown table of the five hinge-round bag variants (plan 11.2, T6.5) from the crochet_cli outputs.

    tools/crochet/variant_table.py [docs/crochet-shots]

Reads <dir>/<name>.json for the pouch and the five bag_round_<variant> fixtures; a variant whose run has not finished is listed as pending.
"""
import json
import os
import sys

DIR = sys.argv[1] if len(sys.argv) > 1 else "docs/crochet-shots"
NAMES = [("pouch", "pouch (reference)"), ("bag_round_plain", "plain sc round"), ("bag_round_blo", "BLO round"), ("bag_round_flo", "FLO round"),
         ("bag_round_slst", "sl st round"), ("bag_round_surface", "surface sl st")]
COLS = [("corner_angle_deg", "corner angle (deg)", "%.1f"), ("base_sag_mm", "base sag (mm)", "%.1f"), ("drop_cm", "drop (cm)", "%.1f"),
        ("bag_height_cm", "bag height (cm)", "%.1f"), ("opening_gape_cm", "gape (cm)", "%.1f"), ("strap_elongation_pct", "strap +%", "%.1f"),
        ("load_top_above_rim_cm", "load above rim (cm)", "%.1f"), ("max_contact_pressure_kPa", "peak pressure (kPa)", "%.0f"),
        ("mass_g", "mass (g)", "%.0f"), ("frames", "frames", "%d")]

def cell(m, key, fmt):
    v = m.get(key)
    return "-" if v is None else fmt % v

rows = []
for name, label in NAMES:
    path = os.path.join(DIR, name + ".json")
    if not os.path.exists(path):
        rows.append("| %s | pending |" % label + " |" * (len(COLS) - 1))
        continue
    d = json.load(open(path))
    m = d["metrics"]
    ok = "yes" if d.get("settled") else "NO"
    rows.append("| %s | %s | " % (label, ok) + " | ".join(cell(m, k, f) for k, _, f in COLS) + " |")
head = "| piece | settled | " + " | ".join(h for _, h, _ in COLS) + " |"
print(head)
print("|" + "---|" * (len(COLS) + 2))
print("\n".join(rows))
