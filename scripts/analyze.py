#!/usr/bin/env python3
"""Parse dcSim output files and compute summary statistics."""

import sys
from pathlib import Path

def parse_fct(path):
    """Parse fct.txt: <load> <fct_us>"""
    fcts = []
    with open(path) as f:
        for idx, line in enumerate(f):
            parts = line.split()
            if len(parts) >= 2:
                try:
                    load = float(parts[0])
                    fct_us = float(parts[1])
                    fcts.append((idx, load, fct_us))
                except ValueError:
                    pass
    return fcts

def parse_drop_reorder(path):
    """Parse drop_reorder.txt: <time> <drops> <reorders> <queueing>"""
    samples = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 4:
                t = float(parts[0])
                drops = int(parts[1])
                reorders = int(parts[2])
                queueing = int(parts[3])
                samples.append((t, drops, reorders, queueing))
    return samples

def parse_util(path):
    """Parse util_all.txt: <time> <util_Gbps>"""
    samples = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                try:
                    t = float(parts[0])
                    gbps = float(parts[1])
                    samples.append((t, gbps))
                except ValueError:
                    pass
    return samples

def percentile(values, p):
    """Compute pth percentile (0-100) of a sorted list."""
    if not values:
        return 0
    sorted_vals = sorted(values)
    k = (len(sorted_vals) - 1) * p / 100.0
    f_idx = int(k)
    c_idx = min(f_idx + 1, len(sorted_vals) - 1)
    if f_idx == c_idx:
        return sorted_vals[f_idx]
    d = k - f_idx
    return sorted_vals[f_idx] * (1 - d) + sorted_vals[c_idx] * d

def main():
    base = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("output")

    print(f"=== Analysis of {base} ===\n")

    # --- FCT analysis (the key metric) ---
    fct_path = base / "fct.txt"
    if fct_path.exists():
        flows = parse_fct(fct_path)
        if flows:
            fcts = [f[2] for f in flows]
            print(f"## Flow Completion Times ({len(fcts)} flows)")
            print(f"  Min FCT:    {min(fcts):8.2f} us")
            print(f"  Mean FCT:   {sum(fcts)/len(fcts):8.2f} us")
            print(f"  Median FCT: {percentile(fcts, 50):8.2f} us")
            print(f"  p95 FCT:    {percentile(fcts, 95):8.2f} us")
            print(f"  p99 FCT:    {percentile(fcts, 99):8.2f} us")
            print(f"  Max FCT:    {max(fcts):8.2f} us")
            print(f"  -> CCT (max FCT) = {max(fcts):.2f} us\n")
    else:
        print(f"  fct.txt not found\n")

    # --- Drop / reorder analysis ---
    dr_path = base / "drop_reorder.txt"
    if dr_path.exists():
        samples = parse_drop_reorder(dr_path)
        if samples:
            final = samples[-1]
            print(f"## Drops & Reordering (final cumulative counts at t={final[0]:.5f}s)")
            print(f"  Drops:     {final[1]}")
            print(f"  Reorders:  {final[2]}")
            print(f"  Queueing:  {final[3]} (cumulative pkt-time units)\n")

    # --- Throughput analysis ---
    util_path = base / "util_all.txt"
    if util_path.exists():
        samples = parse_util(util_path)
        if samples:
            gbps_values = [s[1] for s in samples]
            print(f"## Aggregate Throughput")
            print(f"  Peak:    {max(gbps_values):8.2f} Gbps")
            print(f"  Mean:    {sum(gbps_values)/len(gbps_values):8.2f} Gbps")
            print(f"  (samples: {len(samples)})\n")

if __name__ == "__main__":
    main()
