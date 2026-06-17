import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# Define runs: (fct_file_path, algorithm, bdp_label)
runs = {
     "phost": {
        "0.5x": "output/phost/bdp0.5/fct.txt",
        "1x":   "output/phost/bdp1/fct.txt",
        "2x":   "output/phost/bdp2/fct.txt",
    },
     "dcpim": {
        "0.5x": "output/dcpim/bdp0.5/fct.txt",
        "1x":   "output/dcpim/bdp1/fct.txt",
        "2x":   "output/dcpim/bdp2/fct.txt",
    },
    "dcsim": {
        "0.5x": "output/dcsim/bdp0.5/fct.txt",
        "1x":   "output/dcsim/bdp1/fct.txt",
        "2x":   "output/dcsim/bdp2/fct.txt",
    }
}

bdp_labels = ["0.5x", "1x", "2x"]
algorithms = ["phost","dcpim","dcsim"]
colors     = {"phost": "yellow",  "dcpim": "blue","dcsim": "red"}
hatches    = {"dcsim": "..", "dcpim": "//", "phost": "x"}

def get_cct(filepath):
    fcts = []
    try:
        with open(filepath) as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 2:
                    fcts.append(float(parts[1]))
        return max(fcts) if fcts else 0
    except FileNotFoundError:
        print(f"WARNING: {filepath} not found, skipping")
        return 0

# Collect CCTs
ccts = {alg: [] for alg in algorithms}
for alg in algorithms:
    for bdp in bdp_labels:
        ccts[alg].append(get_cct(runs[alg][bdp]))

# Plot
x = np.arange(len(bdp_labels))
width = 0.20

fig, ax = plt.subplots(figsize=(9, 5))

for i, alg in enumerate(algorithms):
    offset = (i - 0.5) * width
    bars = ax.bar(x + offset, ccts[alg], width,
                  label=alg.upper(),
                  color=colors[alg],
                  hatch=hatches[alg],
                  edgecolor='black')

ax.set_xlabel("Flow Size (multiple of BDP)")
ax.set_ylabel("Collective Completion Time (µs)")
ax.set_title("dcSim vs dcPIM vs phost- All-to-All CCT Comparison")
ax.set_xticks(x)
ax.set_xticklabels(bdp_labels)
ax.legend()
ax.grid(axis='y')
plt.tight_layout()
plt.savefig("output/dcsim_vs_dcpim_vs_phost_cct.png")
print("Saved to output/dcsim_vs_dcpim_vs_phost_cct.png")