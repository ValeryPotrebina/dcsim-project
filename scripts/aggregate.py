#!/usr/bin/env python3
"""Walk <output_dir>/<alg>_<size>pkt/ folders, parse fct.txt + drop_reorder.txt,
emit a single results.csv with one row per (algorithm, flow_size_pkts).

Usage:
  python aggregate.py [output_dir]
  default output_dir = "output"
"""

import os
import sys
import csv
import re
from pathlib import Path

OUTPUT_DIR  = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("output")
RESULTS_CSV = OUTPUT_DIR / "results.csv"

DIR_RE = re.compile(r"^(?P<alg>[a-z]+)_(?P<size>\d+)pkt$")


def percentile(values, p):
    if not values:
        return 0.0
    sv = sorted(values)
    k = (len(sv) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(sv) - 1)
    if lo == hi:
        return sv[lo]
    return sv[lo] * (hi - k) + sv[hi] * (k - lo)


def parse_fct(path):
    """Returns list of FCT (us) values."""
    fcts = []
    if not path.exists():
        return fcts
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                try:
                    fcts.append(float(parts[1]))
                except ValueError:
                    pass
    return fcts


def parse_drop_reorder(path):
    """Returns last cumulative (drops, reorders) as (int, int) or (0, 0)."""
    if not path.exists():
        return (0, 0)
    last = None
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 4:
                last = parts
    if last is None:
        return (0, 0)
    try:
        return (int(last[1]), int(last[2]))
    except (ValueError, IndexError):
        return (0, 0)


def parse_util_all(path):
    """Returns max throughput in Gbps."""
    if not path.exists():
        return 0.0
    peak = 0.0
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                try:
                    peak = max(peak, float(parts[1]))
                except ValueError:
                    pass
    return peak


def main():
    rows = []
    for sub in sorted(OUTPUT_DIR.iterdir()):
        if not sub.is_dir():
            continue
        m = DIR_RE.match(sub.name)
        if not m:
            continue
        alg  = m.group("alg")
        size = int(m.group("size"))

        fcts = parse_fct(sub / "fct.txt")
        if not fcts:
            print(f"[skip] {sub.name}: no fct.txt or empty")
            continue

        drops, reorders = parse_drop_reorder(sub / "drop_reorder.txt")
        peak_gbps       = parse_util_all(sub / "util_all.txt")

        rows.append({
            "algorithm":      alg,
            "flow_size_pkts": size,
            "n_flows":        len(fcts),
            "min_fct_us":     round(min(fcts), 2),
            "mean_fct_us":    round(sum(fcts) / len(fcts), 2),
            "p99_fct_us":     round(percentile(fcts, 99), 2),
            "cct_us":         round(max(fcts), 2),
            "drops":          drops,
            "reorders":       reorders,
            "peak_gbps":      round(peak_gbps, 1),
        })

    # Sort: alg first then size
    alg_order = {"dcsim": 0, "dcpim": 1, "pfabric": 2, "dctcp": 3}
    rows.sort(key=lambda r: (alg_order.get(r["algorithm"], 99), r["flow_size_pkts"]))

    if not rows:
        print(f"No results found in {OUTPUT_DIR}/")
        sys.exit(1)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    with open(RESULTS_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"\n=== {RESULTS_CSV} ===\n")
    headers = list(rows[0].keys())
    widths  = [max(len(h), max(len(str(r[h])) for r in rows)) for h in headers]
    print(" | ".join(h.ljust(w) for h, w in zip(headers, widths)))
    print("-+-".join("-" * w for w in widths))
    for r in rows:
        print(" | ".join(str(r[h]).ljust(w) for h, w in zip(headers, widths)))
    print(f"\n{len(rows)} rows written to {RESULTS_CSV}")


if __name__ == "__main__":
    main()
