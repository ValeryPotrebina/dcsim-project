import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

loads = [0.4, 0.6, 0.8]
algos = {
    'dcSim': ('output/poisson_dcsim/load{}/fct.txt', 'red'),
    'dcPIM': ('output/poisson_dcpim/load{}/fct.txt', 'royalblue'),
    'adaptive_sns': ('output/adaptive_sns/load{}/fct.txt', 'green'),
    'async_sns': ('output/async_sns/load{}/fct.txt', 'yellow')
}


def stats(path):
    fcts = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                fcts.append(float(parts[1]))   # column 2 = FCT (us)
    a = np.array(fcts)
    return a.mean(), np.percentile(a, 50), np.percentile(a, 99)

# data[algo] = (avg[], p50[], p99[]) over the three loads
data = {}
for name, (tmpl, _) in algos.items():
    avg, p50, p99 = [], [], []
    for L in loads:
        m, med, t = stats(tmpl.format(L))
        avg.append(m); p50.append(med); p99.append(t)
    data[name] = (avg, p50, p99)

x = np.arange(len(loads))
width = 0.20
fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))
panels = [('Median FCT (typical flow)', 1),
          ('Average FCT', 0),
          ('p99 FCT (tail)', 2)]
for ax, (title, idx) in zip(axes, panels):
    for k, (name, (tmpl, color)) in enumerate(algos.items()):
        ys = data[name][idx]
        bars = ax.bar(x + (k - 0.5) * width, ys, width, label=name, color=color)
        # ax.bar_label(bars, fmt='%.0f', fontsize=8)
    ax.set_title(title)
    ax.set_xlabel('Load')
    ax.set_ylabel('FCT (us)')
    ax.set_xticks(x)
    ax.set_xticklabels([str(l) for l in loads])
    ax.legend()
    ax.grid(axis='y', alpha=0.3)

fig.suptitle('Web-search: dcSim vs dcPIM vs adaptive SNS vs async SNS')
plt.tight_layout()
plt.savefig('output/poisson_compare_bars-1.png')
print('Saved to output/poisson_compare_bars.png')
