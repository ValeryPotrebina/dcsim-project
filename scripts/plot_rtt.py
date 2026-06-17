#!/usr/bin/env python3
"""
Shows why adaptive SNS ≈ async SNS: EWMA converges to actual observed RTT.

RTT log format: flow_id  ewma_rtt_us  measured_rtt_us

Folders:
  output/adaptive_sns/load0.4/record_rtt.txt
  output/adaptive_sns/load0.6/record_rtt.txt
  output/adaptive_sns/load0.8/record_rtt.txt
  output/async_sns/load0.4/record_rtt.txt
  output/async_sns/load0.6/record_rtt.txt
  output/async_sns/load0.8/record_rtt.txt

Usage:
  python3 plot_rtt.py
  python3 plot_rtt.py --out output/rtt_analysis.png
"""

import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

RTT_MAX  = 22.0  # fixed dcSim RTT_max in µs
RTT_ZERO = 7.8   # zero-load RTT in µs
LOADS    = ["0.4", "0.6", "0.8"]


def load_rtt(path):
    ewma, measured = [], []
    try:
        with open(path) as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) == 3:
                    ewma.append(float(parts[1]))
                    measured.append(float(parts[2]))
        print(f"  loaded {len(ewma):,} records from {path}")
    except FileNotFoundError:
        print(f"  WARNING: {path} not found")
    return np.array(ewma), np.array(measured)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="output/rtt_analysis.png")
    args = parser.parse_args()

    # load all files
    adaptive, async_sns = {}, {}
    for load in LOADS:
        adaptive[load]  = load_rtt(f"output/adaptive_sns/load{load}/record_rtt.txt")
        async_sns[load] = load_rtt(f"output/async_sns/load{load}/record_rtt.txt")

    load_colors = {"0.4": "#2ecc71", "0.6": "#e67e22", "0.8": "#e74c3c"}

    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle(
        "Why adaptive SNS ≈ async SNS: EWMA converges to actual Sim-Ack RTT",
        fontsize=12, fontweight="bold"
    )

    # ── Panel 1: median adaptive (EWMA) vs median async (measured) per load ──
    ax = axes[0]
    x = np.arange(len(LOADS))
    w = 0.3

    ewma_meds = []
    meas_meds = []
    for load in LOADS:
        e, m = adaptive[load]
        ewma_meds.append(np.median(e) if len(e) > 0 else 0)
        e2, m2 = async_sns[load]
        meas_meds.append(np.median(m2) if len(m2) > 0 else 0)

    ax.bar(x - w/2, ewma_meds, w, label="Adaptive: EWMA RTT",
           color="#2ecc71", edgecolor="black")
    ax.bar(x + w/2, meas_meds, w, label="Async: measured RTT",
           color="gold", edgecolor="black")
    ax.axhline(y=RTT_MAX,  color='red',  linestyle='--',
               linewidth=1.5, label=f'Fixed RTT_max = {RTT_MAX}µs')
    ax.axhline(y=RTT_ZERO, color='grey', linestyle=':',
               linewidth=1.2, label=f'Zero-load RTT = {RTT_ZERO}µs')
    ax.set_xticks(x)
    ax.set_xticklabels([f"Load {l}" for l in LOADS])
    ax.set_ylabel("Median RTT (µs)")
    ax.set_title("Delay used to schedule Data packet")
    ax.legend(fontsize=8)
    ax.grid(axis='y', alpha=0.3)

    # ── Panel 2: CDF of gap (EWMA - measured) per load ───────────────────────
    ax = axes[1]
    for load in LOADS:
        e_adapt, _ = adaptive[load]
        _, m_async  = async_sns[load]
        n = min(len(e_adapt), len(m_async))
        if n == 0:
            continue
        gap = e_adapt[:n] - m_async[:n]
        sorted_gap = np.sort(gap)
        cdf = np.arange(1, len(sorted_gap) + 1) / len(sorted_gap)
        ax.plot(sorted_gap, cdf, label=f"Load {load}",
                color=load_colors[load], linewidth=1.8)

    ax.axvline(x=0, color='black', linestyle='--',
               linewidth=1.2, label='gap = 0\n(adaptive = async)')
    ax.set_xlabel("EWMA RTT − measured RTT (µs)\n(≈0 means adaptive waits same as async)")
    ax.set_ylabel("CDF")
    ax.set_title("Gap between adaptive and async scheduling delay")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # ── Panel 3: summary table ────────────────────────────────────────────────
    ax = axes[2]
    ax.axis('off')

    rows = []
    for load in LOADS:
        e_adapt, _ = adaptive[load]
        _, m_async  = async_sns[load]
        if len(e_adapt) == 0 or len(m_async) == 0:
            continue
        med_ewma = np.median(e_adapt)
        med_meas = np.median(m_async)
        gap      = med_ewma - med_meas
        rows.append([
            f"Load {load}",
            f"{med_ewma:.2f}µs",
            f"{med_meas:.2f}µs",
            f"{gap:+.2f}µs",
            f"{RTT_MAX:.1f}µs",
        ])

    col_labels = [
        "Load",
        "Adaptive\n(EWMA RTT)",
        "Async\n(measured RTT)",
        "Gap",
        "Fixed\nRTT_max",
    ]
    if rows:
        table = ax.table(
            cellText=rows,
            colLabels=col_labels,
            loc='center',
            cellLoc='center'
        )
        table.auto_set_font_size(False)
        table.set_fontsize(9)
        table.scale(1.1, 2.5)

    ax.set_title(
        "Gap ≈ 0 → adaptive and async schedule Data\n"
        "at the same time → same FCT\n"
        f"Both << {RTT_MAX}µs (fixed) → both beat fixed dcSim",
        fontsize=9
    )

    plt.tight_layout()
    plt.savefig(args.out, dpi=150, bbox_inches="tight")
    print(f"\nSaved to {args.out}")

    # terminal summary
    print(f"\n{'Load':<8} {'Adaptive EWMA':>15} {'Async measured':>16} "
          f"{'Gap':>8} {'Fixed RTT_max':>14}")
    print("-" * 66)
    for load in LOADS:
        e, _ = adaptive[load]
        _, m  = async_sns[load]
        if len(e) > 0 and len(m) > 0:
            gap = np.median(e) - np.median(m)
            print(f"{load:<8} {np.median(e):>13.2f}µs {np.median(m):>14.2f}µs "
                  f"{gap:>+7.2f}µs {RTT_MAX:>12.1f}µs")

if __name__ == "__main__":
    main()