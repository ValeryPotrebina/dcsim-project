#!/usr/bin/env python3
"""Read <output_dir>/results.csv and emit <output_dir>/fig5_reproduction.png.

Reproduces the structure of Fig. 5(a) from the dcSim paper as a
grouped bar chart (matching the paper's visual style):
- X axis: flow size labelled as multiples of BDP (0.5x, 1x, 2x, 4x)
- Y axis: CCT (us), linear scale
- 4 grouped bars per X position: pFabric, pHost-stub, dcPIM, dcSim
  (we use DCTCP as the 4th algorithm in lieu of pHost since the
   simulator does not expose pHost directly)

Usage:
  python plot.py [output_dir]
  default output_dir = "output"

Falls back to ASCII table if matplotlib is not installed.
"""

import csv
import re
import sys
from pathlib import Path
from collections import defaultdict

OUTPUT_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("output")
CSV_PATH   = OUTPUT_DIR / "results.csv"
PNG_PATH   = OUTPUT_DIR / "fig5_reproduction.png"

# Detect "kN" suffix in folder name to title the plot accordingly.
_M = re.search(r"k(\d+)", OUTPUT_DIR.name)
K_LABEL = _M.group(1) if _M else "?"

# Which sizes correspond to which BDP multiple in our reproduction.
# (BDP = 87 packets per paper; we use round numbers 50/100/200/400.)
SIZE_TO_BDP_LABEL = {
    50:  "0.5x",
    100: "1x",
    200: "2x",
    400: "4x",
}


def load_csv(path):
    series = defaultdict(dict)  # alg -> {size: cct}
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            series[row["algorithm"]][int(row["flow_size_pkts"])] = float(row["cct_us"])
    return series


def ascii_plot(series, sizes):
    print("\n=== ASCII CCT plot (matplotlib not installed) ===\n")
    headers = ["alg \\ size"] + [f"{s}pkt" for s in sizes]
    rows = []
    for alg in ["pfabric", "dctcp", "dcpim", "dcsim"]:
        if alg not in series:
            continue
        rows.append([alg] + [f"{series[alg].get(s, 0):.1f}" for s in sizes])

    widths = [max(len(str(rows[r][c])) for r in range(len(rows))) for c in range(len(rows[0]))]
    widths = [max(w, len(headers[c])) for c, w in enumerate(widths)]

    print(" | ".join(h.ljust(w) for h, w in zip(headers, widths)))
    print("-+-".join("-" * w for w in widths))
    for row in rows:
        print(" | ".join(str(v).ljust(w) for v, w in zip(row, widths)))


def matplotlib_plot(series, sizes, out_path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    # Order matches paper's Fig. 5(a) bar grouping.
    # Paper:  pFabric (green/striped), pHost (yellow/dotted), dcPIM (blue/striped), dcSim (red/solid)
    # We swap pHost -> DCTCP as we don't have pHost in this fork.
    bar_order = [
        ("pfabric", "pFabric", "#2ca02c", "/"),    # green, forward stripes
        ("dctcp",   "DCTCP",   "#ffbb33", "."),    # yellow, dotted
        ("dcpim",   "dcPIM",   "#1f77b4", "\\"),   # blue, back stripes
        ("dcsim",   "dcSim",   "#d62728", ""),     # red, solid
    ]

    n_groups = len(sizes)                            # 4 flow sizes
    n_bars   = len([1 for k, *_ in bar_order if k in series])
    bar_w    = 0.8 / n_bars                          # total group width = 0.8

    fig, ax = plt.subplots(figsize=(7.5, 5))
    x = np.arange(n_groups)

    for i, (alg, label, color, hatch) in enumerate(bar_order):
        if alg not in series:
            continue
        heights = [series[alg].get(s, 0) for s in sizes]
        offset = (i - (n_bars - 1) / 2.0) * bar_w
        ax.bar(x + offset, heights, bar_w,
               label=label, color=color, hatch=hatch,
               edgecolor="black", linewidth=0.6)

    # X axis: BDP-multiple labels if we recognise the sizes, else raw packet counts.
    if all(s in SIZE_TO_BDP_LABEL for s in sizes):
        xlabels = [SIZE_TO_BDP_LABEL[s] for s in sizes]
        ax.set_xlabel("Flow size (multiple of BDP)")
    else:
        xlabels = [f"{s}pkt" for s in sizes]
        ax.set_xlabel("Flow size (packets, jumbo 9 KB each)")
    ax.set_xticks(x)
    ax.set_xticklabels(xlabels)

    ax.set_ylabel("Collective Completion Time (us)")
    n_hosts = (int(K_LABEL) ** 3) // 4 if K_LABEL.isdigit() else "?"
    ax.set_title(f"Fig. 5(a) reproduction - Permutation, Fat-Tree k={K_LABEL} ({n_hosts} hosts), 800 Gbps")
    ax.grid(True, axis="y", linestyle="--", alpha=0.4)
    ax.legend(loc="upper left", framealpha=0.9)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"\nSaved: {out_path}")


def main():
    if not CSV_PATH.exists():
        print(f"ERROR: {CSV_PATH} not found. Run aggregate.py first.")
        sys.exit(1)

    series = load_csv(CSV_PATH)
    sizes = sorted({s for d in series.values() for s in d.keys()})

    try:
        matplotlib_plot(series, sizes, PNG_PATH)
    except ImportError:
        ascii_plot(series, sizes)
        print("\nTo get a PNG, install matplotlib:  pip install matplotlib")

    # Always print a summary table
    print("\n=== CCT (us) by algorithm and flow size ===")
    print(f"{'alg':<10}", end="")
    for s in sizes:
        print(f"{s:>10}pkt", end="")
    print()
    for alg in ["dcsim", "dcpim", "pfabric", "dctcp"]:
        if alg not in series:
            continue
        d = series[alg]
        print(f"{alg:<10}", end="")
        for s in sizes:
            v = d.get(s, None)
            if v is not None:
                print(f"{v:>13.1f}", end="")
            else:
                print(f"{'-':>13}", end="")
        print()


if __name__ == "__main__":
    main()
